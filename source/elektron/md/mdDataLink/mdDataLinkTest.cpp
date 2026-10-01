// mdDataLink::Session against a scripted fake device: what goes on the wire for
// each intent, and how replies move the observable state. Firmware behaviour
// itself is measured by mdLibTest/mdDataLayerFirmwareTest.

#include "mdDataLink.h"

#include "elektronData/mdValidate.h"

#include <cstdio>

namespace
{
	namespace ed = elektronData;
	using Session = mdDataLink::Session;
	using Bytes = Session::Bytes;

	int g_failures = 0;

	void check(const bool _condition, const char* _what)
	{
		if(_condition)
			return;
		std::fprintf(stderr, "FAIL: %s\n", _what);
		++g_failures;
	}

	Bytes statusReply(const ed::MdStatus _param, const uint8_t _value)
	{
		return {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x72, static_cast<uint8_t>(_param), _value, 0xf7};
	}

	struct Wire
	{
		std::vector<Bytes> sent;
		Session session{[this](const Bytes& _b) { sent.push_back(_b); }};

		uint8_t command(const size_t _i) const { return sent.at(_i).at(6); }
	};

	void testRequests()
	{
		Wire w;
		w.session.requestStatus();
		check(w.sent.size() == 7, "status request covers all seven parameters");
		w.sent.clear();
		w.session.requestKit(3);
		w.session.requestSong(4);
		w.session.requestGlobal(1);
		w.session.requestPattern(9);
		check(w.command(0) == 0x53 && w.sent[0][7] == 3, "kit request");
		check(w.command(1) == 0x6a && w.sent[1][7] == 4, "song request");
		check(w.command(2) == 0x51 && w.sent[2][7] == 1, "global request");
		check(w.command(3) == 0x68 && w.sent[3][7] == 9, "pattern request");
	}

	void testStatusAndQueue()
	{
		Wire w;
		int notified = 0;
		w.session.onState = [&](const Session::State&) { ++notified; };
		w.session.onSysex(statusReply(ed::MdStatus::Pattern, 0));
		w.session.onSysex(statusReply(ed::MdStatus::Kit, 0));
		w.session.onSysex(statusReply(ed::MdStatus::LockMode, 1));
		w.session.onSysex(statusReply(ed::MdStatus::SequencerMode, 0));
		const auto& s = w.session.state();
		check(s.pattern == uint8_t{0} && s.kit == uint8_t{0} && s.extendedMode == true && s.songMode == false,
			"status replies land in the state");
		check(notified == 4, "each change notifies once");

		w.sent.clear();
		w.session.selectPattern(5);
		check(w.command(0) == 0x57 && w.sent[0][7] == 5, "select sends LOAD PATTERN");
		check(s.queuedPattern == uint8_t{5}, "selected pattern is queued");
		w.session.onSysex(statusReply(ed::MdStatus::Pattern, 0));
		check(s.queuedPattern == uint8_t{5}, "still queued while the old pattern plays");
		w.session.onSysex(statusReply(ed::MdStatus::Pattern, 5));
		check(!s.queuedPattern && s.pattern == uint8_t{5}, "queue clears when the firmware reports the switch");

		// A chain made on the panel replaces a pick still waiting for the pattern end.
		w.session.selectPattern(7);
		check(s.queuedPattern == uint8_t{7}, "picked while playing: queued");
		w.sent.clear();
		w.session.noteChained();
		check(!s.queuedPattern, "a chain drops the queued pick: the chain plays next");
		check(w.sent.size() == 3 && w.command(0) == 0x70 && w.sent[2][7] == static_cast<uint8_t>(ed::MdStatus::SequencerMode),
			"and asks for the pattern, kit and sequencer mode again");
	}

