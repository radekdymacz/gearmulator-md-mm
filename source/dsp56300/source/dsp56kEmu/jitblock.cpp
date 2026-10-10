#include "dsp.h"
#include "interrupts.h"
#include "jitemitter.h"
#include "jitblock.h"

#include "jitblockinfo.h"
#include "jitblockruntimedata.h"
#include "jitops.h"
#include "memory.h"
#include "opcodecycles.h"
#include "peripherals.h"

namespace dsp56k
{
	JitBlock::JitBlock(JitEmitter& _a, DSP& _dsp, JitRuntimeData& _runtimeData, JitConfig&& _config)
	: m_runtimeData(_runtimeData)
	, m_asm(_a)
	, m_dsp(_dsp)
	, m_stack(*this)
	, m_xmmPool({regXMMTempA})
	, m_gpPool(g_regGPTemps)
	, m_dspRegs(*this)
	, m_dspRegPool(*this)
	, m_mem(*this)
	, m_config(std::move(_config))
	{
	}

	JitBlock::~JitBlock() = default;

	void JitBlock::markPeripheralAccess()
	{
		// Normal block compilation assigns this in emit(). The instruction-level
		// JIT unit harness emits operations directly, without a cache/runtime entry;
		// it still needs to compile peripheral branches, but has no block metadata to
		// annotate. In Release builds the old assert disappeared and the following
		// dereference crashed while building BCLR (ea).
		if(!m_currentJitBlockRuntimeData)
			return;
		m_currentJitBlockRuntimeData->m_info.addFlag(
			JitBlockInfo::Flags::PeripheralAccess);
	}

	namespace
	{
		void callFastForwardNopLoop(DSP* _dsp, const uint32_t _instructionsPerTurn, const uint32_t _cyclesPerTurn, const uint32_t _turnsPerExit)
		{
			_dsp->fastForwardNopLoop(_instructionsPerTurn, _cyclesPerTurn, _turnsPerExit);
		}

		void callFastForwardPollLoop(DSP* _dsp, const uint32_t _instructionsPerTurn, const uint32_t _cyclesPerTurn)
		{
			_dsp->fastForwardPollLoop(_instructionsPerTurn, _cyclesPerTurn);
		}

		// The ops an idle poll loop may hold besides its final branch: register-only ALU work that does not take the
		// condition codes as an input (the conditions of Ifcc and of the branch are tested separately), NOPs, and
		// reads of DMA registers, which have no side effect and change only in a peripheral run.
		bool isPollLoopAlu(const Instruction _inst)
		{
			switch(_inst)
			{
			case Add_SD: case Sub_SD: case Cmp_S1S2: case Cmpm_S1S2: case Tst: case And_SD: case Or_SD: case Eor_SD:
			case Tfr: case Clr: case Abs: case Neg:
				return true;
			default:
				return false;
			}
		}

		bool isPollLoopImmediateAlu(const Instruction _inst)
		{
			switch(_inst)
			{
			case Add_xx: case Add_xxxx: case Sub_xx: case Sub_xxxx: case Cmp_xxS2: case Cmp_xxxxS2: case And_xx:
			case And_xxxx: case Or_xx: case Or_xxxx:
				return true;
			default:
				return false;
			}
		}

		bool isDmaRegister(const TWord _addr)
		{
			return _addr >= XIO_DCR5 && _addr <= XIO_DSTR;
		}

		// jclr/jset/brclr/brset #n,x:pp,target on a DMA register: reads it and branches, nothing else
		template<Instruction Inst> bool isDmaBitTestBranch(const TWord _op)
		{
			if(getFieldValue<Inst, Field_S>(_op))
				return false;	// Y space
			return isDmaRegister(getFieldValue<Inst, Field_pppppp>(_op) + 0xffffc0);
		}

		bool isDmaBitTestBranch(const Instruction _inst, const TWord _op)
		{
			switch(_inst)
			{
			case Jclr_pp:	return isDmaBitTestBranch<Jclr_pp>(_op);
			case Jset_pp:	return isDmaBitTestBranch<Jset_pp>(_op);
			case Brclr_pp:	return isDmaBitTestBranch<Brclr_pp>(_op);
			case Brset_pp:	return isDmaBitTestBranch<Brset_pp>(_op);
			default:		return false;
			}
		}

		bool isDmaRegisterRead(const Instruction _inst, const TWord _op)
		{
			if(_inst != Movep_Spp)
				return false;
			if(getFieldValue<Movep_Spp, Field_W>(_op) || getFieldValue<Movep_Spp, Field_s>(_op))
				return false;	// a write, or Y space
			return isDmaRegister(getFieldValue<Movep_Spp, Field_pppppp>(_op) + 0xffffc0);
		}

		// an op of a poll loop other than its branches
		bool isPollLoopBodyOp(const Instruction _instA, const Instruction _instB, const TWord _opA)
		{
			if(_instA == Nop)
				return true;
			if(isDmaRegisterRead(_instA, _opA) || isPollLoopImmediateAlu(_instA))
				return _instB == Invalid;
			if(isPollLoopAlu(_instA))
				return _instB == Move_Nop || _instB == Ifcc || _instB == Ifcc_U;
			return false;
		}

		// a branch that may end a poll loop or leave it: to a fixed address, on a condition code or a DMA register bit
		bool isPollLoopConditionalBranch(const Instruction _instA, const Instruction _instB, const TWord _opA)
		{
			if(_instB != Invalid)
				return false;
			return _instA == Bcc_xxxx || _instA == Bcc_xxx || _instA == Jcc_xxx || isDmaBitTestBranch(_instA, _opA);
		}

		bool isPollLoopJump(const Instruction _instA, const Instruction _instB)
		{
			return _instB == Invalid && (_instA == Bra_xxxx || _instA == Bra_xxx || _instA == Jmp_xxx);
		}

		RegisterMask notMask(const RegisterMask _m)
		{
			return static_cast<RegisterMask>(~static_cast<uint64_t>(_m));
		}

		// Track, over a turn, the registers read before the turn writes them: a turn repeats the one before only if
		// none of those is written in the turn (then each carries a value from the turn before).
		void trackPollLoopRegisters(RegisterMask& _carried, RegisterMask& _writtenSoFar, const Instruction _instA, const Instruction _instB, const TWord _opA)
		{
			auto written = RegisterMask::None;
			auto read = RegisterMask::None;
			if(_instA == Movep_Spp)
				written = getRegisters(Movep_Spp, Field_dddddd, _opA);	// getRegisters() leaves out a movep's destination
			else
				Opcodes::getRegisters(written, read, _opA, _instA, _instB);

			_carried |= read & notMask(_writtenSoFar);
			_writtenSoFar |= written;
		}

		void callFastForwardPollCycle(DSP* _dsp, const JitPollCycle* _cycle)
		{
			_dsp->fastForwardPollCycle(*_cycle);
		}
	}

