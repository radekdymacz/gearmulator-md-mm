// deskWire's unit test: the MIDI wire (paced out at DIN speed, whole messages in) and the
// encoders the plug-in's wire engines and the firmware test rigs share.

#include "deskWire.h"
#include "mdWire.h"
#include "mmWire.h"

#include <cstdio>
#include <string>

namespace
{
	int g_failures = 0;

	void check(const bool _ok, const std::string& _what)
	{
		std::printf("  %s %s\n", _ok ? "ok  " : "FAIL", _what.c_str());
		if(!_ok)
			++g_failures;
	}

	using deskWire::Bytes;

	enum class Key : uint8_t { Exit, Play, Stop };

	void pacedOut()
	{
		std::printf("wire: paced out\n");
		std::vector<Bytes> sent;
		std::vector<Bytes> arrived;
		deskWire::MidiWire wire([&](const Bytes& _m) { sent.push_back(_m); }, [&] { return std::exchange(arrived, {}); });
		const auto nothing = [](const Bytes&) {};
		// 3125 bytes a second: a 3-byte CC takes 0.96 ms, a 32-byte SysEx 10.24 ms.
		wire.send(Bytes{0xb0, 1, 2});
		wire.send(Bytes(32, 0xf0));
		wire.send(Bytes{0xfa});
		wire.pump(100.0, nothing);
		check(sent.size() == 1 && sent[0] == Bytes({0xb0, 1, 2}), "the first message goes at once");
		wire.pump(100.5, nothing);
		check(sent.size() == 1, "the next waits while the first is on the wire");
		wire.pump(100.96, nothing);
		check(sent.size() == 2 && sent[1].size() == 32, "then the next");
		wire.pump(111.0, nothing);
		check(sent.size() == 2, "the SysEx holds the wire for 10.24 ms");
		wire.pump(111.2, nothing);
		check(sent.size() == 3 && sent[2] == Bytes({0xfa}) && wire.idle(112.0), "then the last, and the wire is idle");
	}

	void wholeIn()
	{
		std::printf("wire: whole messages in\n");
		std::vector<Bytes> arrived{{0xf0, 0x00, 0x20, 0x3c, 0xf7}, {0xf0, 0x01, 0xf7}};
		deskWire::MidiWire wire([](const Bytes&) {}, [&] { return std::exchange(arrived, {}); });
		std::vector<Bytes> got;
		wire.pump(0, [&](const Bytes& _m) { got.push_back(_m); });
		check(got.size() == 2 && got[0].size() == 5 && got[1] == Bytes({0xf0, 0x01, 0xf7}), "each message whole, in order");
		wire.pump(1, [&](const Bytes& _m) { got.push_back(_m); });
		check(got.size() == 2, "nothing twice");
	}

	void channelMessages()
	{
		std::printf("wire: channel message length\n");
		check(deskWire::channelMessage(0x90, 60, 100) == Bytes({0x90, 60, 100}), "note on is three bytes");
		check(deskWire::channelMessage(0xb3, 1, 2) == Bytes({0xb3, 1, 2}), "CC is three bytes");
		check(deskWire::channelMessage(0xe0, 0, 64) == Bytes({0xe0, 0, 64}), "pitch bend is three bytes");
		check(deskWire::channelMessage(0xc2, 5, 0xff) == Bytes({0xc2, 5}), "program change is two bytes, the third dropped");
		check(deskWire::channelMessage(0xd5, 77, 0xff) == Bytes({0xd5, 77}), "channel pressure is two bytes, the third dropped");
	}

	void realtime()
	{
		std::printf("wire: realtime keys\n");
		check(deskWire::realtimeOf(Key::Play) == deskWire::midi::g_start, "PLAY is Start (0xfa)");
		check(deskWire::realtimeOf(Key::Stop) == deskWire::midi::g_stop, "STOP is Stop (0xfc)");
		check(!deskWire::realtimeOf(Key::Exit), "no other key goes over a wire");
		const auto both = deskWire::realtimeOf(std::vector<Key>{Key::Stop, Key::Play});
		check(both && both->size() == 2 && (*both)[0] == Bytes({0xfc}) && (*both)[1] == Bytes({0xfa}), "keys in order");
		check(!deskWire::realtimeOf(std::vector<Key>{Key::Play, Key::Exit}), "all or none");
		check(deskWire::md::realtimeOf("play") == 0xfa && deskWire::md::realtimeOf("stop") == 0xfc && !deskWire::md::realtimeOf("kit"),
			"the MD's key names");
	}

	void mdEncoders()
	{
		std::printf("wire: Machinedrum CCs\n");
		// MD MIDI implementation: tracks 1-4 on the base channel, CC 16-39 (first lane) for the 24
		// parameters, level CC 8 + lane, mute CC 12 + lane.
		const auto p = deskWire::md::kitParam(0, 0, 0, 64);
		check(p && *p == Bytes({0xb0, 16, 64}), "track 1, parameter 1 on channel 1");
		const auto base = deskWire::md::kitParam(3, 5, 9, 100);
		check(base && (*base)[0] == (0xb0 | 4), "track 6 on the base channel + 1 (base 3: channel index 4)");
		const auto level = deskWire::md::kitParam(2, 2, deskWire::md::g_levelIndex, 90);
		check(level && *level == Bytes({0xb2, 10, 90}), "level: CC 8 + lane on the base channel");
		const auto mute = deskWire::md::mute(1, 7, true);
		check(mute && *mute == Bytes({0xb2, 15, 1}), "mute: CC 12 + lane, 1 = muted");
		check(!deskWire::md::kitParam(14, 15, 0, 1), "no CC past channel 16");
	}

	void mmEncoders()
	{
		std::printf("wire: Monomachine CCs and NRPN\n");
		const auto p = deskWire::mm::param(2, 1, 0, 0, 33);
		check(p && (*p)[0] == (0xb0 | 3) && (*p)[2] == 33, "one channel per track from the base channel");
		const auto n = deskWire::mm::nrpn(5, 3, 17, 99);
		check(n.size() == 3 && n[0] == Bytes({0xb5, 99, 3}) && n[1] == Bytes({0xb5, 98, 17}) && n[2] == Bytes({0xb5, 6, 99}),
			"NRPN: 99 track, 98 parameter, 6 value, on the base channel");
		check(deskWire::controlChange(0x13, 0x80, 0xff) == Bytes({0xb3, 0, 0x7f}), "a CC keeps to 4 and 7 bits");
	}
}

int main()
{
	pacedOut();
	wholeIn();
	channelMessages();
	realtime();
	mdEncoders();
	mmEncoders();
	std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "passed", g_failures);
	return g_failures ? 1 : 0;
}
