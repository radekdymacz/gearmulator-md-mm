// P3 Machinedrum Editor probes against MD OS 1.63 firmware (manual: needs a
// user-supplied ROM). Discovery harness: it finds and measures, the smoke test
// (mdDeskFirmwareTest) checks.
//
//   mdEditorProbeFirmwareTest <ROM> [workkit|liverec|samples|sds]
//
// Exits 77 (skip) without arguments.

#include "mdFirmwareSession.h"
#include "sdsTestData.h"

#include "elektronData/mdCommands.h"
#include "elektronData/mdGlobal.h"
#include "elektronData/mdJson.h"
#include "elektronData/mdKit.h"
#include "elektronData/mdMachines.h"
#include "elektronData/mdPattern.h"
#include "elektronData/mdWorkingKit.h"

#include "mdLib/mdautomation.h"
#include "mdLib/mdsysextransfer.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <optional>

using namespace mdFirmwareSession;
namespace ed = elektronData;

namespace
{
	std::string g_romName;
	int g_failures = 0;

	void check(const bool _condition, const std::string& _what)
	{
		std::printf("  %s %s\n", _condition ? "ok  " : "FAIL", _what.c_str());
		if(!_condition)
			++g_failures;
	}

	double ms(const uint64_t _frames) { return _frames * 1000.0 / g_rate; }

	std::optional<ed::MdKit> readKit(Machine& _m, const uint8_t _slot)
	{
		return ed::decodeMdKit(_m.request(ed::mdKitRequest(_slot), ed::g_mdKitDump));
	}

	std::optional<ed::MdPattern> readPattern(Machine& _m, const uint8_t _slot)
	{
		return ed::decodeMdPattern(_m.request(ed::mdPatternRequest(_slot), ed::g_mdPatternDump));
	}

	int status(Machine& _m, const ed::MdStatus _param)
	{
		const auto r = ed::parseMdStatusResponse(_m.request(ed::mdStatusRequest(_param), 0x72));
		return r ? r->value : -1;
	}

	void cc(Machine& _m, const uint8_t _track, const uint8_t _index, const uint8_t _value)
	{
		const md::automation::ParameterChange change{
			static_cast<uint8_t>(_index == 24 ? md::automation::machinedrum::Level : _index / 8), _track,
			static_cast<uint8_t>(_index == 24 ? 0 : _index % 8), _value};
		const auto c = md::automation::encodeParameterChange(md::MachineModel::Machinedrum, change, 0);
		require(c.has_value(), "no CC for the parameter");
		_m.send({(*c)[0], (*c)[1], (*c)[2]});
	}

	std::vector<size_t> findAll(const Bytes& _hay, const Bytes& _needle)
	{
		std::vector<size_t> at;
		if(_needle.empty() || _hay.size() < _needle.size())
			return at;
		auto it = _hay.begin();
		while((it = std::search(it, _hay.end(), _needle.begin(), _needle.end())) != _hay.end())
		{
			at.push_back(static_cast<size_t>(it - _hay.begin()));
			++it;
		}
		return at;
	}

	Bytes paramsBlock(const ed::MdKit& _k)
	{
		Bytes b;
		for(const auto& t : _k.params)
			b.insert(b.end(), t.begin(), t.end());
		return b;
	}

	void printAt(const char* _what, const char* _space, const std::vector<size_t>& _at, const uint32_t _base)
	{
		std::printf("  %s in %s:", _what, _space);
		if(_at.empty())
			std::printf(" none");
		for(const auto a : _at)
			std::printf(" 0x%06zx", a + _base);
		std::printf("\n");
	}

	Bytes patchRam(Machine& _m) { return _m.hardware().copyPatchRam(); }

	// ---- working kit ----