	void JitBlock::emitFastForwardGate(const asmjit::Label& _skip, const uint64_t _cyclesAhead, const uint64_t _instructionsAhead)
	{
		// Skip the call unless _cyclesAhead more cycles stay below the run's stop and the peripherals' cycle deadline,
		// and, when given, _instructionsAhead more instructions below the peripherals' instruction target.
		// Signed compares: a limit of 0 (none published, or the fast-forward off) always skips.
		const auto* periph = m_dsp.getPeriph(0);
		const RegGP value(*this);
		const RegGP horizon(*this);
		if(_instructionsAhead)
		{
			mem().mov(r64(horizon), m_dsp.getInstructionCounter());
			m_asm.add(r64(horizon), asmjit::Imm(_instructionsAhead));
			mem().mov(r64(value), *reinterpret_cast<const uint64_t*>(periph->getTargetClockPtr()));
			m_asm.cmp(r64(value), r64(horizon));
			m_asm.jle(_skip);
		}
		mem().mov(r64(horizon), m_dsp.getCycles());
		m_asm.add(r64(horizon), asmjit::Imm(_cyclesAhead));
		mem().mov(r64(value), m_dsp.getFastForwardCycleLimit());
		m_asm.cmp(r64(value), r64(horizon));
		m_asm.jle(_skip);
		const auto noDeadline = m_asm.newLabel();
		mem().mov(r32(value), reinterpret_cast<const uint8_t&>(periph->hasCycleDeadline()));
		m_asm.test_(r32(value));
		m_asm.jz(noDeadline);
		mem().mov(r64(value), periph->getTargetCycle());
		m_asm.cmp(r64(value), r64(horizon));
		m_asm.jle(_skip);
		m_asm.bind(noDeadline);
	}

	bool JitBlock::isIdlePollLoop(const JitBlockInfo& _info, const TWord _pc, const bool _isFastInterrupt, const std::set<TWord>& _loopEnds) const
	{
		// A block that ends in a conditional branch to its own start, and whose turns repeat each other: it reads only
		// DMA registers, writes no memory, and no register it reads carries a value from the turn before (every one
		// is either never written in the block or written before it is read). The words are the ones in P memory now:
		// a write to them destroys the block. Below P:$100 a block may also run as a fast interrupt: there only a
		// lone bit-test branch qualifies (jset #n,x:DCR0,*), and only with the processing mode tested at run time
		// (dynamic fast interrupts; DSP::fastForwardPollLoop skips nothing outside the default mode).
		if(!m_config.pollLoopFastForward || (_isFastInterrupt && !m_config.dynamicFastInterrupts))
			return false;
		if(_info.terminationReason != JitBlockInfo::TerminationReason::Branch || _info.branchTarget != _pc || !_info.branchIsConditional)
			return false;
		if(_info.hasFlag(JitBlockInfo::Flags::IsLoopBodyBegin) || _info.hasFlag(JitBlockInfo::Flags::ModeChange))
			return false;
		if(_loopEnds.find(_pc + _info.memSize) != _loopEnds.end())
			return false;

		const auto& opcodes = m_dsp.opcodes();

		auto writtenSoFar = RegisterMask::None;
		auto carried = RegisterMask::None;

		for(TWord q = 0; q < _info.memSize;)
		{
			TWord opA, opB;
			m_dsp.memory().getOpcode(_pc + q, opA, opB);

			Instruction instA, instB;
			opcodes.getInstructionTypes(opA, instA, instB);

			const auto len = Opcodes::getOpcodeLength(opA, instA, instB);
			if(!len)
				return false;

			const bool last = q + len == _info.memSize;

			if(_isFastInterrupt && (q || !last))
				return false;

			if(last)
			{
				if(!isPollLoopConditionalBranch(instA, instB, opA))
					return false;
			}
			else if(!isPollLoopBodyOp(instA, instB, opA))
			{
				return false;
			}

			trackPollLoopRegisters(carried, writtenSoFar, instA, instB, opA);

			q += len;
		}

		// the PC is the branch's; every other register written must not be read before it is written
		return (carried & writtenSoFar & notMask(RegisterMask::PC)) == RegisterMask::None;
	}

	bool JitBlock::findPollCycle(JitPollCycle& _cycle, const TWord _pc, const std::map<TWord, TWord>& _loopStarts, const std::set<TWord>& _loopEnds) const
	{
		// A poll loop over several blocks that holds the block starting at _pc: from its head to a branch back to the
		// head, ops as in isIdlePollLoop, and in between only conditional branches out of the loop (its exits). Found
		// from the words in P memory now, the same way from each of its blocks; the blocks keep a marker of the turn
		// (emit), named by a hash of the words. Linked blocks call each other without the
		// dispatcher, whose boundaries the fast-forward lands on: not with them.
		if(!m_config.pollCycleFastForward || m_config.linkJitBlocks)
			return false;

		constexpr TWord maxWords = 32;

		const auto& opcodes = m_dsp.opcodes();

		struct Op { Instruction instA, instB; TWord opA, opB, len; };
		const auto decode = [&](const TWord _addr, Op& _op)
		{
			m_dsp.memory().getOpcode(_addr, _op.opA, _op.opB);
			opcodes.getInstructionTypes(_op.opA, _op.instA, _op.instB);
			_op.len = Opcodes::getOpcodeLength(_op.opA, _op.instA, _op.instB);
			return _op.len != 0;
		};

		// forward from _pc to the branch back to it or before it: the closing branch, its target the head
		TWord head = g_invalidAddress;
		TWord end = 0;

		for(TWord a = _pc; a < _pc + maxWords;)
		{
			Op op;
			if(!decode(a, op))
				return false;

			const bool conditional = isPollLoopConditionalBranch(op.instA, op.instB, op.opA);
			const bool jump = isPollLoopJump(op.instA, op.instB);

			if(conditional || jump)
			{
				const auto target = getBranchTarget(op.instA, op.opA, op.opB, a);
				if(target <= _pc)
				{
					head = target;
					end = a + op.len;
					break;
				}
				if(jump)
					return false;
			}
			else if(!isPollLoopBodyOp(op.instA, op.instB, op.opA))
			{
				return false;
			}
			a += op.len;
		}

		if(head == g_invalidAddress || end - head > maxWords)
			return false;

		// below P:$100 a block may run as a fast interrupt: only with the processing mode tested at run time
		if(head < Vba_End && !m_config.dynamicFastInterrupts)
			return false;

		// from the head: _pc must start an op, the exits must leave the loop, the turns must repeat each other
		auto writtenSoFar = RegisterMask::None;
		auto carried = RegisterMask::None;
		bool hasPc = false;
		uint32_t instructions = 0;
		uint32_t cycles = 0;

		for(TWord a = head; a < end;)
		{
			Op op;
			if(!decode(a, op))
				return false;

			hasPc |= a == _pc;

			// no DO loop begins or ends inside
			if(_loopEnds.find(a + op.len) != _loopEnds.end() || (a >= 2 && _loopStarts.find(a - 2) != _loopStarts.end()))
				return false;

			const bool last = a + op.len == end;
			const bool conditional = isPollLoopConditionalBranch(op.instA, op.instB, op.opA);

			if(last)
			{
				if(!conditional && !isPollLoopJump(op.instA, op.instB))
					return false;
				if(getBranchTarget(op.instA, op.opA, op.opB, a) != head)
					return false;
			}
			else if(conditional)
			{
				const auto target = getBranchTarget(op.instA, op.opA, op.opB, a);
				if(target >= head && target < end)
					return false;
			}
			else if(!isPollLoopBodyOp(op.instA, op.instB, op.opA))
			{
				return false;
			}

			trackPollLoopRegisters(carried, writtenSoFar, op.instA, op.instB, op.opA);

			++instructions;
			cycles += calcCycles(op.instA, op.instB, a, op.opA, m_dsp.memory().getBridgedMemoryAddress(), 1);

			a += op.len;
		}

		if(!hasPc || (carried & writtenSoFar & notMask(RegisterMask::PC)) != RegisterMask::None)
			return false;

		_cycle.head = head;
		_cycle.end = end;
		_cycle.instructions = instructions;
		_cycle.cycles = cycles;

		// the id: a hash of the words, so that a block made from other words never passes the marker on
		uint32_t id = 2166136261u;
		const auto hash = [&](const TWord _w) { id = (id ^ _w) * 16777619u; };
		hash(head);
		hash(end);
		for(TWord a = head; a < end; ++a)
			hash(m_dsp.memory().get(MemArea_P, a));
		_cycle.id = id ? id : 1;
		return true;
	}

