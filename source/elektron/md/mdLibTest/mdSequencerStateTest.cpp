// md::SequencerState and md::panelKeySequence without firmware (P3).
#include "mdLib/mdpanel.h"
#include "mdLib/mdsequencerstate.h"

#include <cstdio>

namespace
{
	int g_failures = 0;
	void check(const bool _c, const char* _what)
	{
		if(!_c)
		{
			std::fprintf(stderr, "FAIL: %s\n", _what);
			++g_failures;
		}
	}
	constexpr uint64_t ms(const uint64_t _ms) { return _ms * md::g_samplerate / 1000; }
}

int main()
{
	md::SequencerState s;
	s.update(0, 1, 0x06, ms(10));
	check(!s.playing() && !s.recording() && !s.gridEdit(), "boot: stopped");
	// Playing: the stopped byte is 0 and the step moves.
	s.update(1, 0, 0x06, ms(120));
	check(s.playing() && !s.recording(), "a moving step with the stopped byte clear plays");
	// Pause (or STOP pressed twice): the byte stays 0 but the step stops moving.
	s.update(1, 0, 0x06, ms(400));
	check(s.playing(), "still playing 0.4 s after the last step (slow tempo)");
	s.update(1, 0, 0x06, ms(400));
	check(!s.playing(), "stopped 0.8 s after the last step");
	// STOP from playing: the byte says so at once.
	s.update(2, 0, 0x06, ms(100));
	s.update(3, 1, 0x06, ms(10));
	check(!s.playing(), "the stopped byte stops at once");
	// Live recording: the RECORD LED blinks while playing.
	s.update(4, 0, 0x16, ms(100));
	check(s.recording() && !s.gridEdit(), "RECORD LED turns on while playing: live recording");
	s.update(5, 0, 0x06, ms(360));
	check(s.recording(), "and blinks off");
	s.update(6, 0, 0x06, ms(700));
	check(!s.recording() && s.playing(), "steady off: recording left, still playing");
	// Grid edit: steady RECORD LED.
	s.update(7, 0, 0x16, ms(100));
	s.update(8, 0, 0x16, ms(700));
	check(!s.recording() && s.gridEdit(), "steady RECORD LED: grid edit");

	const auto rp = md::panelKeySequence(md::MachineModel::Machinedrum, "recordPlay");
	check(rp.size() == 4 && rp[0].mask == 0x02 && rp[1].mask == 0x06 && rp[2].mask == 0x02 && rp[3].mask == 0,
		"recordPlay holds RECORD, presses PLAY, releases both");
	const auto t16 = md::panelKeySequence(md::MachineModel::Machinedrum, "trig16");
	check(t16.size() == 2 && t16[0].row == 0x21 && t16[0].mask == 0x80 && t16[1].mask == 0, "trig16 is TRIG key 16");
	check(md::panelKeySequence(md::MachineModel::Machinedrum, "trig17").empty(), "no TRIG key 17");
	check(md::panelKeySequence(md::MachineModel::Machinedrum, "page").size() == 2, "the page key");
	// P4: a chain holds the bank key and the TRIG keys together, in order.
	const auto ch = md::panelKeySequence(md::MachineModel::Machinedrum, "chain:1:2,9,0");
	check(ch.size() == 7 && ch[0].row == 0x23 && ch[0].mask == 0x02 && ch[1].row == 0x20 && ch[1].mask == 0x04
		&& ch[2].row == 0x21 && ch[2].mask == 0x02 && ch[3].row == 0x20 && ch[3].mask == 0x05
		&& ch[4].mask == 0 && ch[5].mask == 0 && ch[6].row == 0x23 && ch[6].mask == 0,
		"chain:1:2,9,0 holds B/F, adds TRIG 3, 10, 1, releases all");
	check(md::panelKeySequence(md::MachineModel::Machinedrum, "chain:4:1").empty()
		&& md::panelKeySequence(md::MachineModel::Machinedrum, "chain:0:16").empty(), "no bank key 5, no TRIG 17");
	check(md::panelKeySequence(md::MachineModel::Machinedrum, "bankGroup").size() == 2, "BANK GROUP key");
	if(g_failures)
		return 1;
	std::puts("mdSequencerStateTest: PASS");
	return 0;
}