	void workingKit(const Bytes& _rom)
	{
		std::puts("== probe: where is the working kit?");
		Machine m(_rom, g_romName);
		const int kitSlot = status(m, ed::MdStatus::Kit);
		require(kitSlot >= 0, "no kit status");
		const auto stored = *readKit(m, static_cast<uint8_t>(kitSlot));
		std::printf("  current kit %d \"%.16s\"\n", kitSlot + 1, reinterpret_cast<const char*>(stored.name.data()));

		auto pr = patchRam(m);
		auto ram = m.snapshotRam();
		printAt("stored params block", "patch RAM", findAll(pr, paramsBlock(stored)), 0);
		printAt("stored params block", "main RAM", findAll(ram, paramsBlock(stored)), 0x200000);

		// Two distinctive CC edits: only the working copy follows them.
		auto edited = stored;
		edited.params[0][0] = stored.params[0][0] == 0x5a ? 0x5b : 0x5a;
		edited.params[3][10] = stored.params[3][10] == 0x21 ? 0x22 : 0x21;
		edited.levels[5] = stored.levels[5] == 0x33 ? 0x34 : 0x33;
		cc(m, 0, 0, edited.params[0][0]);
		cc(m, 3, 10, edited.params[3][10]);
		cc(m, 5, 24, edited.levels[5]);
		m.run(50);
		pr = patchRam(m);
		ram = m.snapshotRam();
		const auto inPatch = findAll(pr, paramsBlock(edited));
		const auto inMain = findAll(ram, paramsBlock(edited));
		printAt("edited params block", "patch RAM", inPatch, 0);
		printAt("edited params block", "main RAM", inMain, 0x200000);
		check(inPatch.size() + inMain.size() == 1, "exactly one copy follows CC edits");

		// Relative layout: is the rest of the dump next to it, in dump order?
		const bool patch = !inPatch.empty();
		const auto& space = patch ? pr : ram;
		const size_t p = patch ? inPatch.front() : inMain.front();
		const auto raw = ed::mdWorkingKitImage(edited);
		const size_t start = p - ed::g_mdWorkingKitParamsOffset;
		size_t equal = 0;
		for(size_t i = 0; i < raw.size() && start + i < space.size(); ++i)
			equal += space[start + i] == raw[i];
		std::printf("  image at %s 0x%06zx: %zu of %zu bytes equal the dump-order image\n", patch ? "patch" : "main",
			start + (patch ? 0 : 0x200000), equal, raw.size());
		for(size_t i = 0; i < raw.size(); ++i)
			if(space[start + i] != raw[i])
				std::printf("    +0x%03zx ram %02x dump %02x\n", i, space[start + i], raw[i]);

		// Every kind of live edit, then the CPU-visible image against SAVE KIT.
		m.send(ed::mdAssignMachine(2, *ed::mdMachineModel("EFM-CB"), ed::MdMachineInit::Synthesis));
		m.send(ed::mdSetLfo(4, 2, 3));
		m.send(ed::mdSetLfo(4, 0, 9));
		m.send(ed::mdSetMasterFx(1, 3, 77));
		m.send(ed::mdSetMuteGroup(6, 7));
		m.send(ed::mdSetTrigGroup(8, 9));
		m.send(ed::mdSetKitName("PROBE KIT"));
		m.run(50);
		const auto readImage = [&]
		{
			Bytes image(ed::g_mdWorkingKitSize);
			for(size_t i = 0; i < image.size(); ++i)
				image[i] = m.read8(static_cast<uint32_t>(ed::g_mdWorkingKitAddress + i));
			return image;
		};
		const auto live = ed::mdWorkingKitFromImage(readImage(), static_cast<uint8_t>(kitSlot));
		m.send(ed::mdSaveKit(static_cast<uint8_t>(kitSlot)));
		const auto saved = readKit(m, static_cast<uint8_t>(kitSlot));
		check(live && saved, "image and SAVE KIT read-back");
		if(!live || !saved)
			return;
		auto a = *live, b = *saved;
		a.version = b.version;
		a.revision = b.revision;
		check(a.name == b.name, "name");
		check(a.params == b.params, "parameters (CC and machine assignment)");
		check(a.levels == b.levels, "levels");
		check(a.models == b.models, "machine models");
		bool lfoFields = true, lfoState = true;
		for(size_t t = 0; t < 16; ++t)
		{
			const auto& x = a.lfos[t];
			const auto& y = b.lfos[t];
			lfoFields &= x.track == y.track && x.param == y.param && x.shape1 == y.shape1 && x.shape2 == y.shape2
				&& x.update == y.update;
			lfoState &= x.state == y.state;
		}
		check(lfoFields, "LFO fields");
		std::printf("  LFO running state %s the saved kit\n", lfoState ? "equals" : "differs from");
		check(a.masterFx == b.masterFx, "master effects");
		check(a.trigGroups == b.trigGroups && a.muteGroups == b.muteGroups, "groups");
		// Which bytes name the current kit? LOAD KIT 5, then 9, then SAVE KIT 12.
		{
			std::map<int, std::pair<Bytes, Bytes>> snaps;
			for(const int k : {5, 9})
			{
				m.send(ed::mdLoadKit(static_cast<uint8_t>(k)));
				m.run(100);
				snaps[k] = {m.snapshotRam(), patchRam(m)};
				std::printf("  LOAD KIT %d: status kit %d, patch RAM 0..9:", k + 1, status(m, ed::MdStatus::Kit) + 1);
				for(int i = 0; i < 10; ++i)
					std::printf(" %02x", snaps[k].second[i]);
				std::printf("\n");
			}
			m.send(ed::mdSaveKit(12));
			m.run(100);
			const auto ram12 = m.snapshotRam();
			const auto pr12 = patchRam(m);
			std::printf("  current-kit byte candidates (5 -> 9 -> 12):");
			for(size_t a = 0; a < ram12.size(); ++a)
				if(snaps[5].first[a] == 5 && snaps[9].first[a] == 9 && ram12[a] == 12)
					std::printf(" main %06zx", a + 0x200000);
			for(size_t a = 0; a < pr12.size(); ++a)
				if(snaps[5].second[a] == 5 && snaps[9].second[a] == 9 && pr12[a] == 12)
					std::printf(" patch %06zx", a + 0x700000);
			std::printf("\n");
			m.send(ed::mdLoadKit(static_cast<uint8_t>(kitSlot)));
			m.run(100);
		}
		// Does the image churn while the pattern plays (LFO running state)?
		m.panel(md::PanelControl::Play);
		auto last = readImage();
		int changes = 0;
		std::map<size_t, int> churn;
		for(int i = 0; i < 100; ++i)
		{
			m.run(20);
			const auto now = readImage();
			if(now != last)
				++changes;
			for(size_t b = 0; b < now.size(); ++b)
				if(now[b] != last[b])
					++churn[b];
			last = now;
		}
		m.panel(md::PanelControl::Stop);
		std::printf("  playing 2 s: image changed in %d of 100 reads; bytes:", changes);
		for(const auto& [b, n] : churn)
			std::printf(" +0x%03zx(%d)", b, n);
		std::printf("\n");
		std::printf("  saved kit: machine 3 %s, LFO 5 track %u shape1 %u, echo FB %u, mute 7->%u, trig 9->%u\n",
			ed::mdMachineName(b.models[2]).c_str(), b.lfos[4].track, b.lfos[4].shape1, b.masterFx[1][3], b.muteGroups[6],
			b.trigGroups[8]);
	}