	bool JitBlock::isNopLoopBody(const JitBlockInfo& _info, const TWord _pc, const bool _isFastInterrupt) const
	{
		// a DO loop body (the block starts at the loop start and ends at the loop end) that returns to the dispatcher
		// every maxDoIterations turns, made of NOPs only (the words as they are in P memory now: a write to them
		// destroys the block)
		if(!m_config.nopLoopFastForward || !m_config.maxDoIterations || _isFastInterrupt)
			return false;
		if(!_info.hasFlag(JitBlockInfo::Flags::IsLoopBodyBegin) || _info.terminationReason != JitBlockInfo::TerminationReason::LoopEnd)
			return false;
		if(!_info.memSize || _info.instructionCount != _info.memSize)
			return false;
		for(TWord i = 0; i < _info.memSize; ++i)
		{
			TWord opA, opB;
			m_dsp.memory().getOpcode(_pc + i, opA, opB);
			if(opA != 0)	// nop
				return false;
		}
		return true;
	}

	void JitBlock::getInfo(JitBlockInfo& _info, const DSP& _dsp, const TWord _pc, const JitConfig& _config, const PagedArray<JitCacheEntry>& _cache, const std::set<TWord>& _volatileP, const std::map<TWord, TWord>& _loopStarts, const std::set<TWord>& _loopEnds)
	{
		const auto& opcodes = _dsp.opcodes();

		const bool isFastInterrupt = _pc < Vba_End;

		const TWord pcMax = isFastInterrupt ? (_pc + 2) : _dsp.memory().sizeP();

		bool shouldEmit = true;

		auto& readRegs = _info.readRegs;
		auto& writtenRegs = _info.writtenRegs;

		bool writesSR = false;
		bool readsSR = false;

		auto& numWords = _info.memSize;
		auto& numInstructions = _info.instructionCount;
		auto& numCycles = _info.cycleCount;

		auto& terminationReason = _info.terminationReason;

		_info.pc = _pc;

		if(_loopStarts.find(_pc - 2) != _loopStarts.end())
		{
			// Note: a loop body may also be compiled while the DSP is NOT currently executing
			// that loop (the address is reachable as regular flow, or the block was invalidated
			// by a program rewrite and recompiles later). The emitted loop epilogue verifies the
			// loop state at runtime (jumpIfLoop compares against the hardware stack), so the
			// flag is safe to set either way.
			_info.addFlag(JitBlockInfo::Flags::IsLoopBodyBegin);
		}

		auto writesM = RegisterMask::None;
		auto readsM = RegisterMask::None;

		while(shouldEmit)
		{
			const auto pc = _pc + numWords;

			if(pc >= pcMax)
			{
				terminationReason = JitBlockInfo::TerminationReason::PcMax;
				break;
			}

			// never overwrite code that already exists
			if(pc < _cache.size() && _cache[pc].block)
			{
				terminationReason = JitBlockInfo::TerminationReason::ExistingCode;
				break;
			}

			TWord opA;
			TWord opB;
			_dsp.memory().getOpcode(pc, opA, opB);

			Instruction instA, instB;

			opcodes.getInstructionTypes(opA, instA, instB);

			auto written = RegisterMask::None;
			auto read = RegisterMask::None;

			Opcodes::getRegisters(written, read, opA, instA, instB);

			const auto writtenM = (written & RegisterMask::M);
			const auto readM = read & RegisterMask::M;

			const auto flags = Opcodes::getFlags(instA, instB);

			// a jsr in a fast interrupt modifies the MR because it disables scaling mode bits, loop flag and sixteen-bit arithmetic mode
			if(isFastInterrupt && (written & RegisterMask::SSL) != RegisterMask::None)
				written |= RegisterMask::MR;

			const auto srModeChange = (written & (RegisterMask::EMR | RegisterMask::MR)) != RegisterMask::None;

			if(srModeChange)
			{
				_info.addFlag(JitBlockInfo::Flags::ModeChange);
			}

			if(writtenM != RegisterMask::None)
			{
				writesM |= writtenM;
				_info.addFlag(JitBlockInfo::Flags::ModeChange);
			}
			if(readM != RegisterMask::None)
			{
				const auto readAfterWrite = readM & writesM;
				readsM |= readAfterWrite;
			}

			// while an M register change causes a mode change, we can continue this block as long as that M register is not read in a subsequent instruction
			if((writesM & readsM) != RegisterMask::None)
			{
				if(numInstructions)
				{
					_info.terminationReason = JitBlockInfo::TerminationReason::ModeChange;
					break;
				}
			}

			// for a volatile P address, if you have some code, break now. if not, generate this one op, and then return.
			if (_volatileP.find(pc) != _volatileP.end() || 
				(_volatileP.find(pc+1) != _volatileP.end() && Opcodes::getOpcodeLength(opA, instA, instB) == 2))
			{
				terminationReason = JitBlockInfo::TerminationReason::VolatileP;
				if (numInstructions)
					break;
				shouldEmit = false;
			}

			const auto isRep = flags & (OpFlagRepDynamic | OpFlagRepImmediate);

			writtenRegs |= written;
			readRegs |= read;

			if ((readRegs & RegisterMask::SR) != RegisterMask::None)
				readsSR = true;

			if ((writtenRegs & RegisterMask::SR) != RegisterMask::None)
				writesSR = true;

			if (writesSR && !readsSR)
				_info.addFlag(JitBlockInfo::Flags::WritesSRbeforeRead);

			const auto opLen = Opcodes::getOpcodeLength(opA, instA, instB);

			if(opLen == 0)
			{
				// The word at 'pc' decodes to no valid instruction (getOpcodeLength() == 0). Without
				// this guard the analysis loop spins forever: numWords never advances, so the
				// pc >= pcMax termination (and every other one) is never reached - a hard hang of the
				// whole emulator. This can only happen if the DSP's PC lands on a non-opcode word (e.g.
				// mid-instruction, via a corrupted control-flow landing). Count the word as one
				// instruction so the block makes forward runtime progress and terminate the block here;
				// emit() renders an undecodable word as a NOP.
				static const bool logInvalid = std::getenv("DSP_LOG_INVALIDOP") != nullptr;
				if(logInvalid)
					fprintf(stderr, "[DSP_LOG_INVALIDOP] block start pc=%06x decodes to Invalid (op=%06x:%06x) -> NOP-terminated\n", pc, opA, opB);

				numWords += 1;
				++numInstructions;
				numCycles += 1;
				terminationReason = JitBlockInfo::TerminationReason::InvalidInstruction;
				break;
			}

			numWords += opLen;
			++numInstructions;
			numCycles += calcCycles(instA, instB, pc, opA, _dsp.memory().getBridgedMemoryAddress(), 1);

			if(getLoopEndAddr(_info.loopEnd, instA, pc, opB))
				_info.loopBegin = pc;

			if(!isRep)
			{
				if(flags & OpFlagBranch)
				{
					terminationReason = JitBlockInfo::TerminationReason::Branch;
					_info.branchTarget = getBranchTarget(instA, opA, opB, pc);
					_info.branchIsConditional = hasField(instA, Field_CCCC) || hasField(instA, Field_bbbbb);
					break;
				}
				if(flags & OpFlagPopPC)
				{
					terminationReason = JitBlockInfo::TerminationReason::PopPC;
					break;
				}
				if(any(written, RegisterMask::LA | RegisterMask::LC))
				{
					terminationReason = JitBlockInfo::TerminationReason::WriteLoopRegs;
					break;
				}

				if(flags & OpFlagLoop)
				{
					terminationReason = JitBlockInfo::TerminationReason::LoopBegin;
					break;
				}

				if(instA == Wait)
				{
					terminationReason = JitBlockInfo::TerminationReason::WaitInstruction;
					break;
				}
			}

			// always terminate block if loop end has reached
			if(_loopEnds.find(_pc + numWords) != _loopEnds.end())
			{
				assert((_pc + numWords) == static_cast<TWord>(_dsp.regs().la.var + 1));
				terminationReason = JitBlockInfo::TerminationReason::LoopEnd;
				break;
			}

			if(writesToPMemory(instA, opA) || writesToPMemory(instB, opA))
			{
				terminationReason = JitBlockInfo::TerminationReason::WritePMem;
				break;
			}

			if(srModeChange)
			{
				terminationReason = JitBlockInfo::TerminationReason::ModeChange;
				break;
			}

			// we must NOT prematurely abort the creation of a fast interrupt block
			if(!isRep && !isFastInterrupt && _config.maxInstructionsPerBlock > 0 && numInstructions >= _config.maxInstructionsPerBlock)
			{
				terminationReason = JitBlockInfo::TerminationReason::InstructionLimit;
				break;
			}
		}
	}

