#pragma once

#include "elektronData/mdGlobal.h"
#include "elektronData/mdKit.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mdDesk
{
	// How a document change reaches the machine. Patterns and songs travel as
	// whole dumps (the firmware stores them at once). The working kit and the
	// global settings have no working-copy dump - a kit dump writes the stored
	// slot only - so their changes are sent as the small live edits a knob move
	// or the SysEx edit commands make (manual Appendix C). This file only
	// decides which edits; the transport sends them.
	struct LiveEdit
	{
		enum class Kind : uint8_t
		{
			Param,		// kit track parameter 0-23 (CC)
			Level,		// kit track level (CC)
			Machine,	// assign machine: model, re-initialises the synthesis page
			Lfo,		// LFO block field 0-4
			TrigGroup,
			MuteGroup,
			MasterFx,	// track = effect (MdKit::MasterFx), index = parameter 0-7
			KitName,
			Route,		// global: track output
			Tempo,		// global: BPM x 24 in 'model'
			LockMode	// global: CLASSIC / EXTENDED
		};

		Kind kind = Kind::Param;
		uint8_t track = 0;
		uint8_t index = 0;
		uint8_t value = 0;
		uint32_t model = 0;
		std::string name;

		bool operator==(const LiveEdit& _o) const
		{
			return kind == _o.kind && track == _o.track && index == _o.index && value == _o.value && model == _o.model
				&& name == _o.name;
		}
	};

	struct Delivery
	{
		std::vector<LiveEdit> edits;
		// Parts of the change that no live edit can make (for example removing a
		// group, or the opaque LFO state); they reach the machine only through a
		// kit dump + LOAD KIT, which would also save the kit. Listed for the UI.
		std::vector<std::string> notLive;
	};

	// The live edits that turn the working kit _before into _after. A machine
	// change comes first and is followed by all eight synthesis values, because
	// the firmware re-initialises them on assignment.
	Delivery kitDelivery(const elektronData::MdKit& _before, const elektronData::MdKit& _after);

	Delivery globalDelivery(const elektronData::MdGlobal& _before, const elektronData::MdGlobal& _after);

	// SysEx for one live edit; empty for Param and Level (they are CCs, which the
	// plug-in's parameter layer sends) and for a group removal (no live form).
	std::vector<uint8_t> liveEditSysex(const LiveEdit& _edit);
}