	// ---- live recording ----

	void rawPanel(Machine& _m, const uint8_t _row, const uint8_t _mask, const double _holdMs = 40)
	{
		_m.hardware().trySendPanelEvent(_row, _mask);
		_m.run(_holdMs);
	}

	int noteForTrack(const ed::MdGlobal& _g, const uint8_t _track)
	{
		for(int n = 0; n < 128; ++n)
			if(_g.keymap[n] == _track)
				return n;
		return -1;
	}

	void liveRecording(const Bytes& _rom)
	{
		std::puts("== probe: live recording (hold RECORD, press PLAY)");
		Machine m(_rom, g_romName);
		const int patSlot = status(m, ed::MdStatus::Pattern);
		const auto g = ed::decodeMdGlobal(m.request(ed::mdGlobalRequest(0), ed::g_mdGlobalDump));
		require(patSlot >= 0 && g.has_value(), "status / global");
		auto p = *readPattern(m, static_cast<uint8_t>(patSlot));
		// Tracks 13-15 empty, no locks there.
		for(uint8_t t = 13; t < 16; ++t)
			for(uint8_t s = 0; s < 64; ++s)
				if(ed::hasTrig(p, t, s))
					p = ed::withTrig(p, t, s, false);
		m.send(ed::encodeMdPattern(p));
		const auto base = *readPattern(m, static_cast<uint8_t>(patSlot));
		std::printf("  pattern %s, length %u, tempo multiplier %u, base channel %u, notes: t14 %d t15 %d t16 %d\n",
			ed::mdPatternName(static_cast<uint8_t>(patSlot)).c_str(), base.length, base.scale, g->baseChannel,
			noteForTrack(*g, 13), noteForTrack(*g, 14), noteForTrack(*g, 15));

		const auto ram0 = m.snapshotRam();
		m.panel(md::PanelControl::Play);
		m.run(300);
		const auto ramPlay = m.snapshotRam();
		m.panel(md::PanelControl::Stop);
		m.run(300);
		// RECORD down, PLAY down while it is held, both up.
		rawPanel(m, 0x22, 0x02);
		rawPanel(m, 0x22, 0x06);
		rawPanel(m, 0x22, 0x02);
		rawPanel(m, 0x22, 0x00);
		m.run(200);
		const auto ramRec = m.snapshotRam();
		std::printf("  after REC+PLAY: playing %d, step %u\n", m.read8(0x28cdaf) == 0, m.playhead());
		// RAM bytes that tell live recording from playing (same in stop and play, different in rec).
		std::vector<uint32_t> flags;
		for(uint32_t a = 0; a < ram0.size(); ++a)
			if(ramRec[a] != ramPlay[a] && ramPlay[a] == ram0[a])
				flags.push_back(a);
		std::printf("  %zu RAM bytes differ only in live recording; first:", flags.size());
		for(size_t i = 0; i < flags.size() && i < 24; ++i)
			std::printf(" %06x=%02x/%02x", flags[i] + 0x200000, ram0[flags[i]], ramRec[flags[i]]);
		std::printf("\n");

		const auto waitStep = [&](const uint8_t _step)
		{
			for(int i = 0; i < 20000 && m.playhead() != _step; ++i)
				m.step();
		};
		// Track 16 by its TRIG key on step 5; a CC to its FLTF before step 9's note.
		const auto n14 = noteForTrack(*g, 13), n15 = noteForTrack(*g, 14);
		waitStep(4);
		rawPanel(m, 0x21, 0x80, 20);
		rawPanel(m, 0x21, 0x00, 20);
		// Track 15: MIDI note on step 9 after a CC on its FLTF (param 12).
		waitStep(7);
		cc(m, 14, 12, 99);
		waitStep(8);
		if(n15 >= 0)
		{
			m.send({static_cast<uint8_t>(0x90 | g->baseChannel), static_cast<uint8_t>(n15), 100});
			m.run(20);
			m.send({static_cast<uint8_t>(0x80 | g->baseChannel), static_cast<uint8_t>(n15), 0});
		}
		// Track 14: MIDI note on step 13, no CC.
		waitStep(12);
		if(n14 >= 0)
		{
			m.send({static_cast<uint8_t>(0x90 | g->baseChannel), static_cast<uint8_t>(n14), 100});
			m.run(20);
			m.send({static_cast<uint8_t>(0x80 | g->baseChannel), static_cast<uint8_t>(n14), 0});
		}
		waitStep(15);
		m.panel(md::PanelControl::Stop);
		m.run(200);
		const auto after = *readPattern(m, static_cast<uint8_t>(patSlot));
		for(uint8_t t = 13; t < 16; ++t)
		{
			std::printf("  track %u trigs:", t + 1);
			for(uint8_t s = 0; s < 64; ++s)
				if(ed::hasTrig(after, t, s))
					std::printf(" %u", s + 1);
			std::printf("\n");
		}
		for(uint8_t t = 13; t < 16; ++t)
			for(uint8_t prm = 0; prm < 24; ++prm)
				for(uint8_t s = 0; s < 64; ++s)
					if(const auto v = ed::lockValue(after, t, prm, s))
						std::printf("  lock track %u param %u step %u = %u\n", t + 1, prm, s + 1, *v);
		check(ed::hasTrig(after, 15, 4) || ed::hasTrig(after, 15, 5), "TRIG key recorded on track 16");
		check(ed::hasTrig(after, 14, 8) || ed::hasTrig(after, 14, 9), "MIDI note recorded on track 15");
	}

