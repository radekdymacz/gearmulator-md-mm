// P0 proof: edit the playing Machinedrum pattern by SysEx while the firmware
// sequencer runs, and measure what happens.
//
//   mdPatternLiveEditFirmwareTest <MD-1.63-ROM> <output-dir>
//
// Three deterministic runs share one timeline (boot, PLAY, five pattern loops):
//   A  baseline: no pattern sent
//   B  the unchanged pattern is re-sent at each edit point (transport cost only)
//   C  an edited pattern is sent at each edit point (trig, then p-lock)
// B-A isolates glitches caused by receiving a dump; C-B isolates the edit.
// A is also rendered twice (A2) to prove the emulation is deterministic, and A2
// samples step LEDs plus main/internal RAM at every step to find a playhead.
// Requires user-supplied firmware; exits 77 (skip) without it.

#include "elektronData/mdPattern.h"

#include "mdLib/mdhardware.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using Bytes = std::vector<uint8_t>;

	constexpr uint32_t g_block = 64;
	constexpr double g_bpm = 125.0;				// MD A01 factory tempo, read off the LCD
	constexpr double g_stepFrames = 44100.0 * 60.0 / g_bpm / 4.0;	// 5292 frames per 16th
	constexpr uint32_t g_patternSteps = 32;
	constexpr uint32_t g_loops = 5;

	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	Bytes load(const char* _path)
	{
		std::ifstream s(_path, std::ios::binary);
		return {std::istreambuf_iterator<char>(s), std::istreambuf_iterator<char>()};
	}

	void writeWav(const std::string& _path, const std::vector<float>& _l, const std::vector<float>& _r)
	{
		std::ofstream s(_path, std::ios::binary);
		const auto u32 = [&s](const uint32_t _v) { s.write(reinterpret_cast<const char*>(&_v), 4); };
		const auto u16 = [&s](const uint16_t _v) { s.write(reinterpret_cast<const char*>(&_v), 2); };
		const auto frames = static_cast<uint32_t>(_l.size());
		s.write("RIFF", 4); u32(36 + frames * 8); s.write("WAVEfmt ", 8);
		u32(16); u16(3); u16(2); u32(44100); u32(44100 * 8); u16(8); u16(32);
		s.write("data", 4); u32(frames * 8);
		for(uint32_t i = 0; i < frames; ++i)
		{
			s.write(reinterpret_cast<const char*>(&_l[i]), 4);
			s.write(reinterpret_cast<const char*>(&_r[i]), 4);
		}
	}

	// ---- transport: the only code here that talks to the machine ----

	class Link
	{
	public:
		explicit Link(md::Hardware& _hw) : m_hw(_hw) {}

		void send(const Bytes& _sysex, const synthLib::MidiEventSource _source = synthLib::MidiEventSource::Host)
		{
			synthLib::SMidiEvent e(_source);
			e.sysex.assign(_sysex.begin(), _sysex.end());
			m_hw.sendMidi(e);
		}

		// Collect finished SysEx from the firmware's MIDI out.
		void poll(std::vector<Bytes>& _received)
		{
			m_events.clear();
			m_hw.readMidiOut(m_events);
			for(const auto& e : m_events)
				if(!e.sysex.empty())
					_received.emplace_back(e.sysex.begin(), e.sysex.end());
		}

	private:
		md::Hardware& m_hw;
		std::vector<synthLib::SMidiEvent> m_events;
	};

	void boot(md::Hardware& _hw)
	{
		uint32_t frames = 0;
		while(!_hw.isFirmwareMidiReady())
		{
			_hw.advance(g_block);
			frames += g_block;
			require(frames < 44100 * 60, "MD firmware boot timed out");
		}
		// The splash animation keeps running for ~20 s after MIDI is ready.
		for(uint32_t i = 0; i < 44100 * 25 / g_block; ++i)
			_hw.advance(g_block);
	}

	Bytes requestBlocking(md::Hardware& _hw, const Bytes& _request, const uint8_t _reply, uint32_t& _frames)
	{
		Link link(_hw);
		std::vector<Bytes> rx;
		link.poll(rx);
		rx.clear();
		link.send(_request);
		for(_frames = 0; _frames < 44100 * 5; _frames += g_block)
		{
			_hw.advance(g_block);
			link.poll(rx);
			for(const auto& m : rx)
				if(m.size() > 7 && m[6] == _reply)
					return m;
			rx.clear();
		}
		return {};
	}

	// ---- scenario ----

	enum class Mode { Baseline, RequestOnly, Resend, Edit, LockProof };

	using Plan = std::vector<std::pair<double, elektronData::MdPattern>>;	// (step position, pattern to send)

	// What each run sends, and when (in steps from loop 1 step 1).
	Plan planFor(const Mode _mode, const elektronData::MdPattern& _initial)
	{
		const std::array<double, 3> at{1.0 * g_patternSteps + 4.5, 2.0 * g_patternSteps + 12.5,
			3.0 * g_patternSteps + 20.5};
		switch(_mode)
		{
		case Mode::Baseline:
			return {};
		case Mode::RequestOnly:
		case Mode::Resend:
			return {{at[0], _initial}, {at[1], _initial}, {at[2], _initial}};
		case Mode::Edit:
		{
			// Cumulative: kick on step 9, p-lock step 17's kick, then length 32 -> 16.
			const auto trig = elektronData::withTrig(_initial, 0, 8, true);
			const auto lock = *elektronData::withLock(trig, 0, 0, 16, 0);
			auto shorter = lock;
			shorter.length = 16;
			shorter.scale = 0;
			return {{at[0], trig}, {at[1], lock}, {at[2], shorter}};
		}
		case Mode::LockProof:
		{
			// Only track 1 plays (steps 1 and 17); then lock step 17's first
			// synthesis parameter to 0 so the two kicks must differ.
			auto solo = _initial;
			for(size_t t = 1; t < elektronData::MdPattern::g_tracks; ++t)
				solo.trigs[t] = 0;
			const auto locked = *elektronData::withLock(solo, 0, 0, 16, 0);
			return {{0.5 * g_patternSteps + 4.5, solo}, {2.0 * g_patternSteps + 20.5, locked}};
		}
		}
		return {};
	}

	// Found by this harness (see findPlayhead): MD OS 1.63 current-step byte.
	constexpr uint32_t g_playheadAddress = 0x261aa7;

	struct EditEvent
	{
		uint32_t frame = 0;			// capture frame at which the dump was queued
		uint32_t drainedFrames = 0;	// until every byte was consumed by the firmware UART
		uint32_t replyFrames = 0;	// dump request -> dump reply, issued right after draining
		bool readBackMatches = false;
		size_t bytes = 0;
	};

	struct RunResult
	{
		std::vector<float> left, right;
		std::vector<EditEvent> edits;
		size_t rxOverflow = 0;
		double wallSeconds = 0;
		elektronData::MdPattern initial;
		std::vector<std::array<bool, 16>> stepLeds;	// sampled mid-step
		std::vector<Bytes> ram;						// sampled mid-step
		std::vector<std::pair<uint32_t, uint8_t>> playhead;	// (capture frame, value) at each change
	};

	struct Probe
	{
		bool enabled = false;
		double gridOffset = 0;	// capture frame of step 1 of loop 1
	};

	Bytes snapshotRam(md::Hardware& _hw)
	{
		Bytes ram;
		ram.reserve(0x100000 + 0x2000);
		for(uint32_t a = 0x200000; a < 0x300000; ++a)
			ram.push_back(_hw.getUC().read8(a));
		for(uint32_t a = 0x01000000; a < 0x01002000; ++a)
			ram.push_back(_hw.getUC().read8(a));
		return ram;
	}

	RunResult run(const Bytes& _rom, const char* _romName, const Mode _mode, const Probe& _probe)
	{
		const auto t0 = std::chrono::steady_clock::now();
		md::Hardware hw(_rom, _romName, md::MachineModel::Machinedrum);
		require(hw.isValid(), "firmware did not construct a valid machine");
		boot(hw);

		RunResult result;
		uint32_t frames = 0;
		const auto original = requestBlocking(hw, elektronData::mdPatternRequest(0), 0x67, frames);
		const auto decoded = elektronData::decodeMdPattern(original);
		require(decoded.has_value(), "firmware pattern dump did not decode");
		require(elektronData::encodeMdPattern(*decoded) == original, "firmware dump is not byte-exact after codec");
		result.initial = *decoded;

		const auto plan = planFor(_mode, *decoded);

		Link link(hw);
		const auto playPacket = md::panelPacket(hw.getModel(), md::PanelControl::Play);
		const uint32_t total = static_cast<uint32_t>((g_loops * g_patternSteps + 4) * g_stepFrames);
		std::array<std::vector<float>, 6> out;
		for(auto& o : out)
			o.resize(g_block);
		synthLib::TAudioOutputs outputs{};
		for(size_t c = 0; c < out.size(); ++c)
			outputs[c] = out[c].data();

		size_t nextEdit = 0;
		std::optional<EditEvent> pending;
		bool awaitingReply = false;
		uint32_t replyStart = 0;
		std::vector<Bytes> rx;
		size_t nextProbeStep = 0;
		const auto overflowBefore = hw.midiRxOverflowCount();

		for(uint32_t f = 0; f < total; f += g_block)
		{
			if(f == 0)
				hw.trySendPanelEvent(playPacket->row, playPacket->mask);
			if(f == 2048)
				hw.trySendPanelEvent(playPacket->row, 0);

			if(nextEdit < plan.size() && f >= _probe.gridOffset + plan[nextEdit].first * g_stepFrames
				&& _probe.gridOffset > 0)
			{
				const auto bytes = _mode == Mode::RequestOnly ? elektronData::mdPatternRequest(0)
					: elektronData::encodeMdPattern(plan[nextEdit].second);
				link.send(bytes);
				++nextEdit;
				if(_mode != Mode::RequestOnly)
					pending = EditEvent{f, 0, 0, false, bytes.size()};
			}

			hw.processAudio(outputs, g_block, 0);
			const auto step = hw.getUC().read8(g_playheadAddress);
			if(result.playhead.empty() || result.playhead.back().second != step)
				result.playhead.emplace_back(f, step);
			for(uint32_t i = 0; i < g_block; ++i)
			{
				result.left.push_back(out[0][i]);
				result.right.push_back(out[1][i]);
			}

			link.poll(rx);
			if(pending && !awaitingReply && hw.isMidiIngressIdle())
			{
				pending->drainedFrames = f + g_block - pending->frame;
				link.send(elektronData::mdPatternRequest(0));
				awaitingReply = true;
				replyStart = f + g_block;
				rx.clear();
			}
			if(awaitingReply)
			{
				for(const auto& m : rx)
				{
					if(m.size() < 8 || m[6] != 0x67)
						continue;
					pending->replyFrames = f + g_block - replyStart;
					const auto& expected = _mode == Mode::Edit ? plan[nextEdit - 1].second : *decoded;
					const auto back = elektronData::decodeMdPattern(m);
					pending->readBackMatches = back && *back == expected;
					result.edits.push_back(*pending);
					pending.reset();
					awaitingReply = false;
					break;
				}
			}
			rx.clear();

			if(_probe.enabled)
			{
				const auto probeFrame = _probe.gridOffset + (nextProbeStep + 0.5) * g_stepFrames;
				if(f >= probeFrame && nextProbeStep < 2 * g_patternSteps + 8)
				{
					const auto panel = hw.getFrontPanelSnapshot();
					std::array<bool, 16> leds{};
					for(uint32_t i = 0; i < 16; ++i)
						leds[i] = panel.getStepLed(i);
					result.stepLeds.push_back(leds);
					result.ram.push_back(snapshotRam(hw));
					++nextProbeStep;
				}
			}
		}
		result.rxOverflow = hw.midiRxOverflowCount() - overflowBefore;
		result.wallSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		return result;
	}

	// ---- analysis (pure) ----

	float peak(const std::vector<float>& _l, const std::vector<float>& _r, const size_t _begin, const size_t _end)
	{
		float p = 0;
		for(size_t i = _begin; i < std::min(_end, _l.size()); ++i)
			p = std::max(p, std::max(std::abs(_l[i]), std::abs(_r[i])));
		return p;
	}

	size_t firstOnset(const std::vector<float>& _l, const float _threshold)
	{
		for(size_t i = 0; i < _l.size(); ++i)
			if(std::abs(_l[i]) > _threshold)
				return i;
		return _l.size();
	}

	struct Diff
	{
		float maxAbs = 0;
		size_t first = SIZE_MAX;
	};

	Diff diff(const RunResult& _a, const RunResult& _b)
	{
		Diff d;
		for(size_t i = 0; i < std::min(_a.left.size(), _b.left.size()); ++i)
		{
			const auto v = std::max(std::abs(_a.left[i] - _b.left[i]), std::abs(_a.right[i] - _b.right[i]));
			if(v > 0 && d.first == SIZE_MAX)
				d.first = i;
			d.maxAbs = std::max(d.maxAbs, v);
		}
		return d;
	}

	// Largest sample-to-sample jump, the cheapest discontinuity detector.
	float maxJump(const std::vector<float>& _l, const size_t _begin, const size_t _end)
	{
		float j = 0;
		for(size_t i = std::max<size_t>(_begin, 1); i < std::min(_end, _l.size()); ++i)
			j = std::max(j, std::abs(_l[i] - _l[i - 1]));
		return j;
	}

	void printGrid(const char* _label, const RunResult& _r, const double _offset)
	{
		std::printf("%s step peaks (dBFS, '.' < -60):\n", _label);
		for(uint32_t loop = 0; loop < g_loops; ++loop)
		{
			std::printf("  loop %u:", loop + 1);
			for(uint32_t s = 0; s < g_patternSteps; ++s)
			{
				const auto begin = static_cast<size_t>(_offset + (loop * g_patternSteps + s) * g_stepFrames - 64);
				const auto p = peak(_r.left, _r.right, begin, begin + 512);
				if(p < 0.001f)
					std::printf("   .");
				else
					std::printf(" %3d", static_cast<int>(std::lround(20 * std::log10(p))));
			}
			std::printf("\n");
		}
	}

	size_t zeroCrossings(const std::vector<float>& _x, const size_t _begin, const size_t _count)
	{
		size_t n = 0;
		for(size_t i = _begin + 1; i < std::min(_begin + _count, _x.size()); ++i)
			n += (_x[i - 1] < 0) != (_x[i] < 0) ? 1 : 0;
		return n;
	}

	// Track 1 solo: loop 2 plays step 1 and 17 unlocked, loop 4 has step 17 locked.
	void lockProof(const RunResult& _r, const double _offset)
	{
		const auto window = [&](const uint32_t _loop, const uint32_t _step)
		{
			return static_cast<size_t>(_offset + ((_loop - 1) * g_patternSteps + _step - 1) * g_stepFrames);
		};
		constexpr size_t count = 4096;
		for(const uint32_t loop : {2u, 4u})
		{
			const auto s1 = window(loop, 1);
			const auto s17 = window(loop, 17);
			float maxDiff = 0;
			for(size_t i = 0; i < count; ++i)
				maxDiff = std::max(maxDiff, std::abs(_r.left[s1 + i] - _r.left[s17 + i]));
			std::printf("lock proof loop %u (%s): step1 zc=%zu peak=%.3f | step17 zc=%zu peak=%.3f | max|s1-s17|=%.3f\n",
				loop, loop == 2 ? "no lock" : "step 17 PTCH locked to 0", zeroCrossings(_r.left, s1, count),
				peak(_r.left, _r.right, s1, s1 + count), zeroCrossings(_r.left, s17, count),
				peak(_r.left, _r.right, s17, s17 + count), maxDiff);
		}
	}

	// Step boundaries as the firmware sees them: compare two runs' step clocks.
	void comparePlayhead(const char* _label, const RunResult& _a, const RunResult& _b)
	{
		const auto n = std::min(_a.playhead.size(), _b.playhead.size());
		int64_t worst = 0;
		size_t mismatched = 0;
		for(size_t i = 0; i < n; ++i)
		{
			const auto d = static_cast<int64_t>(_b.playhead[i].first) - static_cast<int64_t>(_a.playhead[i].first);
			if(std::abs(d) > std::abs(worst))
				worst = d;
			mismatched += _a.playhead[i].second != _b.playhead[i].second ? 1 : 0;
		}
		size_t shifted = 0;
		for(size_t i = 0; i < n; ++i)
			shifted += _a.playhead[i].first != _b.playhead[i].first ? 1 : 0;
		const auto last = n ? static_cast<int64_t>(_b.playhead[n - 1].first) - static_cast<int64_t>(_a.playhead[n - 1].first) : 0;
		std::printf("%s: sequencer step clock vs A over %zu steps: %zu steps moved, worst %lld frames (%.2f ms),"
			" last step %lld frames, value mismatches %zu\n", _label, n, shifted, static_cast<long long>(worst),
			worst / 44.1, static_cast<long long>(last), mismatched);
	}

	void printPlayhead(const char* _label, const RunResult& _r, const double _offset)
	{
		std::printf("%s playhead byte (loop 4 onwards, value@step):", _label);
		for(const auto& [frame, value] : _r.playhead)
		{
			const auto s = (frame - _offset) / g_stepFrames;
			if(s >= 3 * g_patternSteps + 16 && s < 3 * g_patternSteps + 60)
				std::printf(" %u@%.2f", value, s - 3 * g_patternSteps + 1);
		}
		std::printf("\n");
	}

	void findPlayhead(const RunResult& _r)
	{
		std::printf("step LEDs sampled mid-step (first 34 steps):\n");
		for(size_t k = 0; k < std::min<size_t>(34, _r.stepLeds.size()); ++k)
		{
			std::printf("  step %2zu: ", k + 1);
			for(const auto on : _r.stepLeds[k])
				std::printf("%c", on ? '#' : '-');
			std::printf("\n");
		}

		const auto& ram = _r.ram;
		if(ram.size() < 8)
			return;
		size_t shown = 0;
		for(size_t a = 0; a < ram[0].size() && shown < 24; ++a)
		{
			for(const uint32_t modulo : {16u, 32u, 64u, 128u})
			{
				bool match = true;
				for(size_t k = 1; k < ram.size() && match; ++k)
					match = (ram[k][a] - ram[k - 1][a] + 256) % 256 % modulo == 1 % modulo
						&& ram[k][a] < modulo;
				if(!match)
					continue;
				const auto address = a < 0x100000 ? 0x200000 + a : 0x01000000 + (a - 0x100000);
				std::printf("playhead candidate: 0x%08zx mod %u, value at step 1 = %u\n",
					address, modulo, ram[0][a]);
				++shown;
				break;
			}
		}
		if(!shown)
			std::printf("playhead candidate: none (8-bit linear counter not found)\n");
	}
}