	void testKitLifecycle()
	{
		Wire w;
		const auto& s = w.session.state();
		w.session.onSysex(statusReply(ed::MdStatus::Kit, 0));
		w.session.onSysex(statusReply(ed::MdStatus::LockMode, 1));

		ed::MdPattern linked;
		linked.position = 1;
		linked.kit = 5;
		ed::MdPattern same;
		same.position = 2;
		same.kit = 0;
		w.session.onSysex(ed::encodeMdPattern(linked));
		w.session.onSysex(ed::encodeMdPattern(same));
		check(s.patternKits.at(1) == 5 && s.patternKits.at(2) == 0, "pattern dumps record their kit link");

		check(!w.session.selectWouldDiscardKitEdits(1), "no warning without unsaved edits");
		w.session.noteWorkingKitEdited();
		check(s.workingKit == Session::WorkingKit::Edited, "live edits mark the working kit");
		check(w.session.selectWouldDiscardKitEdits(1), "pattern on another kit would discard edits");
		check(!w.session.selectWouldDiscardKitEdits(2), "pattern on the same kit keeps edits");
		check(!w.session.selectWouldDiscardKitEdits(7), "unknown link: no claim");
		w.session.onSysex(statusReply(ed::MdStatus::LockMode, 0));
		check(!w.session.selectWouldDiscardKitEdits(1), "CLASSIC mode does not link kits");
		w.session.onSysex(statusReply(ed::MdStatus::LockMode, 1));

		// Firmware switched to the linked kit: its stored copy is now playing.
		w.session.onSysex(statusReply(ed::MdStatus::Kit, 5));
		check(s.workingKit == Session::WorkingKit::Clean && s.kit == uint8_t{5}, "linked switch loads a clean kit");

		ed::MdKit kit;
		kit.position = 5;
		w.sent.clear();
		check(w.session.pushKit(kit, Session::KitApply::Store).empty(), "valid kit is pushed");
		check(w.sent.size() == 2 && w.command(0) == 0x52 && w.command(1) == 0x53, "store = dump + read-back");
		check(s.workingKit == Session::WorkingKit::Edited, "storing over the playing kit's slot: working differs");

		w.sent.clear();
		check(w.session.pushKit(kit, Session::KitApply::StoreAndLoad).empty(), "valid kit is pushed and loaded");
		check(w.sent.size() == 3 && w.command(0) == 0x52 && w.command(1) == 0x58 && w.command(2) == 0x53,
			"store and load = dump + LOAD KIT + read-back");
		check(s.workingKit == Session::WorkingKit::Clean, "loaded kit is clean");

		w.session.noteWorkingKitEdited();
		w.session.saveKit(9);
		check(s.kit == uint8_t{9} && s.workingKit == Session::WorkingKit::Clean, "SAVE KIT n makes n current");

		auto bad = kit;
		bad.models[3] = 90;
		w.sent.clear();
		check(!w.session.pushKit(bad, Session::KitApply::StoreAndLoad).empty() && w.sent.empty(),
			"invalid kit is refused before anything is sent");
	}

	void testSongAndValues()
	{
		Wire w;
		const auto& s = w.session.state();
		w.session.onSysex(statusReply(ed::MdStatus::Song, 3));
		ed::MdSong song;
		song.position = 3;
		check(w.session.pushSong(song).empty(), "valid song is pushed");
		check(s.songReloadNeeded, "song pushed into the current slot needs a reload");
		w.session.loadSong(3);
		check(!s.songReloadNeeded && w.sent.back()[6] == 0x6c, "LOAD SONG clears it");
		song.position = 4;
		w.session.pushSong(song);
		check(!s.songReloadNeeded, "another slot needs no reload");

		ed::MdSong noEnd;
		noEnd.rows[0].pattern = 0;
		check(!w.session.pushSong(noEnd).empty(), "song without END is refused");

		int kits = 0, songs = 0, globals = 0, patterns = 0;
		w.session.onKit = [&](const ed::MdKit& _k) { kits += _k.position == 7; };
		w.session.onSong = [&](const ed::MdSong& _s) { songs += _s.position == 2; };
		w.session.onGlobal = [&](const ed::MdGlobal& _g) { globals += _g.position == 1; };
		w.session.onPattern = [&](const ed::MdPattern& _p) { patterns += _p.position == 8; };
		ed::MdKit k;
		k.position = 7;
		ed::MdSong so;
		so.position = 2;
		ed::MdGlobal g;
		g.position = 1;
		ed::MdPattern p;
		p.position = 8;
		w.session.onSysex(ed::encodeMdKit(k));
		w.session.onSysex(ed::encodeMdSong(so));
		w.session.onSysex(ed::encodeMdGlobal(g));
		w.session.onSysex(ed::encodeMdPattern(p));
		auto corrupt = ed::encodeMdKit(k);
		corrupt[40] ^= 1;
		w.session.onSysex(corrupt);
		check(kits == 1 && songs == 1 && globals == 1 && patterns == 1, "dumps arrive as values; corrupt ones do not");
	}

	void testStateJson()
	{
		Wire w;
		w.session.onSysex(statusReply(ed::MdStatus::Pattern, 3));
		w.session.selectPattern(4);
		const auto text = ed::json::write(Session::stateToJson(w.session.state()));
		check(text.find("\"pattern\":{\"current\":3,\"queued\":4}") != std::string::npos, "machine document");
		check(text.find("\"working\":\"unknown\"") != std::string::npos, "working kit unknown at start");
	}
}

int main()
{
	testStateJson();
	testRequests();
	testStatusAndQueue();
	testKitLifecycle();
	testSongAndValues();
	std::printf("mdDataLinkTest: %s\n", g_failures ? "FAIL" : "PASS");
	return g_failures ? 1 : 0;
}