	// Second take: which knob moves become locks? The manual: the selected track's
	// DATA ENTRY knobs, locked on that track's next note.
	void liveLocks(const Bytes& _rom)
	{
		std::puts("== probe: live recording, parameter locks (selected track, CC vs DATA ENTRY)");
		Machine m(_rom, g_romName);
		const int patSlot = status(m, ed::MdStatus::Pattern);
		const auto g = ed::decodeMdGlobal(m.request(ed::mdGlobalRequest(0), ed::g_mdGlobalDump));
		auto p = *readPattern(m, static_cast<uint8_t>(patSlot));
		for(uint8_t t = 13; t < 16; ++t)
			for(uint8_t s = 0; s < 64; ++s)
				if(ed::hasTrig(p, t, s))
					p = ed::withTrig(p, t, s, false);
		m.send(ed::encodeMdPattern(p));
		m.send(ed::mdSetStatus(ed::MdStatus::Track, 14));
		m.run(50);
		std::printf("  selected track %d\n", status(m, ed::MdStatus::Track) + 1);
		rawPanel(m, 0x22, 0x02);
		rawPanel(m, 0x22, 0x06);
		rawPanel(m, 0x22, 0x02);
		rawPanel(m, 0x22, 0x00);
		const auto waitStep = [&](const uint8_t _step)
		{
			for(int i = 0; i < 20000 && m.playhead() != _step; ++i)
				m.step();
		};
		const auto note = [&](const uint8_t _track)
		{
			const int n = noteForTrack(*g, _track);
			m.send({static_cast<uint8_t>(0x90 | g->baseChannel), static_cast<uint8_t>(n), 100});
			m.run(20);
			m.send({static_cast<uint8_t>(0x80 | g->baseChannel), static_cast<uint8_t>(n), 0});
		};
		// CC to the selected track (15) param 12, note on step 5.
		waitStep(2);
		cc(m, 14, 12, 99);
		waitStep(4);
		note(14);
		// CC to a track that is not selected (14) param 12, note on step 9.
		waitStep(6);
		cc(m, 13, 12, 88);
		waitStep(8);
		note(13);
		// DATA ENTRY A (synthesis page, param 0) of the selected track, note on step 13.
		waitStep(10);
		for(int i = 0; i < 12; ++i)
			m.hardware().trySendPanelEvent(0x30, 0x01);
		m.run(40);
		waitStep(12);
		note(14);
		waitStep(15);
		m.panel(md::PanelControl::Stop);
		m.run(200);
		const auto after = *readPattern(m, static_cast<uint8_t>(patSlot));
		for(uint8_t t = 13; t < 16; ++t)
		{
			std::printf("  track %u trigs:", t + 1);
			for(uint8_t s = 0; s < 64; ++s)
				if(ed::hasTrig(after, t, s))
					std::printf(" %u", s + 1);
			std::printf("\n");
			for(uint8_t prm = 0; prm < 24; ++prm)
				for(uint8_t s = 0; s < 64; ++s)
					if(const auto v = ed::lockValue(after, t, prm, s))
						std::printf("    lock param %u step %u = %u\n", prm, s + 1, *v);
		}
	}

	Bytes workingImage(Machine& _m)
	{
		Bytes image(ed::g_mdWorkingKitSize);
		for(size_t i = 0; i < image.size(); ++i)
			image[i] = _m.read8(static_cast<uint32_t>(ed::g_mdWorkingKitAddress + i));
		return image;
	}

	uint8_t workingParam(Machine& _m, const uint8_t _t, const uint8_t _i)
	{
		return _m.read8(static_cast<uint32_t>(ed::g_mdWorkingKitAddress + ed::g_mdWorkingKitParamsOffset + _t * 24 + _i));
	}

