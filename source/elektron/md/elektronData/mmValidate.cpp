#include "mmValidate.h"

#include "mmGlobal.h"
#include "mmKit.h"
#include "mmMachines.h"
#include "mmPattern.h"
#include "mmSong.h"

#include <string>

namespace elektronData
{
	namespace
	{
		class Problems
		{
		public:
			void check(const bool _ok, const std::string& _what)
			{
				if(!_ok)
					m_list.push_back(_what);
			}
			template<typename T>
			void range(const T _v, const int _min, const int _max, const std::string& _what)
			{
				check(static_cast<int>(_v) >= _min && static_cast<int>(_v) <= _max,
					_what + ": " + std::to_string(static_cast<int>(_v)) + " is outside " + std::to_string(_min) + ".."
						+ std::to_string(_max));
			}
			std::vector<std::string> take() { return std::move(m_list); }

		private:
			std::vector<std::string> m_list;
		};

		std::string at(const char* _what, const size_t _i) { return std::string(_what) + "[" + std::to_string(_i) + "]"; }

		void arp(Problems& _p, const MmArpBlock& _a, const char* _name, const bool _synth)
		{
			for(size_t t = 0; t < 6; ++t)
			{
				const auto base = std::string(_name) + "[" + std::to_string(t) + "]";
				_p.range(_a.playOjmp[t] & 7, 0, 4, base + ".play");
				_p.range(_a.mode[t], 0, 3, base + ".mode");
				_p.range(_a.range[t], 0, 7, base + ".range");
				_p.range(_a.speed[t], 0, 127, base + ".speed");
				if(_synth)
					_p.range(_a.trigs[t], 0, 7, base + ".trigs");
				_p.range(_a.length[t], 1, 16, base + ".length");
				for(size_t s = 0; s < 16; ++s)
					_p.check(_a.steps[t][s] == g_mmArpStepMuted || _a.steps[t][s] <= 127,
						base + ".steps[" + std::to_string(s) + "] must be 0-127 or muted");
			}
		}

		void transpose(Problems& _p, const MmTranspose& _t, const char* _name)
		{
			for(size_t i = 0; i < 6; ++i)
			{
				_p.range(_t.track[i], -64, 63, at(_name, i) + ".transpose");
				_p.range(_t.scale[i], 0, 3, at(_name, i) + ".scale");
				_p.range(_t.key[i], 0, 11, at(_name, i) + ".key");
			}
		}

		template<size_t N>
		void notePool(Problems& _p, const std::array<uint16_t, N>& _pool, const size_t _count, const char* _name)
		{
			_p.range(_count, 0, static_cast<int>(N), std::string(_name) + " count");
			// Factory patterns hold empty (0xffff) entries inside the count; the
			// firmware keeps entries in (step, track) order as it adds them, but
			// factory data is not always in order, so order is not a limit here.
			for(size_t i = 0; i < _count && i < N; ++i)
			{
				if(_pool[i] == 0xffff)
					continue;
				_p.range(mmNoteEntry(_pool[i]).track, 0, 5, at(_name, i) + ".track");
			}
		}
	}

	std::vector<std::string> validate(const MmPattern& _pat)
	{
		Problems p;
		p.range(_pat.position, 0, 127, "slot");
		p.range(_pat.length, 2, 64, "length");
		p.range(_pat.multiplier, 0, 3, "multiplier");
		p.range(_pat.kit, 0, 127, "kit");
		p.range(_pat.swingAmount, 0, 30, "swingAmount");
		p.range(_pat.patternTranspose, -64, 63, "patternTranspose");
		transpose(p, _pat.transpose, "tracks");
		transpose(p, _pat.midiTranspose, "midiTracks");
		arp(p, _pat.arp, "tracks.arp", true);
		arp(p, _pat.midiArp, "midiTracks.arp", false);
		for(size_t t = 0; t < MmPattern::g_tracks; ++t)
			for(size_t s = 0; s < MmPattern::g_steps; ++s)
				p.check(_pat.notes[t][s] == MmPattern::g_noNote || _pat.notes[t][s] <= 127,
					"tracks[" + std::to_string(t) + "] note on step " + std::to_string(s) + " must be 0-127");
		const auto params = mmLockParams(_pat);
		p.check(params.size() <= MmPattern::g_lockRows, "at most 62 locked parameters per pattern (" + std::to_string(params.size()) + ")");
		p.check(params.size() == _pat.lockRowCount, "lock row count " + std::to_string(_pat.lockRowCount)
			+ " does not match the " + std::to_string(params.size()) + " locked parameters");
		for(size_t r = 0; r < _pat.lockRowCount && r < MmPattern::g_lockRows; ++r)
			for(size_t s = 0; s < MmPattern::g_steps; ++s)
				p.check(_pat.lockRows[r][s] == MmPattern::g_noLock || _pat.lockRows[r][s] <= 127,
					"lock row " + std::to_string(r) + " step " + std::to_string(s) + " must be 0-127");
		// Trig kinds (MM-P2-RESULT §3): a step holds a trig (trig), fires envelopes
		// (amp, filter, lfo) or both. Trigless = trig without envelopes; pitchless =
		// trig without a note; an envelope-only step (TRIG SELECT) has no trig bit.
		// Notes and chords need the trig; a lock needs a trig or an envelope trig.
		for(size_t t = 0; t < MmPattern::g_tracks; ++t)
		{
			for(size_t s = 0; s < MmPattern::g_steps; ++s)
			{
				const bool trig = mmStepSet(_pat.pitch[t], s);
				p.check(trig || _pat.notes[t][s] == MmPattern::g_noNote,
					"tracks[" + std::to_string(t) + "] step " + std::to_string(s) + " has a note but no trig");
				p.check(trig || !mmStepSet(_pat.chord[t], s),
					"tracks[" + std::to_string(t) + "] step " + std::to_string(s) + " has chord notes but no trig");
				p.check(mmStepSet(_pat.midiTrig[t], s) || !mmStepSet(_pat.midiNote[t], s),
					"midiTracks[" + std::to_string(t) + "] step " + std::to_string(s) + " has notes but no MIDI trig");
			}
		}
		for(size_t r = 0; r < params.size() && r < _pat.lockRowCount && r < MmPattern::g_lockRows; ++r)
		{
			const auto& lp = params[r];
			const auto holds = lp.page == 7 ? _pat.midiTrig[lp.track]
				: _pat.pitch[lp.track] | _pat.amp[lp.track] | _pat.filter[lp.track] | _pat.lfo[lp.track];
			for(size_t s = 0; s < MmPattern::g_steps; ++s)
				p.check(_pat.lockRows[r][s] == MmPattern::g_noLock || mmStepSet(holds, s),
					"a lock on track " + std::to_string(lp.track + 1) + " step " + std::to_string(s + 1) + " has no trig to sit on");
		}
		notePool(p, _pat.midiNotes, _pat.midiNoteCount, "midiNotes");
		notePool(p, _pat.chordNotes, _pat.chordNoteCount, "chordNotes");
		return p.take();
	}