	bool JitBlock::emit(JitBlockRuntimeData& _rt, JitBlockChain* _chain, const TWord _pc, const PagedArray<JitCacheEntry>& _cache, const std::set<TWord>& _volatileP, const std::map<TWord, TWord>& _loopStarts, const std::set<TWord>& _loopEnds, bool _profilingSupport)
	{
		JitBlockGenerating generating(_rt);
		m_currentJitBlockRuntimeData = &_rt;

		auto& dspAsm = _rt.m_dspAsm;
		auto& info = _rt.m_info;
		auto& profilingInfo = _rt.m_profilingInfo;
		auto& childAddr = _rt.m_child;

		m_chain = _chain;

		const bool isFastInterrupt = _pc < Vba_End;
		const auto fastInterruptMode = isFastInterrupt ? (m_config.dynamicFastInterrupts ? JitOps::FastInterruptMode::Dynamic : JitOps::FastInterruptMode::Static) : JitOps::FastInterruptMode::None;

		dspAsm.clear();

		// needed so that the dsp register is available
		dspRegPool().makeDspPtr(&m_dsp.getInstructionCounter(), sizeof(uint64_t));

#ifdef HAVE_X86_64
		if constexpr (false)
		{
			const auto skip = m_asm.newLabel();
			RegScratch s(*this);
			m_asm.mov(s, asmjit::Imm(&m_dsp.regs()));
			m_asm.cmp(regDspPtr, s);
			m_asm.jz(skip);
			m_asm.int3();
			m_asm.bind(skip);
		}
#endif

		PushAllUsed pm(*this);

		asmjit::BaseNode* cursorBeforeLoopBegin = m_asm.cursor();	// the idle fast-forward call goes here, see below

		auto loopBegin = m_asm.newNamedLabel("loopBegin");
		m_asm.bind(loopBegin);

		asmjit::BaseNode* cursorInsertIncreaseInstructionCount = m_asm.cursor();	// inserted later

		uint32_t blockFlags = 0;

		getInfo(info, dsp(), _pc, m_config, _cache, _volatileP, _loopStarts, _loopEnds);

		if(isNopLoopBody(info, _pc, isFastInterrupt))
		{
			// Once per entry of the block, before its first turn (the turns it runs inside jump to loopBegin): skip
			// whole block executions while the dispatcher would only find nothing to do (DSP::fastForwardNopLoop).
			// Nothing is loaded into host registers yet, so the call sees the registers in memory and the turns load
			// them afresh. Per turn: one instruction per NOP word, and the cycles getInfo counted for the body.
			auto* const cursor = m_asm.cursor();
			m_asm.setCursor(cursorBeforeLoopBegin);

			// Call only when a whole block of turns fits before the run's stop and the peripherals' cycle deadline,
			// and LC still has a boundary ahead. These are the tests that fail most of the time (a playing machine
			// has a serial slot every 96 cycles), and the call costs more than the turns of one block. The call
			// tests everything again, exactly; this only saves calls that could not skip anything. Signed compares:
			// a limit of 0 (none published, or the fast-forward off) fails the first test.
			const auto noCall = m_asm.newLabel();
			emitFastForwardGate(noCall, static_cast<uint64_t>(m_config.maxDoIterations) * info.cycleCount);
			{
				const RegGP lc(*this);
				mem().mov(r32(lc), reinterpret_cast<const uint32_t&>(m_dsp.regs().lc.var));
				m_asm.cmp(r32(lc), asmjit::Imm(m_config.maxDoIterations));
				m_asm.jle(noCall);
			}
			{
				const FuncArg r0(*this, 0);
				const FuncArg r1(*this, 1);
				const FuncArg r2(*this, 2);
				const FuncArg r3(*this, 3);
				mem().makeDspPtr(r0);
				m_asm.mov(r32(r1), asmjit::Imm(info.memSize));
				m_asm.mov(r32(r2), asmjit::Imm(info.cycleCount));
				m_asm.mov(r32(r3), asmjit::Imm(m_config.maxDoIterations));
				stack().call(asmjit::func_as_ptr(&callFastForwardNopLoop));
			}
			m_asm.bind(noCall);
			m_asm.setCursor(cursor);
		}

		const auto pcNext = _pc + info.memSize;

		// A block of a poll loop over several blocks (findPollCycle; a loop in one block is isIdlePollLoop's). Its head
		// arms the marker of a straight turn before it runs, each block passes it on to the next at its end, and when
		// it comes back round to the head, the head calls the fast-forward before it arms it again.
		JitPollCycle pollCycle;
		const bool inPollCycle = !isFastInterrupt || fastInterruptMode == JitOps::FastInterruptMode::Dynamic
			? findPollCycle(pollCycle, _pc, _loopStarts, _loopEnds) && pcNext <= pollCycle.end
				&& !(_pc == pollCycle.head && pcNext == pollCycle.end)
				&& !info.hasFlag(JitBlockInfo::Flags::ModeChange) && !info.hasFlag(JitBlockInfo::Flags::IsLoopBodyBegin)
			: false;

		if(inPollCycle && _pc == pollCycle.head)
		{
			// Before the counters count this block, where nothing is loaded into host registers yet. When the marker
			// came round, a straight turn has just ended here: its next turns repeat it (DSP::fastForwardPollCycle).
			// Call only when a whole turn fits before the run's stop and the peripherals' deadlines (as for the NOP
			// loops: the call tests everything again, exactly). Then this turn starts: arm the marker.
			auto* const cursor = m_asm.cursor();
			m_asm.setCursor(cursorBeforeLoopBegin);

			_rt.m_pollCycle = std::make_unique<JitPollCycle>(pollCycle);

			const auto arm = m_asm.newLabel();
			{
				const RegGP value(*this);
				const RegGP expected(*this);
				mem().mov(r64(value), m_dsp.pollCycleMarker());
				m_asm.mov(r64(expected), asmjit::Imm(pollCycle.marker(_pc)));
				m_asm.cmp(r64(value), r64(expected));
				m_asm.jnz(arm);
			}
			emitFastForwardGate(arm, pollCycle.cycles, pollCycle.instructions);
			{
				const FuncArg r0(*this, 0);
				const FuncArg r1(*this, 1);
				mem().makeDspPtr(r0);
				m_asm.mov(r64(r1), asmjit::Imm(reinterpret_cast<uint64_t>(_rt.m_pollCycle.get())));
				stack().call(asmjit::func_as_ptr(&callFastForwardPollCycle));
			}
			m_asm.bind(arm);
			{
				const RegGP value(*this);
				m_asm.mov(r64(value), asmjit::Imm(pollCycle.marker(_pc)));
				mem().mov(m_dsp.pollCycleMarker(), r64(value));
				mem().mov(r64(value), m_dsp.getInstructionCounter());
				mem().mov(m_dsp.pollCycleHeadInstructions(), r64(value));
				mem().mov(r64(value), m_dsp.getCycles());
				mem().mov(m_dsp.pollCycleHeadCycles(), r64(value));
			}
			m_asm.setCursor(cursor);
		}

		if(fastInterruptMode != JitOps::FastInterruptMode::Static && info.terminationReason != JitBlockInfo::TerminationReason::PopPC)
		{
			if(info.branchTarget == g_invalidAddress || info.branchIsConditional)
			{
				// A dynamic fast-interrupt block executes both as normal flow and as a fast
				// interrupt. Write the fallthrough PC only in normal flow: when servicing a
				// fast interrupt this would clobber the interrupted PC, which a JSR inside
				// the block pushes as its return address - a "jsset #n,x:pp,handler" style
				// vector would then return INTO the following vector slot instead of into
				// the interrupted code.
				const auto pcReg = m_dspRegPool.get(PoolReg::DspPC, true, true);

				const SkipLabel skip(m_asm);

				if(fastInterruptMode == JitOps::FastInterruptMode::Dynamic)
				{
					const RegScratch s(*this);
					if constexpr (sizeof(m_dsp.m_processingMode) == sizeof(uint32_t))
						mem().mov(r64(s), reinterpret_cast<uint32_t&>(m_dsp.m_processingMode));
					else
						mem().mov(r32(s), reinterpret_cast<uint64_t&>(m_dsp.m_processingMode));
					m_asm.cmp(r32(s), asmjit::Imm(DSP::ProcessingMode::FastInterrupt));
					m_asm.jz(skip);
				}

				m_asm.mov(r32(pcReg), asmjit::Imm(pcNext));
			}
		}

		TWord opA = 0;
		TWord opB = 0;

#if defined(_DEBUG)
		std::string opDisasm;
#endif

		uint32_t opPC = 0;

		uint32_t& ccrRead = info.ccrRead;
		uint32_t& ccrWrite = info.ccrWrite;
		uint32_t& ccrOverwrite = info.ccrOverwrite;

		_rt.m_encodedCycles = info.cycleCount;

		TWord pMemSize = 0;

		while(pMemSize < info.memSize)
		{
			opPC = _pc + pMemSize;

			m_dsp.memory().getOpcode(opPC, opA, opB);

#if defined(_DEBUG)
			m_dsp.disassembler().disassemble(opDisasm, opA, opB, 0, 0, 0);
//			LOG(HEX(pc) << ": " << opDisasm);

			{
				Instruction instA, instB;
				m_dsp.opcodes().getInstructionTypes(opA, instA, instB);
				const auto& oi = g_opcodes[instA];

				if(oi.flag(OpFlagRepImmediate) || oi.flag(OpFlagRepDynamic))
				{
					std::string repInst;
					m_dsp.disassembler().disassemble(repInst, opB, 0, 0, 0, 0);
					opDisasm += '\n' + repInst;
//					LOG("REP:" << opDisasm);
				}
			}

			dspAsm += opDisasm + '\n';
			m_asm.comment(("DSPasm: " + opDisasm).c_str());
#endif

			if(_profilingSupport)
			{
				const auto labelBegin = m_asm.newLabel();
				const auto labelEnd = m_asm.newLabel();

				m_asm.bind(labelBegin);

				const JitBlockRuntimeData::InstructionProfilingInfo pi{ opPC, opA, opB, 0, 1, labelBegin, labelEnd, 0, 0, std::string()};
				profilingInfo.emplace_back(pi);
			}

			JitOps ops(*this, _rt, fastInterruptMode);

			if (m_config.splitOpsByNops)	m_asm.nop();
			ops.emit(opPC, opA, opB);
			if (m_config.splitOpsByNops)	m_asm.nop();

			if (_profilingSupport)
			{
				auto& pi = profilingInfo.back();

				pi.opLen = ops.getOpSize();

				const auto& oi = g_opcodes[ops.getInstruction()];

				if(oi.flag(OpFlagRepImmediate) || oi.flag(OpFlagRepDynamic))
					pi.lineCount++;

				m_asm.bind(pi.labelAfter);
			}

			blockFlags |= ops.getResultFlags();
			
			_rt.m_singleOpWordA = opA;
			_rt.m_singleOpWordB = opB;

			pMemSize += ops.getOpSize();
			++_rt.m_encodedInstructionCount;

			_rt.m_lastOpSize = ops.getOpSize();

			ccrRead |= ops.getCCRRead();
			const auto ccrW = ops.getCCRWritten();
			const auto ccrO = ccrW & ~ccrRead;	// overwrites are writes that have no reads beforehand, i.e. any previous state is discarded

			ccrWrite |= ccrW;
			ccrOverwrite |= ccrO;
		}

		assert(_rt.getEncodedCycleCount() >= _rt.getEncodedInstructionCount());

		if(isIdlePollLoop(info, _pc, isFastInterrupt, _loopEnds))
		{
			// The last op branched: if back to this block, its next turns repeat this one (DSP::fastForwardPollLoop).
			// Gate as for the NOP loops, here with one turn: the call only runs when a turn fits before the stop and
			// the deadline. The DSP registers stay in host registers across the call; it changes only the counters,
			// which the block keeps in memory.
			const auto noCall = m_asm.newLabel();
			{
				const auto pc = r32(m_dspRegPool.get(PoolReg::DspPC, true, false));
				const RegGP first(*this);
				m_asm.mov(r32(first), asmjit::Imm(_pc));
				m_asm.cmp(pc, r32(first));
				m_asm.jnz(noCall);
			}
			emitFastForwardGate(noCall, _rt.getEncodedCycleCount());
			{
				const FuncArg r0(*this, 0);
				const FuncArg r1(*this, 1);
				const FuncArg r2(*this, 2);
				mem().makeDspPtr(r0);
				m_asm.mov(r32(r1), asmjit::Imm(_rt.getEncodedInstructionCount()));
				m_asm.mov(r32(r2), asmjit::Imm(_rt.getEncodedCycleCount()));
				stack().call(asmjit::func_as_ptr(&callFastForwardPollLoop));
			}
			m_asm.bind(noCall);
		}

		if(inPollCycle)
		{
			// Pass the marker on when it names this block and the block went on in the loop (the fall-through, or the
			// head after the closing branch); clear it otherwise: an exit taken, or a turn that did not come this way.
			const TWord next = pcNext == pollCycle.end ? pollCycle.head : pcNext;
			const auto clear = m_asm.newLabel();
			const auto done = m_asm.newLabel();
			{
				const auto pc = r32(m_dspRegPool.get(PoolReg::DspPC, true, false));
				const RegGP value(*this);
				const RegGP expected(*this);
				mem().mov(r64(value), m_dsp.pollCycleMarker());
				m_asm.mov(r64(expected), asmjit::Imm(pollCycle.marker(_pc)));
				m_asm.cmp(r64(value), r64(expected));
				m_asm.jnz(clear);
				m_asm.mov(r32(expected), asmjit::Imm(next));
				m_asm.cmp(pc, r32(expected));
				m_asm.jnz(clear);
				m_asm.mov(r64(expected), asmjit::Imm(pollCycle.marker(next)));
				mem().mov(m_dsp.pollCycleMarker(), r64(expected));
			}
			m_asm.jmp(done);
			m_asm.bind(clear);
			{
				const RegGP zero(*this);
				m_asm.clr(r64(zero));
				mem().mov(m_dsp.pollCycleMarker(), r64(zero));
			}
			m_asm.bind(done);
		}

		if (info.terminationReason == JitBlockInfo::TerminationReason::PopPC)
			blockFlags |= JitOps::PopPC;

		const auto isLoopStart = info.hasFlag(JitBlockInfo::Flags::IsLoopBodyBegin);
		const auto isLoopEnd = info.terminationReason == JitBlockInfo::TerminationReason::LoopEnd;
		const auto isLoopBody = isLoopStart && isLoopEnd;

		bool childIsConditional = false;

		JitBlockRuntimeData* child = nullptr;
		JitBlockRuntimeData* nonBranchChild = nullptr;

		const auto pcFirst = _rt.getInfo().pc;

		if(_chain)
		{
			if(info.terminationReason == JitBlockInfo::TerminationReason::Branch && !info.hasFlag(JitBlockInfo::Flags::ModeChange))
			{
				// if the last instruction of a JIT block is a branch to an address known at compile time, and this branch is fixed, i.e. is not dependent
				// on a condition: Store that address to be able to call the next JIT block from the current block without having to have a transition to the C++ code

				const auto branchTarget = info.branchTarget;

				if (branchTarget != g_invalidAddress)
				{
					assert(branchTarget == g_dynamicAddress || branchTarget < m_dsp.memory().sizeP());

					const auto pcLast = pcFirst + pMemSize;

					childIsConditional = info.branchIsConditional;

					if (branchTarget == g_dynamicAddress)
					{
						childAddr = g_dynamicAddress;
					}
					// do not branch into ourselves
					else if (branchTarget < pcFirst || branchTarget >= pcLast)
					{
						child = _chain->getChildBlock(&_rt, branchTarget);

						if (child)
						{
							assert(child->getFunc());

							child->addParent(pcFirst);
							childAddr = branchTarget;

							if (childIsConditional)
							{
								nonBranchChild = _chain->getChildBlock(&_rt, pcLast);
								if (nonBranchChild)
								{
									nonBranchChild->addParent(pcFirst);
									_rt.m_nonBranchChild = pcLast;
								}
							}
						}
					}
				}
			}
			else if(!isLoopBody)
			{
				if ((info.terminationReason == JitBlockInfo::TerminationReason::ExistingCode || info.terminationReason == JitBlockInfo::TerminationReason::VolatileP))
				{
					if(!isFastInterrupt && !blockFlags && !isLoopEnd)
					{
						nonBranchChild = _chain->getChildBlock(&_rt, pcNext, false);
						if(nonBranchChild)
						{
							childAddr = pcNext;
							nonBranchChild->addParent(pcFirst);
						}
					}
				}
			}
		}

		m_asm.setCursor(cursorInsertIncreaseInstructionCount);
		increaseInstructionCount(asmjit::Imm(_rt.getEncodedInstructionCount()));
		increaseCycleCount(asmjit::Imm(_rt.getEncodedCycleCount()));
		m_asm.setCursor(m_asm.lastNode());

			auto jumpIfLoop = [&](const asmjit::Label& _ifTrue, const JitReg32& _regPC, const JitReg32& _regLC, const JitReg32& _temp)
		{
			if (!isLoopBody)
				return false;

			const SkipLabel skip(m_asm);
			if(m_config.maxDoIterations)
			{
				assert(asmjit::Support::isPowerOf2(m_config.maxDoIterations));
				m_asm.test_(_regLC, asmjit::Imm(m_config.maxDoIterations-1));
				m_asm.jz(skip);
			}

#ifdef HAVE_ARM64
			RegGP temp(*this, false);
			JitReg32 t;
			if(_temp.isValid())
			{
				t = _temp;
			}
			else
			{
				temp.acquire();
				t = r32(temp);
			}
			m_asm.mov(t, asmjit::Imm(pcFirst));
			m_asm.cmp(_regPC, t);
#else
			m_asm.cmp(_regPC, asmjit::Imm(pcFirst));
#endif
			m_asm.jz(_ifTrue);
			return true;
		};

		auto profileBegin = [&](const std::string& _name)
		{
			if(!_profilingSupport)
				return asmjit::Label();

			const auto labelBegin = m_asm.newLabel();
			const auto labelEnd = m_asm.newLabel();

			m_asm.bind(labelBegin);

			const JitBlockRuntimeData::InstructionProfilingInfo pi{ g_invalidAddress, 0, 0, 0, 1, labelBegin, labelEnd, 0, 0, _name};
			profilingInfo.emplace_back(pi);

			return labelEnd;
		};

		auto profileEnd = [&](const asmjit::Label& l)
		{
			if(l.isValid())
				m_asm.bind(l);
		};

		if(isLoopEnd)
		{
			auto pl = profileBegin("loop");

			const auto skip = m_asm.newLabel();
			const auto enddo = m_asm.newLabel();

			JitOps ops(*this, _rt, fastInterruptMode);

			// It is important that this code does not allocate any temp registers inside the branches. thefore, we prewarm everything
			RegGP temp(*this);

			const auto& sr = r32(m_dspRegPool.get(PoolReg::DspSR, true, true));
			                 r32(m_dspRegPool.get(PoolReg::DspLA, true, true));	// we don't use it here but do_end does
			const auto& lc = r32(m_dspRegPool.get(PoolReg::DspLC, true, true));

			m_dspRegPool.lock(PoolReg::DspSR);
			m_dspRegPool.lock(PoolReg::DspLA);
			m_dspRegPool.lock(PoolReg::DspLC);

			DSPReg pc(*this, PoolReg::DspPC, true, true);

			// check loop flag

			m_asm.bitTest(sr, SRB_LF);
			m_asm.jz(skip);

			m_asm.cmp(lc, asmjit::Imm(1));
			m_asm.jle(enddo);
			m_asm.dec(lc);

			if(isLoopBody)
			{
				m_asm.mov(r32(pc), asmjit::Imm(pcFirst));
			}
			else
			{
				const auto ss = r64(pc);
				m_dspRegs.getSS(ss);				// note: not calling getSSH as it will dec the SP
				m_asm.shr(ss, asmjit::Imm(24));
			}
			m_asm.jmp(skip);

			m_asm.bind(enddo);
			ops.do_end(temp);

			m_asm.bind(skip);

			m_dspRegPool.unlock(PoolReg::DspSR);
			m_dspRegPool.unlock(PoolReg::DspLA);
			m_dspRegPool.unlock(PoolReg::DspLC);

			profileEnd(pl);
		}

		auto ccrDirty = m_dspRegs.ccrDirtyFlags();

		if(ccrDirty)
		{
			// we can omit CCR updates for all CCR bits that are overwritten by child blocks
			uint32_t ccrOverwritten = 0;
			if(child)
			{
				ccrOverwritten = child->getInfo().ccrOverwrite;
				if(nonBranchChild)
					ccrOverwritten &= nonBranchChild->getInfo().ccrOverwrite;
			}
			else if(nonBranchChild)
			{
				ccrOverwritten = nonBranchChild->getInfo().ccrOverwrite;
			}

			ccrDirty = static_cast<CCRMask>(ccrDirty & ~ccrOverwritten);

			if(ccrDirty)
			{
				auto pl = profileBegin("ccrUpdate");

				asmjit::Label skipCCRupdate = m_asm.newLabel();

				// We skip to update dirty CCRs if we are running a loop and the loop has not yet ended.
				// Be sure to not skip CCR updates if the loop itself reads the SR before it has written to it
				const auto writesSRbeforeRead = info.hasFlag(JitBlockInfo::Flags::WritesSRbeforeRead);
				const auto readsSR = (info.readRegs & RegisterMask::SR) != RegisterMask::None;
				if(isLoopBody && (writesSRbeforeRead || !readsSR))
				{
					jumpIfLoop(skipCCRupdate, 
						r32(m_dspRegPool.get(PoolReg::DspPC, true, false)), 
						r32(m_dspRegPool.get(PoolReg::DspLC, true, false)), 
						JitReg32()
						);
				}

				JitOps op(*this, _rt, fastInterruptMode);

				op.updateDirtyCCR(ccrDirty);

				m_asm.bind(skipCCRupdate);

				profileEnd(pl);
			}
		}

		auto pl = profileBegin("release");

		RegScratch scratch(*this, false);

		JitReg32 regPC;

		if((child && childIsConditional) || isLoopBody)
		{
			regPC = r32(m_dspRegPool.get(PoolReg::DspPC, true, false));

			// we can keep our PC reg if its volatile. If It's not, it will be destroyed on stack.popAll() below => we need to copy it to a safe place
			// Also, we need to make sure that our PC reg is not the first function argument because we replace it with the Jit*
			if(JitStackHelper::blockMustPreserve(regPC) || r64(regPC) == g_funcArgGPs[0])
			{
				scratch.acquire();
				asm_().mov(r32(scratch), regPC);
				regPC = r32(scratch);
			}
		}

		JitReg32 regLC;
		RegGP tempLC(*this, false);

		if(isLoopBody && m_config.maxDoIterations)
		{
			regLC = r32(m_dspRegPool.get(PoolReg::DspLC, true, false));

			// Basically the same rules that we use for the PC reg above apply to LC too:
			// we can keep our LC reg if its volatile. If It's not, it will be destroyed on stack.popAll() below => we need to copy it to a safe place
			// Also, we need to make sure that our LC reg is not the first function argument because we replace it with the Jit*
			if(JitStackHelper::blockMustPreserve(regLC) || r64(regLC) == g_funcArgGPs[0])
			{
				std::vector<RegGP> temps;

				// acquire a temp that is volatile
				while(!m_gpPool.empty())
				{
					temps.emplace_back(*this);
					if(JitStackHelper::blockMustPreserve(temps.back()))
						continue;

					tempLC = std::move(temps.back());
					temps.clear();
					break;
				}

				assert(tempLC.isValid());
				assert(!JitStackHelper::blockMustPreserve(tempLC));

				asm_().mov(r32(tempLC), regLC);
				regLC = r32(tempLC);
			}
		}

		// The PC in memory is only ever observed after returning to C++. If every exit of this block transfers to a
		// child, and those children leave a correct PC behind, our own store is dead and can be dropped.
		// The conditional-child-without-nonBranchChild case falls through to a ret() below, so it does not qualify.
		// Children are always fully generated blocks (circular references yield nullptr), so this induction terminates,
		// and a parent is invalidated whenever a child is, so the flag cannot go stale.
		const auto exitsOnlyToChildren = child ? (!childIsConditional || nonBranchChild != nullptr) : (nonBranchChild != nullptr);

		const auto childrenEstablishPc = exitsOnlyToChildren
			&& (!child || child->establishesPc())
			&& (!nonBranchChild || nonBranchChild->establishesPc());

		const auto pcWritten = m_dspRegPool.isWritten(PoolReg::DspPC);

		if(pcWritten && childrenEstablishPc)
			m_dspRegPool.discardWritten(PoolReg::DspPC);

		_rt.setEstablishesPc(pcWritten || childrenEstablishPc);

		m_dspRegPool.releaseAll();

		pm.end();

		jumpIfLoop(loopBegin, regPC, regLC, regLC);

		m_stack.popAll();

		m_stack.reset();

		profileEnd(pl);

		asmjit::Label lj;

		if(child || nonBranchChild)
		{
			lj = profileBegin("jump");
		}

		if (child)
		{
			if (childIsConditional)
			{
				// we need to check if the PC has been set to the target address
#ifdef HAVE_ARM64
				auto regTempImm = r32((regPC == r32(g_funcArgGPs[1])) ? g_funcArgGPs[2] : g_funcArgGPs[1]);
				m_asm.mov(regTempImm, asmjit::Imm(childAddr));
				m_asm.cmp(regPC, regTempImm);
#else
				m_asm.cmp(regPC, asmjit::Imm(childAddr));
#endif

				if (nonBranchChild)
				{
					jumpToOneOf(JitCondCode::kZero, child, nonBranchChild);
				}
				else
				{
					jumpToChild(child, JitCondCode::kZero);
					m_asm.ret();
				}
			}
			else
			{
				jumpToChild(child);
			}
		}
		else if(nonBranchChild)
		{
			jumpToChild(nonBranchChild);
		}
		else
		{
			m_asm.ret();
		}

		profileEnd(lj);

		m_currentJitBlockRuntimeData = nullptr;
		return true;
	}

