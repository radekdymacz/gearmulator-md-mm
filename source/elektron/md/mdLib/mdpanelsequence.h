#pragma once

#include <string>
#include <vector>

#include "mdpanel.h"

namespace md
{
	// A named key press as the panel row states to send, each held for a moment,
	// ending with the release. Names (MD, the Machinedrum Editor's vocabulary):
	// "play", "stop", "record", "page" (SYNTHESIS/EFFECTS/ROUTING), "trig1".."trig16",
	// "recordPlay": hold RECORD, press PLAY (live recording), "bankGroup" (A-D / E-H), and
	// "chain:<k>:<t>,<t>,..." (P4): hold bank key k (0-3 = A/E..D/H), press the TRIG keys
	// t (0-15) one after another while holding each (they end up held together, which is
	// what makes a chain; pressed and released one by one they only select), release all.
	// "hold:function" and "release:function": FUNCTION down, and up again later (Control All, manual
	// p.37: FUNCTION held while a DATA ENTRY knob turns moves that knob on every track).
	// Empty if unknown.
	std::vector<PanelPacket> panelKeySequence(MachineModel _model, const std::string& _key);

}