	// DATA ENTRY knobs: step size, the SYNTHESIS/EFFECTS/ROUTING page in RAM, and a
	// RAM flag for live recording.
	void recordKnobs(const Bytes& _rom)
	{
		std::puts("== probe: DATA ENTRY knobs, knob page, live-recording flag");
		Machine m(_rom, g_romName);
		m.send(ed::mdSetStatus(ed::MdStatus::Track, 2));
		m.run(50);
		cc(m, 2, 1, 40);
		m.run(30);
		const auto v0 = workingParam(m, 2, 1);
		m.hardware().trySendPanelEvent(0x31, 0x01);
		m.run(40);
		const auto v1 = workingParam(m, 2, 1);
		for(int i = 0; i < 5; ++i)
			m.hardware().trySendPanelEvent(0x31, 0x01);
		m.run(40);
		const auto v2 = workingParam(m, 2, 1);
		for(int i = 0; i < 5; ++i)
		{
			m.hardware().trySendPanelEvent(0x31, 0xff);
			m.run(15);
		}
		m.run(30);
		const auto v3 = workingParam(m, 2, 1);
		for(int i = 0; i < 30; ++i)
			m.hardware().trySendPanelEvent(0x31, 0x01);
		m.run(60);
		const auto v4 = workingParam(m, 2, 1);
		std::printf("  track 3 param 1: %u, +1 step %u, +5 burst %u, -5 paced %u, +30 burst %u\n", v0, v1, v2, v3, v4);

		// Page: press SYNTHESIS/EFFECTS/ROUTING three times.
		std::vector<Bytes> snaps{m.snapshotRam()};
		std::vector<int> moved;
		for(int k = 0; k < 3; ++k)
		{
			m.panel(md::PanelControl::SynthesisEffectsRouting);
			m.run(100);
			snaps.push_back(m.snapshotRam());
			const auto before = workingImage(m);
			m.hardware().trySendPanelEvent(0x30, 0x01);
			m.run(40);
			const auto after = workingImage(m);
			int which = -1;
			for(int i = 0; i < 24; ++i)
				if(before[ed::g_mdWorkingKitParamsOffset + 2 * 24 + i] != after[ed::g_mdWorkingKitParamsOffset + 2 * 24 + i])
					which = i;
			moved.push_back(which);
			m.run(100);
			snaps.back() = m.snapshotRam();
		}
		std::printf("  DATA ENTRY A after each page press moves param: %d %d %d\n", moved[0], moved[1], moved[2]);
		std::printf("  page byte candidates (period 3):");
		for(uint32_t a = 0; a < snaps[0].size(); ++a)
		{
			const auto x0 = snaps[0][a], x1 = snaps[1][a], x2 = snaps[2][a], x3 = snaps[3][a];
			if(x0 == x3 && x0 != x1 && x1 != x2 && x0 != x2)
				std::printf(" %06x=%u,%u,%u", a + 0x200000, x0, x1, x2);
		}
		std::printf("\n");

		// Live-recording flag: stopped, playing, recording, playing again (PLAY exits
		// recording), stopped; plus grid edit (RECORD alone).
		std::map<std::string, Bytes> st;
		st["stop"] = m.snapshotRam();
		m.panel(md::PanelControl::Play);
		m.run(300);
		st["play"] = m.snapshotRam();
		rawPanel(m, 0x22, 0x02);
		rawPanel(m, 0x22, 0x06);
		rawPanel(m, 0x22, 0x02);
		rawPanel(m, 0x22, 0x00);
		m.run(300);
		st["recFromPlay"] = m.snapshotRam();
		std::printf("  REC+PLAY while playing: playing %d\n", m.read8(0x28cdaf) == 0);
		m.panel(md::PanelControl::Play);
		m.run(300);
		st["play2"] = m.snapshotRam();
		std::printf("  PLAY while recording: playing %d\n", m.read8(0x28cdaf) == 0);
		m.panel(md::PanelControl::Stop);
		m.run(300);
		st["stop2"] = m.snapshotRam();
		rawPanel(m, 0x22, 0x02);
		rawPanel(m, 0x22, 0x06);
		rawPanel(m, 0x22, 0x02);
		rawPanel(m, 0x22, 0x00);
		m.run(300);
		st["rec"] = m.snapshotRam();
		m.panel(md::PanelControl::Stop);
		m.run(300);
		st["stop3"] = m.snapshotRam();
		m.panel(md::PanelControl::Record);
		m.run(300);
		st["grid"] = m.snapshotRam();
		m.panel(md::PanelControl::Record);
		m.run(300);
		st["stop4"] = m.snapshotRam();
		std::printf("  live-recording flag candidates (stop/play/play2/stop2/stop3/grid/stop4 equal, rec+recFromPlay equal and different):\n   ");
		int shown = 0;
		for(uint32_t a = 0; a < st["stop"].size(); ++a)
		{
			const auto n = st["stop"][a];
			if(st["play"][a] != n || st["play2"][a] != n || st["stop2"][a] != n || st["stop3"][a] != n || st["grid"][a] != n
				|| st["stop4"][a] != n)
				continue;
			const auto r = st["rec"][a];
			if(r == n)
				continue;
			if(shown++ < 20)
				std::printf(" %06x=%02x/%02x(fromPlay %02x grid %02x)", a + 0x200000, n, r, st["recFromPlay"][a], st["grid"][a]);
		}
		std::printf(" (%d)\n", shown);
		for(const auto& [k, v] : st)
			std::printf("  %s: 27f976=%02x 27f977=%02x 2818e1=%02x\n", k.c_str(), v[0x7f976], v[0x7f977], v[0x818e1]);
		// Transitions, with the two mode bytes after each.
		const auto show = [&](const char* _what)
		{
			m.run(250);
			std::printf("  %-28s 27f976=%02x 27f977=%02x playing %d\n", _what, m.read8(0x27f976), m.read8(0x27f977),
				m.read8(0x28cdaf) == 0);
		};
		show("stopped");
		rawPanel(m, 0x22, 0x02); rawPanel(m, 0x22, 0x06); rawPanel(m, 0x22, 0x02); rawPanel(m, 0x22, 0x00);
		show("REC+PLAY (live recording)");
		m.panel(md::PanelControl::Play);
		show("PLAY (exit rec, keep playing)");
		m.panel(md::PanelControl::Record);
		show("RECORD while playing (grid)");
		m.panel(md::PanelControl::Record);
		show("RECORD again");
		m.panel(md::PanelControl::Stop);
		show("STOP");
		m.panel(md::PanelControl::Record);
		show("RECORD stopped (grid)");
		m.panel(md::PanelControl::Play);
		show("PLAY in grid");
		m.panel(md::PanelControl::Stop);
		show("STOP in grid");
		m.panel(md::PanelControl::Record);
		show("RECORD (exit grid)");
		rawPanel(m, 0x22, 0x02); rawPanel(m, 0x22, 0x06); rawPanel(m, 0x22, 0x02); rawPanel(m, 0x22, 0x00);
		show("REC+PLAY again");
		m.panel(md::PanelControl::Stop);
		show("STOP while recording");
		m.panel(md::PanelControl::SynthesisEffectsRouting);
		show("page key");
		rawPanel(m, 0x22, 0x02); rawPanel(m, 0x22, 0x06); rawPanel(m, 0x22, 0x02); rawPanel(m, 0x22, 0x00);
		show("REC+PLAY on effects page");
		m.panel(md::PanelControl::Stop);
		show("STOP");
		std::printf("  grid-edit candidates:");
		shown = 0;
		for(uint32_t a = 0; a < st["stop"].size(); ++a)
		{
			const auto n = st["stop3"][a];
			if(st["stop4"][a] != n || st["grid"][a] == n || st["stop"][a] != n)
				continue;
			if(shown++ < 12)
				std::printf(" %06x=%02x/%02x(rec %02x)", a + 0x200000, n, st["grid"][a], st["rec"][a]);
		}
		std::printf(" (%d)\n", shown);
	}

