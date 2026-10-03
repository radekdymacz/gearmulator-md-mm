#pragma once

#include "elektronData/mdGlobal.h"
#include "elektronData/mdMachines.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

namespace mdDesk
{
	// The page's keyboard on a Machinedrum (the note intent, deskCore/deskNotes.h), pure. OS 1.63 has no
	// chromatic mode: a key is the track's MAP EDITOR note (a trig). On the sample machines (ROM-nn,
	// RAM-Pn) the pitch is PTCH, which the manual scales (Appendix A, ROM): two octaves up or down, the
	// first octave every third step a semitone (64 +- 36). The second octave's 27 (up) and 28 (down) steps
	// are spread evenly here: the manual gives no step for them. Pitch 0 is the sound as its PTCH has it
	// now. The other machines have no semitone scale for PTCH in the manual: they play at their own pitch;
	// GND-EMPTY is silent and RAM-Rn records on its trigs, so the keys leave both alone.
	namespace keys
	{
		enum class Kind : uint8_t { Pitch, Trig, None };

		// The sample machines' PTCH is their first synthesis parameter.
		constexpr uint8_t g_ptchIndex = 0;

		inline Kind kindOf(const uint32_t _model)
		{
			const auto facts = elektronData::mdMachineFacts(_model);
			if(facts.sampler)
				return Kind::Pitch;
			if(facts.empty || facts.recorder)
				return Kind::None;
			return Kind::Trig;
		}

		// PTCH as semitones from 64, and back (clamped to 0-127, rounded as the page did: half up).
		inline double ptchSemis(const int _v)
		{
			const double d = _v - 64;
			if(std::abs(d) <= 36)
				return d / 3;
			return d > 0 ? 12 + (d - 36) * 12 / 27 : -12 - (-d - 36) * 12 / 28;
		}

		inline uint8_t semisPtch(const double _s)
		{
			const double v = std::abs(_s) <= 12 ? 64 + 3 * _s : _s > 0 ? 100 + (_s - 12) * 27 / 12 : 28 - (-_s - 12) * 28 / 12;
			return static_cast<uint8_t>(std::max(0.0, std::min(127.0, std::floor(v + 0.5))));
		}

		// The PTCH a key holds: _pitch semitones from the sound as tuned (_tuned).
		inline uint8_t heldPtch(const uint8_t _tuned, const int _pitch)
		{
			return semisPtch(ptchSemis(_tuned) + _pitch);
		}

		// Track _t's MAP EDITOR note in the active global (the lowest that maps to it), or the manual's
		// default map (C2 track 1 ... D4 track 16) while no global is known.
		inline std::optional<uint8_t> trackNote(const elektronData::MdGlobal* _global, const size_t _t)
		{
			static constexpr std::array<uint8_t, 16> defaults{36, 38, 40, 41, 43, 45, 47, 48, 50, 52, 53, 55, 57, 59, 60, 62};
			if(!_global)
				return _t < defaults.size() ? std::optional<uint8_t>(defaults[_t]) : std::nullopt;
			for(size_t n = 0; n < 128; ++n)
				if(_global->keymap[n] == _t)
					return static_cast<uint8_t>(n);
			return std::nullopt;
		}

		// Why a machine the keys leave alone is not played (the page says it once).
		inline std::string notPlayed(const std::string& _machine)
		{
			return _machine + " is not played from the keyboard (" + (_machine == "GND-EMPTY" ? "it has no sound" : "a recorder records on its trigs") + ").";
		}

		inline std::string ownPitch(const std::string& _machine)
		{
			return _machine + " has no semitone scale for PTCH: the keys play it at its own pitch.";
		}
	}
}