	void JitBlock::setNextPC(const DspValue& _pc)
	{
		m_dspRegPool.write(PoolReg::DspPC, _pc);
	}

	void JitBlock::increaseInstructionCount(const asmjit::Operand& _count)
	{
		increaseUint64(_count, m_dsp.getInstructionCounter());
	}

	void JitBlock::increaseCycleCount(const asmjit::Operand& _count)
	{
		increaseUint64(_count, m_dsp.getCycles());
	}

	void JitBlock::increaseUint64(const asmjit::Operand& _count, const uint64_t& _target)
	{
		const auto ptr = dspRegPool().makeDspPtr(&_target, sizeof(uint64_t));

#ifdef HAVE_ARM64
		const RegScratch scratch(*this);
		const auto r = r64(scratch);
		m_asm.ldr(r, ptr);
		if (_count.isImm())
		{
			// for rep loops, it is possible that the immediate exceeds 12 bits
			const auto& imm = _count.as<asmjit::Imm>();
			if(imm.value() > 0xfff)
			{
				assert(imm.value() <= 0xffffff);
				m_asm.add(r, r, asmjit::Imm(imm.value() & 0x000fff));
				m_asm.add(r, r, asmjit::Imm(imm.value() & 0xfff000));
			}
			else
			{
				m_asm.add(r, r, imm);
			}
		}
		else
		{
			m_asm.add(r, r, r64(_count.as<JitRegGP>()));
		}
		m_asm.str(r, ptr);
#else
		if(_count.isImm())
		{
			m_asm.add(ptr, _count.as<asmjit::Imm>());
		}
		else
		{
			m_asm.add(ptr, r64(_count.as<JitRegGP>()));
		}
#endif
	}