	// ---- UW samples: names, memory, SDS ----

	void printFound(const char* _what, const Bytes& _needle, Machine& _m)
	{
		printAt(_what, "main RAM", findAll(_m.snapshotRam(), _needle), 0x200000);
		printAt(_what, "patch RAM", findAll(patchRam(_m), _needle), 0);
		printAt(_what, "flash", findAll(_m.hardware().copyFlashData(), _needle), 0);
	}

	void samples(const Bytes& _rom, const Bytes& _patch)
	{
		std::puts("== probe: UW sample names (0x73 set name), sample memory, SDS dump request");
		Machine m(_rom, g_romName, _patch);
		const Bytes name{0xf0, 0, 0x20, 0x3c, 2, 0, 0x73, 5, 'Z', 'Q', 'X', 'W', 0xf7};
		m.send(name);
		m.run(300);
		printFound("\"ZQXW\" after set name slot 6", {'Z', 'Q', 'X', 'W'}, m);
		const auto dumpRegion = [&](const uint32_t _from, const uint32_t _len)
		{
			for(uint32_t a = _from; a < _from + _len; a += 32)
			{
				std::printf("    %06x:", a);
				for(uint32_t i = 0; i < 32; ++i)
					std::printf(" %02x", m.read8(a + i));
				std::printf("  ");
				for(uint32_t i = 0; i < 32; ++i)
				{
					const auto c = m.read8(a + i);
					std::printf("%c", c >= 32 && c < 127 ? c : '.');
				}
				std::printf("\n");
			}
		};

		// An SDS sample into slot 3 through the plug-in's own paced SysEx transfer.
		const auto before = findAll(m.snapshotRam(), {'T', 'E', 'S', 'T'});
		const auto flashBefore = m.hardware().copyFlashData();
		const auto beforeFlash = findAll(m.hardware().copyFlashData(), {'T', 'E', 'S', 'T'});
		const auto sds = md::test::sdsSample(4097, 12, 0, 2);
		auto prepared = md::prepareMidiSysexTransfer(sds);
		require(prepared && m.hardware().startMidiSysexTransfer(*prepared), "SDS transfer start");
		const auto t0 = m.now();
		for(int i = 0; i < 20000; ++i)
		{
			m.step();
			const auto st = m.hardware().getMidiSysexTransferProgress().state;
			if(st == md::MidiSysexTransferState::Complete || st == md::MidiSysexTransferState::Failed
				|| st == md::MidiSysexTransferState::Cancelled)
				break;
		}
		std::printf("  SDS import state %d after %.1f ms\n", int(m.hardware().getMidiSysexTransferProgress().state),
			ms(m.now() - t0));
		const auto flash0 = m.hardware().copyFlashData();
		m.run(10000);
		{
			const auto flash1 = m.hardware().copyFlashData();
			size_t changed = 0, first = SIZE_MAX, last = 0;
			for(size_t i = 0; i < flash1.size() && i < flash0.size(); ++i)
				if(flash1[i] != flash0[i])
				{
					++changed;
					first = std::min(first, i);
					last = i;
				}
			std::printf("  flash bytes changed in the 10 s after import: %zu (0x%zx..0x%zx)\n", changed, first, last);
		}
		const auto afterRam = findAll(m.snapshotRam(), {'T', 'E', 'S', 'T'});
		const auto afterFlash = findAll(m.hardware().copyFlashData(), {'T', 'E', 'S', 'T'});
		std::printf("  new \"TEST\" in main RAM:");
		for(const auto a : afterRam)
			if(std::find(before.begin(), before.end(), a) == before.end())
			{
				std::printf(" %06zx", a + 0x200000);
				dumpRegion(static_cast<uint32_t>(a + 0x200000 - 0x40) & ~31u, 0xa0);
			}
		{
			const auto flash1 = m.hardware().copyFlashData();
			std::vector<std::pair<size_t, size_t>> runs;
			for(size_t i = 0; i < flash1.size(); ++i)
				if(flash1[i] != flashBefore[i])
				{
					if(!runs.empty() && i - runs.back().second < 64)
						runs.back().second = i;
					else
						runs.push_back({i, i});
				}
			std::printf("  flash changed since before the import: %zu runs\n", runs.size());
			for(size_t r = 0; r < runs.size() && r < 12; ++r)
			{
				std::printf("    0x%06zx..0x%06zx:", runs[r].first, runs[r].second);
				for(size_t i = runs[r].first; i <= runs[r].second && i < runs[r].first + 48; ++i)
					std::printf(" %02x", flash1[i]);
				std::printf("\n");
			}
		}
		std::printf("\n  new \"TEST\" in flash:");
		for(const auto a : afterFlash)
			if(std::find(beforeFlash.begin(), beforeFlash.end(), a) == beforeFlash.end())
				std::printf(" %06zx", a);
		std::printf("\n");
		// SDS dump request (MIDI SDS standard, F0 7E dd 03 ss ss F7) for slots 3 and 6.
		std::vector<Bytes> replies;
		m.onSysex = [&](const Bytes& _b) { replies.push_back(_b); };
		for(const uint8_t slot : {uint8_t(2), uint8_t(5), uint8_t(0)})
		{
			replies.clear();
			m.send({0xf0, 0x7e, 0x00, 0x03, slot, 0x00, 0xf7});
			m.run(1500);
			size_t bytes = 0;
			for(const auto& r : replies)
				bytes += r.size();
			std::printf("  SDS dump request slot %u: %zu replies, %zu bytes", slot + 1, replies.size(), bytes);
			if(!replies.empty())
				std::printf(", first %02x %02x %02x %02x %02x", replies[0][0], replies[0][1], replies[0][2],
					replies[0].size() > 3 ? replies[0][3] : 0, replies[0].size() > 4 ? replies[0][4] : 0);
			std::printf("\n");
		}
		m.onSysex = nullptr;
	}

