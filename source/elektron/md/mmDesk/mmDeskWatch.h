#pragma once

#include "mmDeskTelemetry.h"

#include "deskCore/deskNotes.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace mmDesk
{
	// Small state values of the Monomachine adapter, each with a pure step: (state, what happened, now) ->
	// (next state, what to do). The adapter keeps one of each and does the effects (review finding 9).

	// ---- playing, from the step byte ----
	// In the plug-in the RAM running flag (0x26b46e) can stay 0 while the sequencer plays, so "playing" is the
	// flag or the step advancing: two single steps forward (or a wrap to 0) in a row, each within three step
	// times at the tempo (a 3/4X pattern included). A stop that resets the step to 0 is one move, so it never
	// reads as playing.
	struct StepWatch
	{
		int rawStep = -1;
		int moves = 0;
		double movedMs = -1e9;
	};

	struct StepSeen
	{
		StepWatch next;
		bool playing = false;
		bool stepped = false;	// the step byte changed
	};

	inline StepSeen watchStep(StepWatch _s, const Telemetry& _t, const double _now)
	{
		StepSeen r;
		r.stepped = _t.valid && _t.step != _s.rawStep;
		const double stepMs = _t.tempo > 0 ? 360000.0 / _t.tempo : 250.0;
		const double window = std::max(250.0, 3.0 * stepMs);
		if(r.stepped)
		{
			const bool forward = _s.rawStep >= 0 && (_t.step == _s.rawStep + 1 || (_t.step == 0 && _s.rawStep > 0));
			_s.moves = forward && _now - _s.movedMs < window ? std::min(_s.moves + 1, 2) : (forward ? 1 : 0);
			_s.movedMs = _now;
			_s.rawStep = _t.step;
		}
		const bool stepping = _s.moves >= 2 && _now - _s.movedMs < window;
		if(!stepping && _now - _s.movedMs >= window)
			_s.moves = 0;
		r.playing = _t.valid && (_t.running || stepping);
		r.next = _s;
		return r;
	}

	// ---- RECORD: the current pattern read back while the machine records ----
	// The machine writes the current pattern while it records (MM-P4): read it back this often while it
	// records, and once when it stops, so what the keyboard or the machine's TRIG keys recorded shows.
	struct RecordReads
	{
		int last = -1;			// the recording mode last seen (Telemetry::recording)
		double readMs = -1e9;	// the last read-back
	};

	struct RecordStep
	{
		RecordReads next;
		bool read = false;		// ask for the current pattern now
		bool changed = false;	// the recording mode changed since the last step
	};

	inline RecordStep watchRecord(RecordReads _s, const int _recording, const bool _patternKnown, const double _now, const double _everyMs)
	{
		RecordStep r;
		r.changed = _recording != _s.last;
		if(_recording >= 0 && _patternKnown && ((_recording >= 1 && _now - _s.readMs > _everyMs) || (_s.last >= 1 && _recording == 0)))
		{
			r.read = true;
			_s.readMs = _now;
		}
		_s.last = _recording;
		r.next = _s;
		return r;
	}

	// ---- the transport telemetry message ----
	// The playhead, at most every _minMs, and the recording mode when it changes.
	struct TelemetryOut
	{
		int step = -1;
		bool playing = false;
		double sentMs = -1e9;
	};

	// The next state when a telemetry message is due now; nullopt: nothing to publish.
	inline std::optional<TelemetryOut> telemetryDue(const TelemetryOut& _s, const int _step, const bool _playing, const bool _recordChanged,
		const double _now, const double _minMs)
	{
		if(!(_recordChanged || ((_step != _s.step || _playing != _s.playing) && _now - _s.sentMs > _minMs)))
			return std::nullopt;
		return TelemetryOut{_step, _playing, _now};
	}

	// ---- the keyboard's sounding notes ----
	// (track, pitch) -> the MIDI channel and note its note on went to: the note off goes there, whatever the
	// global says meanwhile. A second note on of the same pitch ends the first.
	struct NoteSend
	{
		uint8_t channel = 0;
		uint8_t note = 0;
		uint8_t velocity = 0;	// 0: note off
	};

	using SoundingNotes = std::map<std::pair<uint8_t, int>, std::pair<uint8_t, uint8_t>>;

	struct NotesStep
	{
		SoundingNotes next;
		std::vector<NoteSend> sends;	// in order
	};

	inline NotesStep pressNote(SoundingNotes _s, const uint8_t _track, const int _pitch, const uint8_t _channel, const uint8_t _note,
		const uint8_t _velocity)
	{
		NotesStep r;
		const std::pair<uint8_t, int> key{_track, _pitch};
		if(const auto it = _s.find(key); it != _s.end())
			r.sends.push_back({it->second.first, it->second.second, 0});
		r.sends.push_back({_channel, _note, _velocity});
		_s[key] = {_channel, _note};
		r.next = std::move(_s);
		return r;
	}

	inline NotesStep releaseNotes(SoundingNotes _s, const deskCore::NoteOff& _off)
	{
		NotesStep r;
		for(auto it = _s.begin(); it != _s.end();)
		{
			if(it->first.first != _off.track || !_off.releases(it->first.second))
			{
				++it;
				continue;
			}
			r.sends.push_back({it->second.first, it->second.second, 0});
			it = _s.erase(it);
		}
		r.next = std::move(_s);
		return r;
	}
}