int main(const int _argc, char** _argv)
{
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	if(_argc != 3)
	{
		std::puts("usage: mdPatternLiveEditFirmwareTest <MD-1.63-ROM> <output-dir>");
		return 77;
	}
	try
	{
		const auto rom = load(_argv[1]);
		require(rom.size() == md::g_romSize, "ROM must be the 8 MiB MD 1.63 image");
		const std::string outDir = _argv[2];

		// Pass 0: find the grid. The first sound after PLAY is the step-1 kick.
		auto a = run(rom, _argv[1], Mode::Baseline, {});
		const auto onset = firstOnset(a.left, 0.01f);
		require(onset < a.left.size(), "no audio after PLAY: pattern A01 has no audible step 1");
		const Probe probe{true, static_cast<double>(onset) - 32};
		std::printf("MD pattern A01: length=%u scale=%u kit=%u trigs(track1)=%016llx lockRows=%zu\n",
			a.initial.length, a.initial.scale, a.initial.kit,
			static_cast<unsigned long long>(a.initial.trigs[0]), elektronData::usedLockRows(a.initial));
		std::printf("first onset after PLAY press: %zu frames (%.1f ms)\n", onset, onset / 44.1);

		auto a2 = run(rom, _argv[1], Mode::Baseline, probe);
		auto r = run(rom, _argv[1], Mode::RequestOnly, probe);
		auto b = run(rom, _argv[1], Mode::Resend, probe);
		auto c = run(rom, _argv[1], Mode::Edit, probe);
		auto l = run(rom, _argv[1], Mode::LockProof, probe);

		const auto dA = diff(a, a2);
		std::printf("determinism A vs A2: maxAbsDiff=%g\n", dA.maxAbs);

		for(const auto* r : {&b, &c})
		{
			const char* label = r == &b ? "B resend" : "C edit";
			for(size_t i = 0; i < r->edits.size(); ++i)
			{
				const auto& e = r->edits[i];
				const auto stepPos = (e.frame - probe.gridOffset) / g_stepFrames;
				std::printf("%s #%zu: %zu bytes queued at loop %d step %.2f; UART drained after %u frames (%.1f ms);"
					" dump request->reply %u frames (%.1f ms); read-back matches sent: %s\n",
					label, i + 1, e.bytes, static_cast<int>(stepPos / g_patternSteps) + 1,
					std::fmod(stepPos, g_patternSteps) + 1, e.drainedFrames, e.drainedFrames / 44.1,
					e.replyFrames, e.replyFrames / 44.1, e.readBackMatches ? "yes" : "NO");
			}
			std::printf("%s: MIDI RX overflow=%zu, wall %.1fs for %.1fs audio\n", label, r->rxOverflow,
				r->wallSeconds, r->left.size() / 44100.0);
		}

		const auto dB = diff(a2, b);
		const auto dC = diff(b, c);
		const auto at = [&](const size_t _frame)
		{
			if(_frame == SIZE_MAX)
				return std::string("never");
			const auto s = (_frame - probe.gridOffset) / g_stepFrames;
			char text[64];
			std::snprintf(text, sizeof(text), "loop %d step %.2f", static_cast<int>(s / g_patternSteps) + 1,
				std::fmod(s, g_patternSteps) + 1);
			return std::string(text);
		};
		std::printf("B (resend unchanged) vs A: maxAbsDiff=%g, first difference at %s\n", dB.maxAbs,
			at(dB.first).c_str());
		std::printf("C (edits) vs B: maxAbsDiff=%g, first difference at %s\n", dC.maxAbs, at(dC.first).c_str());
		std::printf("max sample jump: A %.4f, B %.4f, C %.4f\n", maxJump(a2.left, 0, a2.left.size()),
			maxJump(b.left, 0, b.left.size()), maxJump(c.left, 0, c.left.size()));

		std::printf("R (dump request only) vs A: maxAbsDiff=%g, first difference at %s\n", diff(a2, r).maxAbs,
			at(diff(a2, r).first).c_str());
		comparePlayhead("R request-only", a2, r);
		comparePlayhead("B resend", a2, b);
		comparePlayhead("C edit", a2, c);
		printPlayhead("C", c, probe.gridOffset);

		lockProof(l, probe.gridOffset);

		printGrid("A", a2, probe.gridOffset);
		printGrid("B", b, probe.gridOffset);
		printGrid("C", c, probe.gridOffset);
		findPlayhead(a2);

		writeWav(outDir + "/md_live_edit_A.wav", a2.left, a2.right);
		writeWav(outDir + "/md_live_edit_B.wav", b.left, b.right);
		writeWav(outDir + "/md_live_edit_C.wav", c.left, c.right);
		return 0;
	}
	catch(const std::exception& _e)
	{
		std::fprintf(stderr, "mdPatternLiveEditFirmwareTest FAIL: %s\n", _e.what());
		return 1;
	}
}