	// A playing flag that holds after STOP pressed twice, PLAY-pause, and in live
	// recording: bytes equal in every "moving" state and different in every "still" one.
	void playingFlag(const Bytes& _rom)
	{
		std::puts("== probe: RAM flags for 'moving' and 'live recording'");
		Machine m(_rom, g_romName);
		struct State { bool moving; bool live; std::vector<Bytes> ram; };
		std::vector<State> states;
		const auto recPlay = [&] { rawPanel(m, 0x22, 0x02); rawPanel(m, 0x22, 0x06); rawPanel(m, 0x22, 0x02); rawPanel(m, 0x22, 0x00); };
		const auto grab = [&](const bool _moving, const bool _live, const char* _what)
		{
			m.run(200);
			State s{_moving, _live, {}};
			const auto s0 = m.playhead();
			for(int i = 0; i < 4; ++i)
			{
				m.run(90);
				s.ram.push_back(m.snapshotRam());
			}
			bool on = false, off = false;
			for(int i = 0; i < 60; ++i)
			{
				m.run(20);
				((m.read8(0x27f977) & 0x10) ? on : off) = true;
			}
			std::printf("  %-26s step %u -> %u, REC LED %s%s\n", _what, s0, m.playhead(), on && off ? "blinks" : on ? "on" : "off",
				(on && off) != _live ? "  <-- label wrong" : "");
			states.push_back(std::move(s));
		};
		grab(false, false, "boot");
		m.panel(md::PanelControl::Stop);
		grab(false, false, "STOP while stopped");
		m.panel(md::PanelControl::Play);
		grab(true, false, "PLAY");
		m.panel(md::PanelControl::Play);
		grab(false, false, "PLAY again (pause)");
		m.panel(md::PanelControl::Play);
		grab(true, false, "PLAY (resume)");
		m.panel(md::PanelControl::Stop);
		grab(false, false, "STOP");
		recPlay();
		grab(true, true, "REC+PLAY from STOP");
		m.panel(md::PanelControl::Play);
		grab(true, false, "PLAY (leave rec)");
		m.panel(md::PanelControl::Stop);
		m.panel(md::PanelControl::Stop);
		grab(false, false, "STOP STOP");
		recPlay();
		grab(true, true, "REC+PLAY from STOP STOP");
		m.panel(md::PanelControl::Stop);
		grab(false, false, "STOP (from rec)");
		m.panel(md::PanelControl::Record);
		grab(false, false, "grid");
		m.panel(md::PanelControl::Play);
		grab(true, false, "grid + PLAY");
		m.panel(md::PanelControl::Stop);
		m.panel(md::PanelControl::Record);
		grab(false, false, "STOP, grid off");
		const auto find = [&](const char* _name, auto _key)
		{
			std::printf("  %s candidates:", _name);
			int n = 0;
			for(size_t a = 0; a < states[0].ram[0].size(); ++a)
			{
				std::optional<uint8_t> on, off;
				bool ok = true;
				for(const auto& s : states)
					for(const auto& ram : s.ram)
					{
						auto& want = _key(s) ? on : off;
						if(!want)
							want = ram[a];
						ok &= *want == ram[a];
					}
				if(ok && on && off && on != off && n++ < 16)
					std::printf(" %06zx=%02x/%02x", a + 0x200000, *off, *on);
			}
			std::printf(" (%d)\n", n);
		};
		// Time series: 1.2 s of samples every 20 ms of the two LED-ish bytes.
		const auto series = [&](const char* _what)
		{
			std::string a, b, c;
			for(int i = 0; i < 60; ++i)
			{
				m.run(20);
				a += (m.read8(0x27f977) & 0x10) ? '#' : '.';
				b += (m.read8(0x25ac91) & 0x04) ? '#' : '.';
				c += m.read8(0x28cdaf) ? '#' : '.';
			}
			std::printf("  %-18s REC-LED %s\n  %-18s PLAY    %s\n  %-18s 28cdaf  %s\n", _what, a.c_str(), "", b.c_str(), "", c.c_str());
		};
		m.panel(md::PanelControl::Stop);
		series("stopped");
		m.panel(md::PanelControl::Play);
		series("playing");
		m.panel(md::PanelControl::Play);
		series("paused");
		m.panel(md::PanelControl::Stop);
		recPlay();
		series("live rec");
		m.panel(md::PanelControl::Play);
		series("left rec, playing");
		m.panel(md::PanelControl::Stop);
		m.panel(md::PanelControl::Record);
		series("grid");
		m.panel(md::PanelControl::Play);
		series("grid + playing");
		m.panel(md::PanelControl::Stop);
		m.panel(md::PanelControl::Record);
		// How long must RECORD be held before PLAY? Five tries per timing.
		for(const double hold : {20.0, 40.0, 80.0, 150.0})
		{
			int ok = 0;
			for(int i = 0; i < 5; ++i)
			{
				m.panel(md::PanelControl::Stop);
				m.run(100 + 37 * i);
				rawPanel(m, 0x22, 0x02, hold);
				rawPanel(m, 0x22, 0x06, hold);
				rawPanel(m, 0x22, 0x02, hold);
				rawPanel(m, 0x22, 0x00, hold);
				m.run(200);
				ok += (m.read8(0x27f977) & 0x10) != 0;
			}
			std::printf("  hold %.0f ms: live recording %d of 5\n", hold, ok);
		}
		find("moving", [](const State& _s) { return _s.moving; });
		find("live recording", [](const State& _s) { return _s.live; });
	}
}

int main(const int _argc, char** _argv)
{
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	if(_argc < 2)
	{
		std::puts("usage: mdEditorProbeFirmwareTest <ROM> [workkit|liverec|samples|sds]");
		return 77;
	}
	try
	{
		g_romName = _argv[1];
		const auto rom = load(_argv[1]);
		require(rom.size() == md::g_romSize, "ROM must be the 8 MiB MD 1.63 image");
		const std::string only = _argc > 2 ? _argv[2] : "";
		if(only.empty() || only == "workkit")
			workingKit(rom);
		if(only.empty() || only == "liverec")
			liveRecording(rom);
		if(only.empty() || only == "samples")
			samples(rom, _argc > 3 ? patchRamFromState(_argv[3], rom) : Bytes{});
		if(only.empty() || only == "playing")
			playingFlag(rom);
		if(only.empty() || only == "recknobs")
			recordKnobs(rom);
		if(only.empty() || only == "liverec" || only == "livelocks")
			liveLocks(rom);
		std::printf("%s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
		return g_failures ? 1 : 0;
	}
	catch(const std::exception& _e)
	{
		std::fprintf(stderr, "mdEditorProbeFirmwareTest FAIL: %s\n", _e.what());
		return 1;
	}
}