	std::vector<std::string> validate(const MmKit& _kit)
	{
		Problems p;
		p.range(_kit.position, 0, 127, "slot");
		for(size_t t = 0; t < MmKit::g_tracks; ++t)
		{
			const auto base = "tracks[" + std::to_string(t) + "]";
			p.range(_kit.levels[t], 0, 127, "levels[" + std::to_string(t) + "]");
			p.check(mmMachine(_kit.machines[t]) != nullptr, base + ".machine " + std::to_string(_kit.machines[t]) + " is not an OS 1.32 machine");
			p.range(mmRoutingInput(_kit.routing[t]), 0, 6, base + ".input");
			p.check(_kit.routing[t] < 0x40, base + ".routing uses bits 0-5 only");
			for(size_t pg = 0; pg < 7; ++pg)
				for(size_t i = 0; i < 8; ++i)
					p.range(_kit.tracks[t].pages[pg][i], 0, 127, base + ".pages[" + std::to_string(pg) + "][" + std::to_string(i) + "]");
			for(size_t i = 0; i < 8; ++i)
				p.range(_kit.tracks[t].midi[i], 0, 127, base + ".pages[7][" + std::to_string(i) + "]");
			p.check(_kit.trigPos[t] == MmKit::g_noTrigPos || _kit.trigPos[t] < 6, base + ".trigPos must be a track or none");
			for(size_t i = 0; i < 12; ++i)
			{
				p.range(_kit.assignPage[t][i], 0, 127, base + ".assign.page");
				p.range(_kit.assignDest[t][i], 0, 127, base + ".assign.dest");
			}
		}
		p.range(_kit.multiTrigMode, 0, 3, "multiTrig.mode");
		p.range(_kit.multiTrigTiming, 0, 6, "multiTrig.timing");
		p.range(_kit.splitKey, 0, 127, "multiTrig.splitKey");
		p.range(_kit.splitTrack, 0, 5, "multiTrig.splitTrack");
		return p.take();
	}

	std::vector<std::string> validate(const MmSong& _song)
	{
		Problems p;
		p.range(_song.position, 0, 23, "slot");
		const auto used = mmSongUsedRows(_song);
		for(size_t i = 0; i < used; ++i)
		{
			const auto& b = _song.rows[i].bytes;
			const auto base = "rows[" + std::to_string(i) + "]";
			const auto pat = b[mmSongRow::g_pattern];
			p.check(pat <= 127 || pat == MmSong::g_loop || pat == MmSong::g_end, base + ".pattern must be 0-127, LOOP or END");
			if(pat == MmSong::g_loop)
			{
				p.range(b[mmSongRow::g_target], 0, static_cast<int>(MmSong::g_rows - 1), base + ".target");
				p.range(b[mmSongRow::g_repeats], 0, 99, base + ".repeats");
			}
			if(pat <= 127)
			{
				p.range(b[mmSongRow::g_repeats], 0, 63, base + ".repeats");
				p.range(b[mmSongRow::g_length], 1, 64, base + ".length");
				p.check(b[mmSongRow::g_offset] < b[mmSongRow::g_length], base + ".offset must be below the length");
				const auto tempo = _song.rows[i].tempo();
				p.check(tempo == MmSong::g_keepTempo || (tempo >= 30 && tempo <= 300), base + ".tempo must be 30-300 BPM or keep");
			}
		}
		return p.take();
	}

	std::vector<std::string> validate(const MmGlobal& _g)
	{
		Problems p;
		p.range(_g.position, 0, 7, "slot");
		for(const auto c : {_g.autoChannel, _g.baseChannel, _g.multiTrigChannel, _g.multiMapChannel})
			p.check(c <= 15 || c == 0x7f, "a MIDI channel must be 0-15 (or off)");
		p.range(_g.channelSpan, 1, 16, "channels.span");
		p.range(_g.routingMode, 0, 2, "routingMode");
		for(size_t t = 0; t < 6; ++t)
			p.check(_g.midiSeqChannels[t] <= 15 || _g.midiSeqChannels[t] == 0x7f, "midiSeq.channels[" + std::to_string(t) + "]");
		return p.take();
	}
}
