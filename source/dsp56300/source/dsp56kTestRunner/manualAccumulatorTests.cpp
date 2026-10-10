// Independent architectural expectations: DSP56300FM Rev. 5, table 5-1,
// ABS/NEG/INC/DEC, ASL/ASR, ROL/ROR, long moves and DIV (13-54).
// These oracles use raw DSP bit positions, never host-aligned ALU helpers.
#include "dsp56kEmu/assembler.h"
#include "dsp56kEmu/dsp.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
	using namespace dsp56k;
	constexpr uint64_t mask56 = (uint64_t{1} << 56) - 1;
	constexpr uint64_t sign56 = uint64_t{1} << 55;
	constexpr uint64_t mask48 = (uint64_t{1} << 48) - 1;
	constexpr uint64_t guard = 0x11223344556677;

	uint64_t raw(const TReg56& _r)
	{
		return uint64_t(_r.var) >> 8;
	}
	void assign(TReg56& _r, uint64_t _v)
	{
		_r.var = static_cast<int64_t>(_v << 8);
	}
	bool bit(uint64_t _v, unsigned _n)
	{
		return ((_v >> _n) & 1) != 0;
	}
	int scaling(uint32_t _sr)
	{
		return (_sr & SR_S0) ? 1 : (_sr & SR_S1) ? -1 : 0;
	}

	uint32_t arithmeticFlags(uint64_t _result, uint32_t _before, bool _overflow, bool _carry)
	{
		uint32_t sr = _before & ~(CCR_E | CCR_U | CCR_N | CCR_Z | CCR_V | CCR_C);
		const unsigned highFraction = 47 + scaling(_before);
		// The integer portion must be entirely zero or entirely one. Express
		// that directly; keep this oracle independent of aligned ALU helpers.
		const uint64_t integer = _result >> highFraction;
		const uint64_t allOnes = (uint64_t{1} << (56 - highFraction)) - 1;
		if(integer != 0 && integer != allOnes)
			sr |= CCR_E;
		if(bit(_result, highFraction) == bit(_result, highFraction - 1))
			sr |= CCR_U;
		if(bit(_result, 55))
			sr |= CCR_N;
		if(_result == 0)
			sr |= CCR_Z;
		if(_overflow)
			sr |= CCR_V | CCR_L;
		if(_carry)
			sr |= CCR_C;
		return sr;
	}

	struct State
	{
		uint64_t value;
		uint32_t sr;
	};
	State divide(State _d, uint32_t _source)
	{
		const bool add = bit(_d.value, 55) != bit(_source, 23);
		const bool overflow = bit(_d.value, 55) != bit(_d.value, 54);
		const uint64_t divisor = (uint64_t(_source) << 24) | (bit(_source, 23) ? 0xff000000000000ULL : 0);
		_d.value = ((_d.value << 1) | ((_d.sr & CCR_C) ? 1 : 0)) & mask56;
		_d.value = (add ? _d.value + divisor : _d.value - divisor) & mask56;
		_d.sr &= ~(CCR_V | CCR_C);
		if(overflow)
			_d.sr |= CCR_V | CCR_L;
		if(!bit(_d.value, 55))
			_d.sr |= CCR_C;
		return _d;
	}

	struct Fixture
	{
		DefaultMemoryValidator validator;
		Memory mem{validator, 0x1000};
		PeripheralsNop px, py;
		DSP dsp{mem, &px, &py};
		Assembler assembler;
		TWord cursor = 0x100;
		TWord firstAdvancePC = 0;

		Fixture(bool _optimize, unsigned _blockSize)
		{
			auto config = dsp.getJit().getConfig();
			config.maxInstructionsPerBlock = _blockSize;
			config.linkJitBlocks = true;
			config.enableOptimizer = _optimize;
			dsp.getJit().setConfig(config);
			assign(dsp.regs().a, guard);
			assign(dsp.regs().b, guard);
			dsp.regs().x.var = 0x654321123456;
			dsp.regs().y.var = 0x123456abcdef;
			dsp.regs().r[0].var = 0x10;
			mem.set(MemArea_X, 0x10, 0xabcdef);
			mem.set(MemArea_Y, 0x10, 0x123456);
		}

		void emit(const std::string& _text)
		{
			const auto op = assembler.assemble(_text.c_str());
			if(!op.success())
				throw std::string("Manual test assembly failed: ") + _text;
			for(unsigned i = 0; i < op.wordCount; ++i)
				mem.set(MemArea_P, cursor++, op.word[i]);
		}

		void run(bool _jit)
		{
			emit("jmp $300");
			cursor = 0x300;
			emit("jmp $300");
			dsp.setPC(0x100);
			dsp.getJit().checkModeChange();
			// No register/CCR reads while the program executes. The PC read neither
			// materializes CCR nor introduces a generated block boundary.
			for(unsigned dispatch = 0; dispatch < 512 && dsp.getPC() != 0x300; ++dispatch)
			{
				if(_jit)
					dsp.execJit();
				else
					dsp.execInterpreter();
				if(!firstAdvancePC && dsp.getPC() != 0x100)
					firstAdvancePC = dsp.getPC().var;
			}
			if(dsp.getPC() != 0x300)
				throw std::string("Manual test did not reach exit");
		}
	};

	unsigned total = 0, failures = 0, displayed = 0;
	void check(bool _ok, const std::string& _label, bool _jit, bool _optimize, uint64_t _input, uint32_t _initial,
		uint64_t _actual, uint32_t _sr, State _expected)
	{
		++total;
		if(_ok)
			return;
		++failures;
		if(++displayed <= 12)
			std::cerr << "Manual failure " << _label << " jit=" << _jit << " optimizer=" << _optimize << std::hex
					  << " input=" << _input << '/' << _initial << " actual=" << _actual << '/' << _sr
					  << " expected=" << _expected.value << '/' << _expected.sr << std::dec << '\n';
	}

	void divideContexts(bool _interpreterOnly)
	{
		// Valid fractional operands, including a captured firmware input. Overflow
		// probes execute one iteration and make no claim about a valid quotient.
		struct Input
		{
			uint64_t value;
			uint32_t divisor;
			unsigned count;
		};
		const Input inputs[] = {{0x0821d0000000, 0x40d249, 24}, {0x100000000000, 0xc00000, 24}, {1, 0x800000, 24},
			{0x40000000000000, 0x400000, 1}, {0x80000000000000, 0x400000, 1}};
		struct Branch
		{
			const char* name;
			uint32_t flag;
		};
		const Branch branches[] = {{"jcs", CCR_C}, {"jset #1,sr,", CCR_V}, {"jls", CCR_L}, {"jmi", CCR_N}};
		const auto runCase = [&](const Input& _input, unsigned _flags, bool _prefix, bool _useB, unsigned _layout,
								 bool _optimize, bool _jit, const Branch& _branch)
		{
			if(_interpreterOnly && (_jit || _optimize || _layout == 0))
				return;
			const uint32_t initial = ((_flags & 1) ? CCR_C : 0) | ((_flags & 2) ? CCR_V : 0)
				| ((_flags & 4) ? CCR_L : 0) | ((_flags & 1) ? CCR_E | CCR_U | CCR_N | CCR_Z | CCR_S : 0);
			auto f = std::make_unique<Fixture>(_optimize, _layout == 0 ? 1 : 64);
			auto& r = f->dsp.regs();
			assign(_useB ? r.b : r.a, _input.value);
			// The other accumulator supplies pending E/U/N/Z in the prefix variant.
			const uint64_t other = (_flags & 1) ? 0 : sign56;
			assign(_useB ? r.a : r.b, other);
			r.x.var = _input.divisor;
			r.sr.var = initial;
			State expected{_input.value, initial};
			if(_prefix)
			{
				f->emit(_useB ? "tst a" : "tst b");
				expected.sr = arithmeticFlags(other, initial, false, (initial & CCR_C) != 0);
			}
			if(_layout == 2)
				f->emit("rep #$" + std::string(_input.count == 24 ? "18" : "1"));
			for(unsigned i = 0; i < (_layout == 2 ? 1 : _input.count); ++i)
				f->emit(_useB ? "div x0,b" : "div x0,a");
			for(unsigned i = 0; i < _input.count; ++i)
				expected = divide(expected, _input.divisor);
			const TWord afterDiv = f->cursor;
			f->emit(std::string(_branch.name) + (_branch.flag == CCR_V ? "$200" : " $200"));
			f->emit("move #>1,x1");
			f->emit("jmp $300");
			f->cursor = 0x200;
			f->emit("move #>2,x1");
			f->run(_jit);
			const auto value = raw(_useB ? r.b : r.a);
			const auto sr = f->dsp.getSR().var;
			const auto marker = (expected.sr & _branch.flag) ? 2u : 1u;
			const bool ok = (!_jit || _layout != 1 || f->firstAdvancePC >= afterDiv) && value == expected.value
				&& sr == expected.sr && raw(_useB ? r.a : r.b) == other
				&& r.x.var == (uint64_t(marker) << 24 | _input.divisor) && r.y.var == 0x123456abcdef
				&& f->mem.get(MemArea_X, 0x10) == 0xabcdef && f->mem.get(MemArea_Y, 0x10) == 0x123456;
			check(ok,
				"DIV layout=" + std::to_string(_layout) + " prefix=" + std::to_string(_prefix) + " " + _branch.name
					+ (_useB ? " B" : " A"),
				_jit, _optimize, _input.value, initial, value, sr, expected);
		};
		for(const auto& input : inputs)
			for(unsigned flags = 0; flags < 8; ++flags)
				for(bool prefix : {false, true})
					for(bool useB : {false, true})
						for(unsigned layout = 0; layout < 3; ++layout)
							for(bool optimize : {false, true})
								for(bool jit : {false, true})
									for(const auto& branch : branches)
										runCase(input, flags, prefix, useB, layout, optimize, jit, branch);
	}

	// Reduced regression: DIV is unnecessary to lose pending flags on the untaken
	// register-bit branch. Check both branch outcomes and pending/explicit bits.
	void statusBranches(bool _interpreterOnly)
	{
		for(const auto* branch : {"jset", "jclr"})
			for(unsigned bitIndex : {1u, 3u})
				for(uint64_t input : {uint64_t{0}, sign56})
					for(bool useB : {false, true})
						for(bool optimize : {false, true})
							for(bool jit : {false, true})
							{
								if(_interpreterOnly && (jit || optimize))
									continue;
								auto f = std::make_unique<Fixture>(optimize, 64);
								auto& r = f->dsp.regs();
								assign(useB ? r.b : r.a, input);
								r.sr.var = CCR_C | CCR_V;
								const State expected{input, arithmeticFlags(input, CCR_C | CCR_V, false, true)};
								f->emit(useB ? "tst b" : "tst a");
								f->emit(std::string(branch) + " #" + std::to_string(bitIndex) + ",sr,$200");
								f->emit("move #>1,x1");
								f->emit("jmp $300");
								f->cursor = 0x200;
								f->emit("move #>2,x1");
								f->run(jit);
								const bool set = (expected.sr & (1u << bitIndex)) != 0;
								const uint64_t marker = (set == (std::string(branch) == "jset")) ? 2 : 1;
								const auto value = raw(useB ? r.b : r.a);
								const auto sr = f->dsp.getSR().var;
								check(value == expected.value && sr == expected.sr
										&& r.x.var == ((marker << 24) | 0x123456) && raw(useB ? r.a : r.b) == guard,
									std::string("TST/") + branch + " bit=" + std::to_string(bitIndex), jit, optimize,
									input, CCR_C | CCR_V, value, sr, expected);
							}
	}

	State arithmetic(const std::string& _op, uint64_t _input, uint32_t _sr, unsigned _count)
	{
		uint64_t result = _input;
		bool carry = (_sr & CCR_C) != 0, overflow = false;
		if(_op == "abs" || _op == "neg")
		{
			if(_op == "neg" || bit(_input, 55))
				result = (uint64_t{0} - _input) & mask56;
			overflow = _input == sign56;
		}
		else if(_op == "inc")
		{
			result = (_input + 1) & mask56;
			carry = _input == mask56;
			overflow = _input == sign56 - 1;
		}
		else if(_op == "dec")
		{
			result = (_input - 1) & mask56;
			carry = _input == 0;
			overflow = _input == sign56;
		}
		else
		{
			carry = false;
			// Repeated one-bit steps make intermediate ASL overflow explicit and
			// avoid reproducing either backend's host-width shift algorithm.
			for(unsigned i = 0; i < _count; ++i)
				if(_op == "asl")
				{
					carry = bit(result, 55);
					result = (result << 1) & mask56;
					overflow |= carry != bit(result, 55);
				}
				else
				{
					carry = bit(result, 0);
					result = (result >> 1) | (result & sign56);
				}
		}
		return {result, arithmeticFlags(result, _sr, overflow, carry)};
	}

	void boundaries(bool _interpreterOnly)
	{
		const uint64_t values[] = {0, 1, mask56, sign56 - 1, sign56, sign56 + 1, 0x40000000000000, 0xc0000000000000,
			0x800000000000, 0x7fffffffffff, 0xff800000000000};
		struct Operation
		{
			const char* name;
			unsigned count;
			bool variable;
		};
		const Operation operations[] = {{"abs", 0, false}, {"neg", 0, false}, {"inc", 0, false}, {"dec", 0, false},
			{"asl", 0, false}, {"asr", 0, false}, {"asl", 2, false}, {"asr", 1, false}, {"asl", 55, false},
			{"asr", 55, false}, {"asl", 2, true}, {"asr", 1, true}, {"asl", 0, true}, {"asr", 0, true},
			{"asl", 55, true}, {"asr", 55, true}};
		for(const auto& op : operations)
			for(auto input : values)
				for(auto mode : {0u, uint32_t(SR_S0), uint32_t(SR_S1)})
					for(auto flags : {0u, 0xffu})
						for(bool useB : {false, true})
							for(bool jit : {false, true})
							{
								if(_interpreterOnly && jit)
									continue;
								auto f = std::make_unique<Fixture>(true, 64);
								auto& r = f->dsp.regs();
								assign(useB ? r.b : r.a, input);
								r.sr.var = mode | flags;
								const std::string dest = useB ? "b" : "a";
								std::string instruction = std::string(op.name) + " " + dest;
								if(std::string(op.name) == "asl" || std::string(op.name) == "asr")
								{
									// Upper 18 source bits are ignored; low six contain the shift count.
									r.x.var = 0x654321ffffc0ULL | op.count;
									instruction = std::string(op.name) + " "
										+ (op.variable ? "x0" : "#" + std::to_string(op.count)) + "," + dest + ","
										+ dest;
								}
								const uint64_t beforeX = r.x.var;
								const auto expected = arithmetic(op.name, input, mode | flags, op.count);
								f->emit(instruction);
								f->run(jit);
								const auto value = raw(useB ? r.b : r.a);
								const auto sr = f->dsp.getSR().var;
								check(value == expected.value && sr == expected.sr && raw(useB ? r.a : r.b) == guard
										&& r.x.var == beforeX && r.y.var == 0x123456abcdef && r.r[0].var == 0x10
										&& f->mem.get(MemArea_X, 0x10) == 0xabcdef
										&& f->mem.get(MemArea_Y, 0x10) == 0x123456,
									instruction, jit, true, input, mode | flags, value, sr, expected);
							}
		for(const auto* op : {"rol", "ror"})
			for(uint64_t field : {0ULL, 1ULL, 0x400000ULL, 0x800000ULL, 0xffffffULL})
				for(auto flags : {0u, uint32_t(CCR_C), 0xfeu, 0xffu})
					for(bool useB : {false, true})
						for(bool jit : {false, true})
						{
							if(_interpreterOnly && jit)
								continue;
							auto f = std::make_unique<Fixture>(true, 64);
							auto& r = f->dsp.regs();
							const uint64_t input = 0x55000000abcdef | (field << 24);
							assign(useB ? r.b : r.a, input);
							r.sr.var = flags;
							const bool left = std::string(op) == "rol";
							const uint64_t resultField = left ? ((field << 1) | ((flags & CCR_C) ? 1 : 0)) & 0xffffff
															  : (field >> 1) | ((flags & CCR_C) ? 0x800000 : 0);
							State expected{(input & ~uint64_t(0xffffff000000)) | (resultField << 24),
								flags & ~(CCR_N | CCR_Z | CCR_V | CCR_C)};
							if(bit(resultField, 23))
								expected.sr |= CCR_N;
							if(!resultField)
								expected.sr |= CCR_Z;
							if(bit(field, left ? 23 : 0))
								expected.sr |= CCR_C;
							const auto instruction = std::string(op) + (useB ? " b" : " a");
							f->emit(instruction);
							f->run(jit);
							const auto value = raw(useB ? r.b : r.a);
							const auto sr = f->dsp.getSR().var;
							check(value == expected.value && sr == expected.sr && raw(useB ? r.a : r.b) == guard
									&& r.x.var == 0x654321123456 && r.y.var == 0x123456abcdef,
								instruction, jit, true, input, flags, value, sr, expected);
						}
	}

	void transfers(bool _interpreterOnly)
	{
		for(auto mode : {0u, uint32_t(SR_S0), uint32_t(SR_S1)})
		{
			const int offset = scaling(mode);
			const int64_t threshold = int64_t{1} << (47 + offset);
			std::vector<int64_t> inputs = {0, 1, -1, int64_t(sign56 - 1), -int64_t(sign56)};
			for(int delta : {-2, -1, 0, 1, 2})
			{
				inputs.push_back(threshold + delta);
				inputs.push_back(-threshold + delta);
			}
			// Also exercise the S-bit growth detector, distinct from overflow/L.
			inputs.push_back(int64_t{1} << (46 - offset));
			for(auto signedInput : inputs)
				for(bool otherGrowth : {false, true})
					for(auto flags : {0u, 0xffu})
						for(bool useB : {false, true})
							for(const auto* address : {"l:<$10", "l:(r0)"})
								for(bool jit : {false, true})
								{
									if(_interpreterOnly && jit)
										continue;
									auto f = std::make_unique<Fixture>(true, 64);
									auto& r = f->dsp.regs();
									const auto input = uint64_t(signedInput) & mask56;
									assign(useB ? r.b : r.a, input);
									const uint64_t other = otherGrowth ? uint64_t{1} << (46 - offset) : 0;
									assign(useB ? r.a : r.b, other);
									r.sr.var = mode | flags;
									int64_t scaled = signedInput;
									if(offset < 0)
										scaled *= 2;
									if(offset > 0)
										scaled = signedInput >= 0 ? signedInput / 2 : -((-signedInput + 1) / 2);
									uint32_t expectedSR = mode | flags;
									if(otherGrowth || bit(input, 46 - offset) != bit(input, 45 - offset))
										expectedSR |= CCR_S;
									if(scaled < -int64_t(uint64_t{1} << 47))
									{
										scaled = -int64_t(uint64_t{1} << 47);
										expectedSR |= CCR_L;
									}
									if(scaled > int64_t((uint64_t{1} << 47) - 1))
									{
										scaled = int64_t((uint64_t{1} << 47) - 1);
										expectedSR |= CCR_L;
									}
									const uint64_t stored = uint64_t(scaled) & mask48;
									const std::string instruction =
										std::string("move ") + (useB ? "b," : "a,") + address;
									f->emit(instruction);
									f->run(jit);
									const auto value = raw(useB ? r.b : r.a);
									const auto sr = f->dsp.getSR().var;
									check(value == input && sr == expectedSR && raw(useB ? r.a : r.b) == other
											&& r.x.var == 0x654321123456 && r.y.var == 0x123456abcdef
											&& r.r[0].var == 0x10 && f->mem.get(MemArea_X, 0x10) == stored >> 24
											&& f->mem.get(MemArea_Y, 0x10) == (stored & 0xffffff),
										instruction, jit, true, input, mode | flags, value, sr, {input, expectedSR});
								}
		}
	}
} // namespace

int runManualAccumulatorTests(bool _interpreterOnly)
{
	// Check literal flag values before using the oracle to validate either engine.
	struct OracleCase
	{
		uint64_t result;
		uint32_t mode;
		uint32_t flags;
	};
	const OracleCase oracleCases[] = {{0, 0, 0x14}, {mask56, 0, 0x18}, {0xc0000000000000, 0, 0x38},
		{0xff800000000000, SR_S1, 0x838}, {0xff800000000000, 0, 0x08}, {0x00800000000000, 0, 0x20}};
	for(const auto& test : oracleCases)
		if(arithmeticFlags(test.result, test.mode, false, false) != test.flags)
			throw std::string("Manual flag oracle failed its literal sanity check");

	total = failures = displayed = 0;
	divideContexts(_interpreterOnly);
	std::cerr << "Manual DIV contexts: " << total << " cases, " << failures << " failures\n";
	displayed = 0;
	statusBranches(_interpreterOnly);
	boundaries(_interpreterOnly);
	displayed = 0;
	transfers(_interpreterOnly);
	std::cerr << "Manual total: " << total << " cases, " << failures << " failures\n";
	return failures ? 1 : 0;
}