	AddressingMode JitBlock::getAddressingMode(const uint32_t _aguIndex) const
	{
		const auto* mode = getMode();
		return mode ? mode->getAddressingMode(_aguIndex) : AddressingMode::Unknown;
	}

	const JitDspMode* JitBlock::getMode() const
	{
		return m_chain ? &m_chain->getMode() : m_mode;
	}

	void JitBlock::setMode(JitDspMode* _mode)
	{
		m_mode = _mode;
	}

	void JitBlock::reset(JitConfig&& _config)
	{
		m_config = std::move(_config);
		m_stack.reset();
		m_xmmPool.reset({regXMMTempA});
		m_gpPool.reset(g_regGPTemps);
		m_dspRegs.reset();
		m_dspRegPool.reset();
		m_scratchLocked = false;
		m_shiftLocked = false;
	}

	JitBlock::JitBlockGenerating::JitBlockGenerating(JitBlockRuntimeData& _block): m_block(_block)
	{
		_block.setGenerating(true);
	}

	JitBlock::JitBlockGenerating::~JitBlockGenerating()
	{
		m_block.setGenerating(false);
	}

	JitReg64 JitBlock::getJumpTarget(const JitReg64& _dst, const JitBlockRuntimeData* _child) const
	{
		auto* p = asmjit::func_as_ptr(_child->getFunc());
		const auto addr = reinterpret_cast<uint64_t>(p);

		if(const auto offset = Jitmem::pointerOffset(p, &m_dsp.regs()))
			m_asm.lea_(_dst, regDspPtr, offset);
		else
			m_asm.mov(_dst, asmjit::Imm(addr));

		return _dst;
	}

