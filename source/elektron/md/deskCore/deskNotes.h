#pragma once

#include "elektronData/json.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace deskCore
{
	// The page's keyboard, one intent for both editors (DESIGN-REVIEW-2026-10-02 finding 5):
	//   noteOn {t, vel, pitch}: play track t at velocity vel (1-127), pitch semitones from the track's sound;
	//   noteOff {t, pitch}: let that note go (no pitch: every note of the track).
	// What a pitch is on the machine (a MIDI note on the track's channel, or a kit value held while the key
	// is down) is the adapter's; the page knows no parameter index and no MIDI byte.
	struct NoteOn
	{
		uint8_t track = 0;
		uint8_t velocity = 0;
		int pitch = 0;
	};

	struct NoteOff
	{
		uint8_t track = 0;
		std::optional<int> pitch;	// none: every note of the track

		bool releases(const int _pitch) const { return !pitch || *pitch == _pitch; }
	};

	namespace notes
	{
		inline std::optional<int> member(const elektronData::json::Value& _m, const char* _key)
		{
			const auto* v = _m.find(_key);
			if(!v || !v->isNumber())
				return std::nullopt;
			return static_cast<int>(v->asNumber());
		}
	}

	// The commands as values (the router checked the arguments against the table already).
	inline NoteOn noteOnOf(const elektronData::json::Value& _m)
	{
		return {static_cast<uint8_t>(notes::member(_m, "t").value_or(0)), static_cast<uint8_t>(notes::member(_m, "vel").value_or(0)),
			notes::member(_m, "pitch").value_or(0)};
	}

	inline NoteOff noteOffOf(const elektronData::json::Value& _m)
	{
		return {static_cast<uint8_t>(notes::member(_m, "t").value_or(0)), notes::member(_m, "pitch")};
	}

	// A kit value a held key changes on the machine for as long as it is down (the MD's PTCH on the sample
	// machines): the machine's momentary state, never an edit. base is the value the key replaced, for
	// when no document knows the track.
	struct HeldOverride
	{
		uint8_t track = 0;
		uint8_t index = 0;
		uint8_t value = 0;
		uint8_t base = 0;
	};

	// The held overrides as one named transient layer over the kit that plays. A memory image taken while
	// a key is held shows the override; masked, it shows the document's value there instead, so it never
	// reports the transient value as a kit change. Letting go restores the document's value (an edit made
	// meanwhile is in the document, so it wins), never a guess from what the machine says. Pure.
	class HeldOverrides
	{
	public:
		// One override per (track, index): a second key on the track replaces the first's.
		void hold(const HeldOverride& _o)
		{
			auto it = find(_o.track, _o.index);
			if(it != m_list.end())
				*it = _o;
			else
				m_list.push_back(_o);
		}

		// The track's overrides, taken out (what to restore).
		std::vector<HeldOverride> release(const uint8_t _track)
		{
			std::vector<HeldOverride> out;
			for(const auto& o : m_list)
				if(o.track == _track)
					out.push_back(o);
			m_list.erase(std::remove_if(m_list.begin(), m_list.end(), [&](const HeldOverride& _o) { return _o.track == _track; }), m_list.end());
			return out;
		}

		std::vector<HeldOverride> releaseAll()
		{
			auto out = std::move(m_list);
			m_list.clear();
			return out;
		}

		bool any() const { return !m_list.empty(); }
		const std::vector<HeldOverride>& list() const { return m_list; }

		// The machine reports the value a key holds (its echo): not a change of the kit.
		bool echo(const uint8_t _track, const uint8_t _index, const uint8_t _value) const
		{
			const auto it = std::find_if(m_list.begin(), m_list.end(),
				[&](const HeldOverride& _o) { return _o.track == _track && _o.index == _index; });
			return it != m_list.end() && it->value == _value;
		}

		// _image without the layer: at every held (track, index), _under's value (the document), else the
		// value the key replaced. _at(kit, track, index) is the model's place of a kit value (a reference).
		template<typename Kit, typename At>
		Kit masked(Kit _image, const Kit* _under, const At& _at) const
		{
			for(const auto& o : m_list)
				_at(_image, o.track, o.index) = _under ? _at(*_under, o.track, o.index) : o.base;
			return _image;
		}

		// What letting go of _o puts back: the document's value, else the value the key replaced.
		template<typename Kit, typename At>
		static uint8_t restoreValue(const HeldOverride& _o, const Kit* _document, const At& _at)
		{
			return _document ? _at(*_document, _o.track, _o.index) : _o.base;
		}

	private:
		std::vector<HeldOverride>::iterator find(const uint8_t _track, const uint8_t _index)
		{
			return std::find_if(m_list.begin(), m_list.end(), [&](const HeldOverride& _o) { return _o.track == _track && _o.index == _index; });
		}

		std::vector<HeldOverride> m_list;
	};
}
