// Unit test for mdDesk::wirePort and mmDesk::wirePort (P6): the plug-in's wire engines
// (mdJucePlugin/mdSessionMd.cpp, mdSessionMm.cpp) and the firmware test rigs
// (mdLibTest/mdDeskFirmwareTest.cpp's HwRig) build a DevicePort from these two functions, so a
// wrong byte here is a wrong byte on the wire everywhere. Pure: no ROM, no emulator.

#include "mdDesk/mdDeskWirePort.h"
#include "mmDesk/mmDeskWirePort.h"

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

	using Bytes = std::vector<uint8_t>;

	// A wire that only records what it was sent, draining the pacer (DIN speed) a long way
	// forward each time so a queued message is always due; the clock keeps moving forward
	// (DinPacer::take only releases what is due by _nowMs).
	struct Recorder
	{
		std::vector<Bytes> sent;
		double now = 0;
		deskWire::MidiWire wire{[this](const Bytes& _b) { sent.push_back(_b); }, [] { return std::vector<Bytes>{}; }};

		// DinPacer::take() releases only what is due in one go (each item pushes the "wire free"
		// point on by its own send time), so draining a multi-message send (NRPN's three CCs)
		// needs the clock walked forward in several small hops, not one big jump.
		void flush()
		{
			for(int i = 0; i < 32; ++i)
			{
				now += 10.0;
				wire.pump(now, [](const Bytes&) {});
			}
		}
	};

	void mdWirePort()
	{
		std::printf("mdDeskWirePort: bytes on the base channel\n");
		Recorder r;
		uint8_t channel = 0;
		auto port = mdDesk::wirePort(r.wire, channel, [] { return 0.0; });
		port.baseChannel(3);

		port.sendKitParam(0, 0, 64);
		r.flush();
		check(!r.sent.empty() && r.sent.back() == Bytes({static_cast<uint8_t>(0xb0 | 3), 16, 64}),
			"kit param: track 1, parameter 1, on the base channel");

		// The channel is the base channel plus track/4 (four tracks per MIDI channel); the lane
		// (track & 3) is in the CC number instead.
		port.sendKitParam(2, deskWire::md::g_levelIndex, 90);
		r.flush();
		check(!r.sent.empty() && r.sent.back() == Bytes({static_cast<uint8_t>(0xb0 | 3), 10, 90}), "level: CC 8 + lane, on the base channel");

		port.sendMute(5, true);
		r.flush();
		check(!r.sent.empty() && r.sent.back() == Bytes({static_cast<uint8_t>(0xb0 | (3 + 1)), 13, 1}),
			"mute: CC 12 + lane, channel follows track/4, 1 = muted");

		const bool played = port.pressKey("play");
		r.flush();
		check(played && !r.sent.empty() && r.sent.back() == Bytes({0xfa}), "PLAY is MIDI Start (0xfa) on the wire");
		const bool stopped = port.pressKey("stop");
		r.flush();
		check(stopped && !r.sent.empty() && r.sent.back() == Bytes({0xfc}), "STOP is MIDI Stop (0xfc) on the wire");
		check(!port.pressKey("record"), "a key with no wire equivalent presses nothing");
	}

	void mmWirePort()
	{
		std::printf("mmDeskWirePort: bytes on the base channel\n");
		Recorder r;
		uint8_t channel = 0;
		auto port = mmDesk::wirePort(r.wire, channel, [] { return 0.0; });
		port.baseChannel(5);

		port.sendParam(1, 0, 0, 33);
		r.flush();
		check(!r.sent.empty() && r.sent.back()[0] == static_cast<uint8_t>(0xb0 | (5 + 1)) && r.sent.back()[2] == 33,
			"a parameter: one channel per track from the base channel");

		port.sendNrpn(3, 17, 99);
		r.flush();
		check(r.sent.size() >= 3, "NRPN is three control changes");
		const auto nrpn = std::vector<Bytes>(r.sent.end() - 3, r.sent.end());
		check(nrpn[0] == Bytes({static_cast<uint8_t>(0xb0 | 5), 99, 3}) && nrpn[1] == Bytes({static_cast<uint8_t>(0xb0 | 5), 98, 17})
			&& nrpn[2] == Bytes({static_cast<uint8_t>(0xb0 | 5), 6, 99}), "NRPN: 99 track, 98 parameter, 6 value, on the base channel");

		const bool pressed = port.pressKeys({mmDesk::Key::Play, mmDesk::Key::Stop});
		r.flush();
		check(pressed && r.sent.size() >= 2 && *(r.sent.end() - 2) == Bytes({0xfa}) && r.sent.back() == Bytes({0xfc}),
			"PLAY then STOP as MIDI Start/Stop, in order");
	}
}

int main()
{
	mdWirePort();
	mmWirePort();
	std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "passed", g_failures);
	return g_failures ? 1 : 0;
}
