#pragma once

#include "elektronData/json.h"
#include "elektronData/mdCommands.h"
#include "elektronData/mdGlobal.h"
#include "elektronData/mdKit.h"
#include "elektronData/mdPattern.h"
#include "elektronData/mdSong.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mdDataLink
{
	// Transport glue between MD Desk and a Machinedrum (emulated or real): it
	// turns value-level intents into the firmware's SysEx and firmware replies
	// back into values. Bytes exist only at the device edge (Send / onSysex);
	// the UI side sees elektronData values and the plain State below.
	//
	// What the MD OS 1.63 firmware does, measured in P1 (doc/modern-ux/P1-RESULT.md):
	// - Every dump is stored exactly as sent, with no validation: validate first.
	// - Pattern dump: takes effect at once, also for the playing pattern.
	// - Kit dump: writes the stored slot only. The sound changes on LOAD KIT, which
	//   replaces the working kit and discards its unsaved edits.
	// - Song dump: writes the stored slot. The playing song ignores it, and LOAD
	//   SONG while playing is ignored too: stop, load, play.
	// - LOAD PATTERN while playing is queued until the current pattern ends; the
	//   status reply switches about two steps before the audible change.
	// - EXTENDED mode: switching to a pattern linked to another kit loads that kit
	//   and discards unsaved working-kit edits. Same kit: edits survive.
	// - SAVE KIT n stores the working kit in slot n and makes n the current kit.
	class Session
	{
	public:
		using Bytes = std::vector<uint8_t>;
		using Send = std::function<void(const Bytes&)>;

		enum class KitApply
		{
			Store,			// write the slot; the working (playing) kit is untouched
			StoreAndLoad	// write the slot, then LOAD KIT: heard at once, unsaved edits lost
		};

		// The working (playing) kit relative to its stored slot.
		enum class WorkingKit
		{
			Unknown,		// nothing observed yet
			Clean,			// equals the stored slot (after load, save or a linked switch)
			Edited			// differs from the stored slot: live edits (lost on LOAD KIT or a
							// switch to another kit) or a Store push to the playing kit's slot
		};

		struct State
		{
			// Status replies (manual Appendix C); empty until first reported.
			std::optional<uint8_t> globalSlot, kit, pattern, song, track;
			std::optional<bool> songMode, extendedMode;

			// Pattern requested with selectPattern and not yet reported as current.
			std::optional<uint8_t> queuedPattern;
			WorkingKit workingKit = WorkingKit::Unknown;
			// A song was pushed into the current song's slot: heard after stop + reload.
			bool songReloadNeeded = false;

			// Kit link of every pattern seen in a dump (pattern slot -> kit).
			std::map<uint8_t, uint8_t> patternKits;

			bool operator==(const State& _o) const;
			bool operator!=(const State& _o) const { return !(*this == _o); }
		};

		explicit Session(Send _send);

		// Requests; replies arrive through the callbacks as decoded values.
		void requestStatus();
		void requestPattern(uint8_t _slot);
		void requestKit(uint8_t _slot);
		void requestSong(uint8_t _slot);
		void requestGlobal(uint8_t _slot);

		// Pushes store the value in its own slot (the value's position), then ask
		// for it back so callers see what the firmware holds. Values that fail
		// elektronData::validate are refused: the result lists the problems.
		std::vector<std::string> pushPattern(const elektronData::MdPattern& _pattern);
		std::vector<std::string> pushKit(const elektronData::MdKit& _kit, KitApply _apply);
		std::vector<std::string> pushSong(const elektronData::MdSong& _song);
		std::vector<std::string> pushGlobal(const elektronData::MdGlobal& _global);

		// Machine commands.
		void selectPattern(uint8_t _slot);
		void loadKit(uint8_t _slot);
		void saveKit(uint8_t _slot);
		void loadSong(uint8_t _slot);
		void saveSong(uint8_t _slot);

		// The UI changed the working kit live (CC / parameter edits). The session
		// cannot see those itself.
		void noteWorkingKitEdited();

		// True when selecting _slot would replace the working kit while it holds
		// unsaved edits: EXTENDED mode and a known link to a different kit.
		bool selectWouldDiscardKitEdits(uint8_t _slot) const;

		// Feed every SysEx message the device sends.
		void onSysex(const Bytes& _message);

		const State& state() const { return m_state; }

		std::function<void(const elektronData::MdPattern&)> onPattern;
		std::function<void(const elektronData::MdKit&)> onKit;
		std::function<void(const elektronData::MdSong&)> onSong;
		std::function<void(const elektronData::MdGlobal&)> onGlobal;
		std::function<void(const State&)> onState;

		// The State as the contract's "md-desk/machine" document.
		static elektronData::json::Value stateToJson(const State& _state);

	private:
		void send(const Bytes& _bytes) const;
		void changed(const State& _before) const;
		void applyStatus(const elektronData::MdStatusValue& _status);

		Send m_send;
		State m_state;
	};
}