	void JitBlock::jumpToChild(const JitBlockRuntimeData* _child, const JitCondCode _cc/* = JitCondCode::kMaxValue*/) const
	{
		const auto tempReg = r64(g_funcArgGPs[1]);

		auto initTemp = [this, &tempReg, &_child]
		{
			return getJumpTarget(tempReg, _child);
		};

		auto temp = initTemp();

		if(_cc == JitCondCode::kMaxValue)
		{
#ifdef HAVE_ARM64
			m_asm.br(temp);
#else
			m_asm.jmp(temp);
#endif
		}
		else
		{
			// neither x86 nor ARM have a conditional jump to register
			const auto l = m_asm.newLabel();
			const auto cc = JitOps::reverseCC(_cc);

#ifdef HAVE_ARM64
			m_asm.b(cc, l);
			m_asm.br(temp);
#else
			m_asm.j(cc, l);
			m_asm.jmp(temp);
#endif
			m_asm.bind(l);
		}
	}

	void JitBlock::jumpToOneOf(const JitCondCode _ccTrue, const JitBlockRuntimeData* _childTrue, const JitBlockRuntimeData* _childFalse) const
	{
		auto regTrue = getJumpTarget(r64(g_funcArgGPs[1]), _childTrue);
		auto regFalse = getJumpTarget(r64(g_funcArgGPs[2]), _childFalse);

#ifdef HAVE_ARM64
		m_asm.csel(regFalse, regTrue, regFalse, _ccTrue);
		m_asm.br(regFalse);
#else
		m_asm.cmov(_ccTrue, regFalse, regTrue);
		m_asm.jmp(regFalse);
#endif
	}
}
