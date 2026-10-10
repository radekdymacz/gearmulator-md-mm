// Firmware-free accumulator representation regressions.
// Expected semantics: DSP56300 Family Manual, Rev. 5, sections 5.4 and 13.
#include "dsp56kEmu/assembler.h"
#include "dsp56kEmu/dsp.h"
#include "dsp56kBase/logging.h"

#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	using namespace dsp56k;

	struct Machine
	{
		DefaultMemoryValidator validator;
		Memory mem{validator, 0x1000};
		PeripheralsNop px, py;
		DSP dsp{mem, &px, &py};

		Machine()
		{
			auto config = dsp.getJit().getConfig();
			config.maxInstructionsPerBlock = 1;
			config.linkJitBlocks = false;
			dsp.getJit().setConfig(config);
		}
	};

	int runDifferentialTests()
	{
		Assembler assembler;
		std::vector<std::string> instructions;
		for(const auto* accumulator : {"a", "b"})
		{
			for(const auto* field : {"0", "1", "2"})
			{
				const auto reg = std::string(accumulator) + field;
				instructions.push_back("move " + reg + ",x:<$10");
				instructions.push_back("move x:<$10," + reg);
				instructions.push_back("move " + reg + ",x0");
				instructions.push_back("move x0," + reg);
			}
			for(const auto* suffix : {"", "10"})
			{
				const auto reg = std::string(accumulator) + suffix;
				for(const auto* address : {"l:<$10", "l:(r0)"})
				{
					instructions.push_back("move " + reg + "," + address);
					instructions.push_back(std::string("move ") + address + "," + reg);
				}
			}
			for(const auto* op :
				{"abs", "neg", "not", "inc", "dec", "rnd", "asl", "asr", "lsl", "lsr", "rol", "ror", "clr", "tst"})
				instructions.push_back(std::string(op) + " " + accumulator);
			for(const auto* op : {"add", "sub", "and", "or", "eor", "cmp", "cmpm"})
				instructions.push_back(std::string(op) + " x0," + accumulator);
			for(const auto shift : {0, 1, 8, 23, 24, 31, 47, 55})
			{
				std::ostringstream hex;
				hex << std::hex << shift;
				for(const auto* op : {"asl", "asr"})
					instructions.push_back(std::string(op) + " #$" + hex.str() + "," + accumulator + "," + accumulator);
			}
			instructions.push_back(std::string("asl x0,") + accumulator + "," + accumulator);
			instructions.push_back(std::string("asr x0,") + accumulator + "," + accumulator);
		}
		instructions.push_back("move a,b");
		instructions.push_back("move b,a");
		const uint64_t values[] = {0, 1, 0xffffff, 0x1000000, 0x7fffffffffff, 0x800000000000, 0xffffffffffffff,
			0x7fffffffffffff, 0x80000000000000, 0x55123456abcdef, 0xff800000000000, 0xff7fffffffffff};
		unsigned total = 0, failed = 0;
		for(const auto& instruction : instructions)
		{
			const auto op = assembler.assemble(instruction.c_str());
			if(!op.success())
				throw std::string("Assembly failed: ") + instruction;
			unsigned failuresForInstruction = 0;
			std::mt19937_64 random(0x56300);
			for(unsigned seed = 0; seed < 64; ++seed)
			{
				auto interpreter = std::make_unique<Machine>();
				auto jit = std::make_unique<Machine>();
				const uint64_t a = seed < 12 ? values[seed] : random() & 0xffffffffffffff;
				const uint64_t b = seed < 12 ? values[11 - seed] : random() & 0xffffffffffffff;
				uint64_t x = random() & 0xffffffffffff;
				const uint64_t y = random() & 0xffffffffffff;
				if(instruction.find("x0,a,a") != std::string::npos || instruction.find("x0,b,b") != std::string::npos)
					x = (x & 0xffffff000000ull) | (seed % 56);
				const uint32_t memoryX = random() & 0xffffff, memoryY = random() & 0xffffff;
				const uint32_t status = ((seed % 3) == 1 ? SR_S0 : ((seed % 3) == 2 ? SR_S1 : 0)) | (seed & 0xff);
				for(auto* machine : {interpreter.get(), jit.get()})
				{
					auto& regs = machine->dsp.regs();
					regs.a.var = static_cast<int64_t>(a << 8);
					regs.b.var = static_cast<int64_t>(b << 8);
					regs.x.var = x;
					regs.y.var = y;
					regs.sr.var = status;
					regs.r[0].var = 0x10;
					machine->mem.set(MemArea_X, 0x10, memoryX);
					machine->mem.set(MemArea_Y, 0x10, memoryY);
					machine->mem.set(MemArea_P, 0x100, op.word[0]);
					machine->mem.set(MemArea_P, 0x101, op.wordCount > 1 ? op.word[1] : 0);
					machine->dsp.setPC(0x100);
					// Direct register initialization must also synchronize the JIT's mode.
					machine->dsp.getJit().checkModeChange();
				}
				interpreter->dsp.execInterpreter();
				for(unsigned attempt = 0; attempt < 4 && jit->dsp.getPC() == 0x100; ++attempt)
					jit->dsp.execJit();
				const auto& p = interpreter->dsp.regs();
				const auto& q = jit->dsp.regs();
				const auto ps = interpreter->dsp.getSR().var, qs = jit->dsp.getSR().var;
				bool ok = p.a.var == q.a.var && p.b.var == q.b.var && p.x.var == q.x.var && p.y.var == q.y.var
					&& ps == qs && p.pc.var == q.pc.var;
				for(unsigned i = 0; i < 8; ++i)
					ok &= p.r[i].var == q.r[i].var && p.n[i].var == q.n[i].var && p.m[i].var == q.m[i].var;
				for(const auto area : {MemArea_X, MemArea_Y})
					ok &= interpreter->mem.get(area, 0x10) == jit->mem.get(area, 0x10);
				++total;
				if(!ok)
				{
					++failuresForInstruction;
					++failed;
					if(failuresForInstruction <= 2)
						std::cerr << "FAIL " << instruction << " seed=" << std::dec << seed << " initA=" << std::hex
								  << a << " initB=" << b << " initSR=" << status << " A=" << p.a.var << "/" << q.a.var
								  << " B=" << p.b.var << "/" << q.b.var << " SR=" << ps << "/" << qs << '\n';
				}
			}
		}
		std::cerr << "Differential cases: " << std::dec << total << " failures: " << failed << '\n';
		return failed ? 1 : 0;
	}

	// These expectations do not use either execution engine as an oracle. The
	// differential sweep supplements them; it cannot detect a shared mistake.
	int runExpectedTests()
	{
		struct Case
		{
			const char* instruction;
			uint64_t input;
			uint32_t initialSR;
			uint64_t output;
			uint32_t flagMask;
			uint32_t flags;
		};
		const Case cases[] = {
			{"asr a", 1, 0, 0, CCR_C, CCR_C},
			{"asl a", 0x00800000000000, 0, 0x01000000000000, CCR_C, 0},
			{"ror a", 0, CCR_C, 0x00800000000000, CCR_N | CCR_Z | CCR_C, CCR_N},
			{"ror a", 0xff000000000000, 0, 0xff000000000000, CCR_N | CCR_Z | CCR_C, CCR_Z},
			{"rol a", 0x00800000000000, 0, 0, CCR_N | CCR_Z | CCR_C, CCR_Z | CCR_C},
			{"lsl a", 0x007fffffffffff, CCR_Z, 0x00fffffeffffff, CCR_Z, 0},
			{"lsr a", 0x00000002000000, CCR_Z, 0x00000001000000, CCR_Z, 0},
			{"abs a", 0x80000000000000, CCR_C, 0x80000000000000, CCR_V | CCR_L | CCR_C, CCR_V | CCR_L | CCR_C},
			{"neg a", 0x80000000000000, 0, 0x80000000000000, CCR_V | CCR_L, CCR_V | CCR_L},
			{"neg a", 1, CCR_V, 0xffffffffffffff, CCR_V | CCR_L, 0},
			{"inc a", 0x7fffffffffffff, 0, 0x80000000000000, CCR_V | CCR_L | CCR_C, CCR_V | CCR_L},
			{"inc a", 0xffffffffffffff, 0, 0, CCR_V | CCR_L | CCR_C, CCR_C},
			{"inc a", 0x007fffffffffff, 0, 0x00800000000000, CCR_V | CCR_L | CCR_C, 0},
			{"dec a", 0x80000000000000, 0, 0x7fffffffffffff, CCR_V | CCR_L | CCR_C, CCR_V | CCR_L},
			{"dec a", 0, 0, 0xffffffffffffff, CCR_V | CCR_L | CCR_C, CCR_C},
			{"dec a", 0x00800000000000, 0, 0x007fffffffffff, CCR_V | CCR_L | CCR_C, 0},
			{"tst a", 0x80000000000000, SR_S1, 0x80000000000000, CCR_E, CCR_E},
		};
		Assembler assembler;
		unsigned failures = 0;
		const auto execute = [&](Machine& _machine, const char* _instruction, bool _jit)
		{
			const auto op = assembler.assemble(_instruction);
			if(!op.success())
				throw std::string("Assembly failed: ") + _instruction;
			_machine.mem.set(MemArea_P, 0x100, op.word[0]);
			_machine.mem.set(MemArea_P, 0x101, op.wordCount > 1 ? op.word[1] : 0);
			_machine.dsp.setPC(0x100);
			_machine.dsp.getJit().checkModeChange();
			if(_jit)
			{
				// The first dispatch may only grow the JIT dispatch table.
				for(unsigned attempt = 0; attempt < 4 && _machine.dsp.getPC() == 0x100; ++attempt)
					_machine.dsp.execJit();
			}
			else
				_machine.dsp.execInterpreter();
			if(_machine.dsp.getPC() != 0x100 + op.wordCount)
				throw std::string("Instruction did not complete: ") + _instruction;
		};
		for(const auto& test : cases)
			for(bool jit : {false, true})
			{
				auto machine = std::make_unique<Machine>();
				machine->dsp.regs().a.var = static_cast<int64_t>(test.input << 8);
				machine->dsp.regs().sr.var = test.initialSR;
				execute(*machine, test.instruction, jit);
				const auto result = static_cast<uint64_t>(machine->dsp.regs().a.var) >> 8;
				const auto flags = machine->dsp.getSR().var & test.flagMask;
				if(result != test.output || flags != test.flags)
				{
					++failures;
					std::cerr << "Expected-value failure " << (jit ? "JIT " : "interpreter ") << test.instruction
							  << " result=" << std::hex << result << " expected=" << test.output << " flags=" << flags
							  << " expected=" << test.flags << '\n';
				}
			}
		for(const auto* accumulator : {"a10", "b10"})
			for(const auto* address : {"l:<$10", "l:(r0)"})
				for(bool load : {false, true})
					for(bool jit : {false, true})
					{
						auto machine = std::make_unique<Machine>();
						constexpr uint64_t expected = 0x55123456abcdef00;
						const auto initial = load ? uint64_t(0x5577777788888800) : expected;
						machine->dsp.regs().a.var = static_cast<int64_t>(initial);
						machine->dsp.regs().b.var = static_cast<int64_t>(initial);
						machine->dsp.regs().r[0].var = 0x10;
						machine->mem.set(MemArea_X, 0x10, load ? 0x123456 : 0);
						machine->mem.set(MemArea_Y, 0x10, load ? 0xabcdef : 0);
						const std::string instruction = std::string("move ") + (load ? address : accumulator) + ","
							+ (load ? accumulator : address);
						execute(*machine, instruction.c_str(), jit);
						const auto result = static_cast<uint64_t>(
							accumulator[0] == 'a' ? machine->dsp.regs().a.var : machine->dsp.regs().b.var);
						const bool ok = load ? result == expected
											 : machine->mem.get(MemArea_X, 0x10) == 0x123456
								&& machine->mem.get(MemArea_Y, 0x10) == 0xabcdef;
						if(!ok)
						{
							++failures;
							std::cerr << "Raw-move failure " << (jit ? "JIT " : "interpreter ") << instruction << '\n';
						}
					}
		for(uint64_t input : {uint64_t(0x7fffffffffffff), uint64_t(0x80000000000000)})
			for(bool jit : {false, true})
			{
				auto machine = std::make_unique<Machine>();
				machine->dsp.regs().a.var = static_cast<int64_t>(input << 8);
				machine->dsp.regs().sr.var = SR_S1;
				execute(*machine, "move a,l:<$10", jit);
				const auto x = machine->mem.get(MemArea_X, 0x10), y = machine->mem.get(MemArea_Y, 0x10);
				const bool positive = input == 0x7fffffffffffff;
				if(x != (positive ? 0x7fffff : 0x800000) || y != (positive ? 0xffffff : 0)
					|| !(machine->dsp.getSR().var & CCR_L))
				{
					++failures;
					std::cerr << "Scale-up saturation failure " << (jit ? "JIT" : "interpreter") << '\n';
				}
			}
		std::cerr << "Expected-value failures: " << std::dec << failures << '\n';
		return failures ? 1 : 0;
	}

	// Unlike the single-instruction sweep, these programs keep values and lazy CCR
	// state live across instructions, branches, parallel moves and linked blocks.
	int runSequenceTests()
	{
		Assembler assembler;
		unsigned total = 0, failures = 0;
		const auto emit = [&](Machine& _machine, TWord& _pc, const std::string& _instruction)
		{
			const auto op = assembler.assemble(_instruction.c_str());
			if(!op.success())
				throw std::string("Sequence assembly failed: ") + _instruction;
			for(unsigned i = 0; i < op.wordCount; ++i)
				_machine.mem.set(MemArea_P, _pc++, op.word[i]);
		};
		const auto execute = [](Machine& _machine, bool _jit)
		{
			_machine.dsp.setPC(0x100);
			_machine.dsp.getJit().checkModeChange();
			for(unsigned dispatch = 0; dispatch < 256 && _machine.dsp.getPC() != 0x300; ++dispatch)
				if(_jit)
					_machine.dsp.execJit();
				else
					_machine.dsp.execInterpreter();
			if(_machine.dsp.getPC() != 0x300)
			{
				std::cerr << "Sequence exit PC=" << std::hex << _machine.dsp.getPC().var << " jit=" << _jit << '\n';
				throw std::string("Sequence failed to reach its exit");
			}
		};
		struct BranchCase
		{
			std::vector<std::string> body;
			uint64_t a;
			uint32_t sr;
			const char* branch;
			bool taken;
		};
		const BranchCase branches[] = {
			{{"asr a"}, 1, 0, "jcs", true},
			{{"asr #0,a,a"}, 1, CCR_C, "jcs", false},
			{{"asl a"}, 0x80000000000000, 0, "jcs", true},
			{{"asl a", "tst b"}, 0x40000000000000, 0, "jls", true},
			{{"ror a"}, 0, CCR_C, "jmi", true},
			{{"ror a"}, 0xff000000000000, 0, "jeq", true},
			{{"rol a"}, 0x800000000000, 0, "jeq", true},
			{{"rol a"}, 0x400000000000, 0, "jmi", true},
			{{"lsl a"}, 0x2000000, CCR_Z, "jeq", false},
			{{"lsr a"}, 0x2000000, CCR_Z, "jeq", false},
			{{"tst a", "lsl a"}, 0x00400000000000, 0, "jmi", true},
			{{"tst a", "lsr a"}, 0xff800000000000, 0, "jmi", false},
			{{"tst a", "not a"}, 0, 0, "jmi", true},
			{{"tst a", "and x0,a"}, 0xff800000000000, 0, "jmi", false},
			{{"tst a", "or x0,a"}, 0x00800000000000, 0, "jmi", true},
			{{"tst a", "eor x0,a"}, 0x00800000000000, 0, "jmi", true},
			{{"tst a", "clr b"}, 0x80000000000000, 0, "jmi", false},
			{{"tst a", "clr b"}, 0x80000000000000, 0, "jes", false},
			{{"abs a"}, 0x80000000000000, 0, "jls", true},
			{{"neg a"}, 0x80000000000000, 0, "jls", true},
			{{"inc a"}, 0x7fffffffffffff, 0, "jls", true},
			{{"dec a"}, 0x80000000000000, 0, "jls", true},
			{{"inc a"}, 0xffffffffffffff, 0, "jcs", true},
			{{"dec a"}, 0, 0, "jcs", true},
			{{"neg a", "neg a"}, 1, CCR_L | CCR_V, "jls", true},
			{{"tst a"}, 0x80000000000000, SR_S1, "jes", true},
		};
		for(bool optimize : {false, true})
			for(const auto& test : branches)
				for(bool jit : {false, true})
				{
					auto machine = std::make_unique<Machine>();
					auto config = machine->dsp.getJit().getConfig();
					config.maxInstructionsPerBlock = 32;
					config.linkJitBlocks = true;
					config.enableOptimizer = optimize;
					machine->dsp.getJit().setConfig(config);
					machine->dsp.regs().a.var = static_cast<int64_t>(test.a << 8);
					machine->dsp.regs().sr.var = test.sr;
					TWord pc = 0x100;
					for(const auto& instruction : test.body)
						emit(*machine, pc, instruction);
					emit(*machine, pc, std::string(test.branch) + " $200");
					emit(*machine, pc, "move #>1,x1");
					emit(*machine, pc, "jmp $300");
					pc = 0x200;
					emit(*machine, pc, "move #>2,x1");
					emit(*machine, pc, "jmp $300");
					pc = 0x300;
					emit(*machine, pc, "jmp $300");
					execute(*machine, jit);
					++total;
					if(machine->dsp.x1().var != (test.taken ? 2u : 1u))
					{
						++failures;
						std::cerr << "Branch failure " << test.body.front() << " / " << test.branch << " jit=" << jit
								  << " optimizer=" << optimize << '\n';
						for(const auto& instruction : test.body)
							std::cerr << "  " << instruction << '\n';
					}
				}
		const std::vector<std::string> operations = {"abs a", "neg b", "inc a", "dec b", "asl a", "asr b", "rol a",
			"ror b", "lsl a", "lsr b", "add x0,a", "sub y0,b", "tst a", "tst b", "move a10,l:<$10", "move l:<$10,b10",
			"move b10,l:<$11", "move l:<$11,a10", "move a,l:<$12", "move l:<$12,b", "move b,l:<$13", "move l:<$13,a",
			"abs a a10,l:<$10", "neg b b10,l:<$11", "asr a b10,l:<$12", "asl #8,a,b", "asr #23,b,a", "move a1,x0",
			"move b1,y0"};
		for(bool optimize : {false, true})
			for(unsigned seed = 0; seed < 192; ++seed)
			{
				std::mt19937_64 random(0x56300000 + seed);
				auto interpreter = std::make_unique<Machine>();
				auto jit = std::make_unique<Machine>();
				const uint64_t a = random(), b = random(), x = random(), y = random();
				std::vector<std::string> program;
				for(unsigned i = 0; i < 24; ++i)
					program.push_back(operations[random() % operations.size()]);
				for(auto* machine : {interpreter.get(), jit.get()})
				{
					auto config = machine->dsp.getJit().getConfig();
					config.maxInstructionsPerBlock = 32;
					config.linkJitBlocks = true;
					config.enableOptimizer = optimize;
					machine->dsp.getJit().setConfig(config);
					auto& regs = machine->dsp.regs();
					regs.a.var = static_cast<int64_t>(a & ~uint64_t(255));
					regs.b.var = static_cast<int64_t>(b & ~uint64_t(255));
					regs.x.var = x & 0xffffffffffff;
					regs.y.var = y & 0xffffffffffff;
					regs.sr.var = (seed % 3 == 1 ? SR_S0 : seed % 3 == 2 ? SR_S1 : 0) | (seed & 255);
					TWord pc = 0x100;
					for(const auto& instruction : program)
						emit(*machine, pc, instruction);
					emit(*machine, pc, "jmp $300");
					pc = 0x300;
					emit(*machine, pc, "jmp $300");
				}
				execute(*interpreter, false);
				execute(*jit, true);
				const auto& p = interpreter->dsp.regs();
				const auto& q = jit->dsp.regs();
				bool ok = p.a.var == q.a.var && p.b.var == q.b.var && p.x.var == q.x.var && p.y.var == q.y.var
					&& interpreter->dsp.getSR().var == jit->dsp.getSR().var;
				for(unsigned i = 0; i < 8; ++i)
					ok &= p.r[i].var == q.r[i].var && p.n[i].var == q.n[i].var && p.m[i].var == q.m[i].var;
				for(const auto area : {MemArea_X, MemArea_Y})
					for(TWord address = 0x10; address <= 0x13; ++address)
						ok &= interpreter->mem.get(area, address) == jit->mem.get(area, address);
				++total;
				if(!ok)
				{
					++failures;
					std::cerr << "Sequence failure seed=" << std::dec << seed << " optimizer=" << optimize << '\n';
					std::cerr << std::hex << " A=" << p.a.var << '/' << q.a.var << " B=" << p.b.var << '/' << q.b.var
							  << " X=" << p.x.var << '/' << q.x.var << " Y=" << p.y.var << '/' << q.y.var
							  << " SR=" << interpreter->dsp.getSR().var << '/' << jit->dsp.getSR().var << '\n';
					for(const auto& instruction : program)
						std::cerr << "  " << instruction << '\n';
				}
			}
		std::cerr << "Sequence cases: " << std::dec << total << " failures: " << failures << '\n';
		return failures ? 1 : 0;
	}

	// DIV preserves E/U/N/Z and derives V from the left shift, before the
	// add/subtract. Include the captured firmware dividend/divisor.
	int runDivideFlagTests()
	{
		constexpr uint64_t mask56 = 0x00ffffffffffffffULL;
		struct Input
		{
			uint64_t dividend;
			uint32_t divisor;
			uint32_t sr;
			unsigned iterations = 24;
		};
		const Input inputs[] = {
			{0x0821d0000000ULL, 0x40d249, 0x8050},
			{0x0821d0000000ULL, 0x40d249, 0x8052},
			{0x100000000000ULL, 0x400000, CCR_C},
			{0x100000000000ULL, 0xc00000, CCR_E | CCR_U | CCR_N | CCR_Z},
			{1, 0x7fffff, CCR_V},
			{1, 0x800000, CCR_L | CCR_V},
			// A single shift can overflow outside the valid fractional-quotient
			// range; assert its specified V/L behavior without claiming a quotient.
			{0x40000000000000ULL, 0x400000, CCR_N, 1},
			{0x80000000000000ULL, 0x400000, 0, 1},
		};
		unsigned total = 0, failures = 0;
		Assembler assembler;
		for(const auto& input : inputs)
			for(bool useB : {false, true})
				for(bool optimize : {false, true})
					for(bool jit : {false, true})
					{
						auto machine = std::make_unique<Machine>();
						auto config = machine->dsp.getJit().getConfig();
						config.enableOptimizer = optimize;
						machine->dsp.getJit().setConfig(config);
						auto& r = machine->dsp.regs();
						(useB ? r.b : r.a).var = static_cast<int64_t>(input.dividend << 8);
						r.x.var = input.divisor;
						r.sr.var = input.sr;
						const auto op = assembler.assemble(useB ? "div x0,b" : "div x0,a");
						if(!op.success())
							throw std::string("DIV assembly failed");
						for(unsigned step = 0; step < input.iterations; ++step)
							machine->mem.set(MemArea_P, 0x100 + step, op.word[0]);
						machine->dsp.setPC(0x100);
						machine->dsp.getJit().checkModeChange();
						uint64_t expected = input.dividend;
						uint32_t status = input.sr;
						const uint64_t divisor =
							(uint64_t(input.divisor) << 24) | ((input.divisor & 0x800000) ? 0xff000000000000ULL : 0);
						for(unsigned step = 0; step < input.iterations; ++step)
						{
							const bool overflow = ((expected >> 55) ^ (expected >> 54)) & 1;
							const bool add = ((expected >> 55) ^ (input.divisor >> 23)) & 1;
							expected = ((expected << 1) | ((status & CCR_C) != 0)) & mask56;
							expected = (add ? expected + divisor : expected - divisor) & mask56;
							status = (status & ~(CCR_V | CCR_C)) | (overflow ? CCR_V | CCR_L : 0)
								| ((expected >> 55) ? 0 : CCR_C);
							if(jit)
								for(unsigned attempt = 0; attempt < 4 && machine->dsp.getPC() == 0x100 + step;
									++attempt)
									machine->dsp.execJit();
							else
								machine->dsp.execInterpreter();
							const auto actual = uint64_t((useB ? r.b : r.a).var) >> 8;
							const auto sr = machine->dsp.getSR().var;
							++total;
							if(actual != expected || sr != status || machine->dsp.getPC() != 0x101 + step)
							{
								if(++failures <= 8)
									std::cerr << "DIV flag failure jit=" << jit << " optimizer=" << optimize
											  << " step=" << step << " actual=" << std::hex << actual << '/' << sr
											  << " expected=" << expected << '/' << status << std::dec << '\n';
							}
						}
					}
		std::cerr << "DIV expected-value cases: " << total << " failures: " << failures << '\n';
		return failures ? 1 : 0;
	}

} // namespace

int runManualAccumulatorTests(bool _interpreterOnly);

int main(int _argc, char** _argv)
{
	// Keep generated assembly out of CI logs; failures are reported on stderr.
	Logging::setLogFunc([](const std::string&) {});
	try
	{
		if(_argc == 2 && std::string(_argv[1]) == "--manual-interpreter-only")
			return runManualAccumulatorTests(true);
		if(_argc == 2 && std::string(_argv[1]) == "--manual-only")
			return runManualAccumulatorTests(false);
		if(_argc != 1)
			throw std::string("Unknown accumulator-test option");
		const int expected = runExpectedTests();
		const int differential = runDifferentialTests();
		const int sequences = runSequenceTests();
		const int divide = runDivideFlagTests();
		const int manual = runManualAccumulatorTests(false);
		return expected || differential || sequences || divide || manual ? 1 : 0;
	}
	catch(const std::string& _error)
	{
		std::cerr << _error << '\n';
	}
	catch(const std::exception& _error)
	{
		std::cerr << _error.what() << '\n';
	}
	return 1;
}
