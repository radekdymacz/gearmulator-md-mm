// The Monomachine's edit intents (DESIGN-UNIFY.md 4.1 and 4.2): pure transforms of the documents the core holds. A
// command row of MmModel::commands() names the document kind and checks the arguments; one function per op runs
// over the pattern, the working kit, a song or the active global, and the result is validated. What the
// Monomachine alone has lives here (note steps and chords, the 62 pooled locks, the arpeggiator, the mix buses, the
// machines' default values, ASSIGN, MULTI ENV and MULTI TRIG, the MULTI MAP); how steps move (rotate, double), the
// song rows and the library's slot actions are deskCore/deskEdits.h, shared with the Machinedrum.

#include "mmDeskModel.h"

#include "deskCore/deskBlocks.h"
#include "deskCore/deskEdits.h"

#include "elektronData/factoryGlobals.h"
#include "elektronData/mmJson.h"
#include "elektronData/mmMachines.h"
#include "elektronData/mmValidate.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace mmDesk
{
	namespace ed = elektronData;
	using ed::json::Value;
	using ed::MmPattern;
	using ed::MmKit;
	using ed::MmGlobal;
	using ed::MmSong;

	namespace
	{
		// ---- arguments: the table has checked types and constant ranges; what depends on the document is here ----
		class Args
		{
		public:
			Args(const Value& _cmd, std::vector<std::string>& _errors) : m_cmd(_cmd), m_errors(_errors) {}

			const Value& command() const { return m_cmd; }

			bool has(const char* _key) const
			{
				const auto* v = m_cmd.find(_key);
				return v && !v->isNull();
			}
			int integer(const char* _key, const int _default = 0) const
			{
				const auto* v = m_cmd.find(_key);
				return v && v->isNumber() ? static_cast<int>(v->asNumber()) : _default;
			}
			bool flag(const char* _key) const
			{
				const auto* v = m_cmd.find(_key);
				return v && (v->isBool() ? v->asBool() : v->isNumber() && v->asNumber() != 0);
			}
			const std::string& text(const char* _key) const { return m_cmd.find(_key)->asString(); }
			const Value* value(const char* _key) const { return m_cmd.find(_key); }
			void error(std::string _e) const { m_errors.push_back(std::move(_e)); }

		private:
			const Value& m_cmd;
			std::vector<std::string>& m_errors;
		};

		std::vector<std::string> problemsOf(const Document& _d)
		{
			struct V
			{
				std::vector<std::string> operator()(const MmPattern& _p) const { return ed::validate(_p); }
				std::vector<std::string> operator()(const MmKit& _k) const { return ed::validate(_k); }
				std::vector<std::string> operator()(const ed::MmSong& _s) const { return ed::validate(_s); }
				std::vector<std::string> operator()(const MmGlobal& _g) const { return ed::validate(_g); }
				std::vector<std::string> operator()(const WorkingKit& _w) const { return ed::validate(_w.kit); }
			};
			return std::visit(V{}, _d);
		}

		struct In
		{
			const Args& a;
			Clipboard& clip;
			std::string& note;
			const Documents& docs;
		};

		template<typename T>
		using EditFn = std::optional<T> (*)(T, const In&);
		template<typename T>
		using Edits = std::map<std::string, EditFn<T>>;

		std::string trackName(const int _t) { return _t < 6 ? "T" + std::to_string(_t + 1) : "M" + std::to_string(_t - 5); }

		// ---- a track of the pattern: a synth track (0-5) or a MIDI sequencer track (6-11) ----
		struct Track
		{
			bool midi;
			uint8_t t;		// 0-5 in its kind
		};
		Track trackOf(const int _t) { return {_t >= 6, static_cast<uint8_t>(_t >= 6 ? _t - 6 : _t)}; }

		// The masks of a track that a step's value lives in (slide and swing are tracks of their own).
		std::vector<uint64_t*> stepMasks(MmPattern& _p, const Track& _tr)
		{
			if(_tr.midi)
				return {&_p.midiTrig[_tr.t], &_p.midiNote[_tr.t], &_p.midiNoteOff[_tr.t]};
			return {&_p.pitch[_tr.t], &_p.amp[_tr.t], &_p.filter[_tr.t], &_p.lfo[_tr.t], &_p.noteOff[_tr.t], &_p.chord[_tr.t]};
		}
		uint64_t& slideOf(MmPattern& _p, const Track& _tr) { return _tr.midi ? _p.midiSlide[_tr.t] : _p.slide[_tr.t]; }
		uint64_t& swingOf(MmPattern& _p, const Track& _tr) { return _tr.midi ? _p.midiSwing[_tr.t] : _p.swing[_tr.t]; }

		// The steps a lock of this track can sit on (mmValidate: a trig or an envelope trig; a MIDI track's trig).
		uint64_t holds(const MmPattern& _p, const Track& _tr)
		{
			return _tr.midi ? _p.midiTrig[_tr.t] : _p.pitch[_tr.t] | _p.amp[_tr.t] | _p.filter[_tr.t] | _p.lfo[_tr.t];
		}

		// ---- the note pools: entries (track, step, note), kept in (step, track) order as the firmware keeps them ----
		template<size_t N>
		struct Pool
		{
			std::array<uint16_t, N>& e;
			size_t count;

			static int key(const uint16_t _w) { const auto x = ed::mmNoteEntry(_w); return x.step * 8 + x.track; }

			template<typename Pred>
			void remove(Pred _pred)
			{
				size_t out = 0;
				for(size_t i = 0; i < count; ++i)
					if(e[i] == 0xffff || !_pred(ed::mmNoteEntry(e[i])))
						e[out++] = e[i];
				for(size_t i = out; i < count; ++i)
					e[i] = 0xffff;
				count = out;
			}
			// after every entry of a key not above its own, so notes of one step stay in the order given
			bool insert(const ed::MmNoteEntry& _x)
			{
				if(count >= N)
					return false;
				const auto w = ed::mmNoteEntryWord(_x);
				size_t at = count;
				for(size_t i = 0; i < count; ++i)
					if(e[i] != 0xffff && key(e[i]) > key(w))
					{
						at = i;
						break;
					}
				for(size_t i = count; i > at; --i)
					e[i] = e[i - 1];
				e[at] = w;
				++count;
				return true;
			}
			// the entries in (step, track) order again, stably; empty entries keep their places
			void sort()
			{
				std::vector<uint16_t> live;
				for(size_t i = 0; i < count; ++i)
					if(e[i] != 0xffff)
						live.push_back(e[i]);
				std::stable_sort(live.begin(), live.end(), [](const uint16_t _a, const uint16_t _b) { return key(_a) < key(_b); });
				size_t j = 0;
				for(size_t i = 0; i < count; ++i)
					if(e[i] != 0xffff)
						e[i] = live[j++];
			}
		};
		Pool<MmPattern::g_chordNoteCapacity> chordPool(MmPattern& _p) { return {_p.chordNotes, _p.chordNoteCount}; }
		Pool<MmPattern::g_midiNoteCapacity> midiPool(MmPattern& _p) { return {_p.midiNotes, _p.midiNoteCount}; }
		void store(MmPattern& _p, const Pool<MmPattern::g_chordNoteCapacity>& _c) { _p.chordNoteCount = static_cast<uint8_t>(_c.count); }
		void store(MmPattern& _p, const Pool<MmPattern::g_midiNoteCapacity>& _m) { _p.midiNoteCount = static_cast<uint16_t>(_m.count); }

		// ---- the 62 pooled lock rows: one per locked parameter, in (track, page, param) order ----
		ed::MmLockParam lockParam(const Track& _tr, const int _page, const int _i)
		{
			return {_tr.t, static_cast<uint8_t>(_tr.midi ? 7 : _page), static_cast<uint8_t>(_i)};
		}
		void removeRow(MmPattern& _p, const size_t _row)
		{
			const auto params = ed::mmLockParams(_p);
			if(_row >= params.size())
				return;
			const auto& lp = params[_row];
			_p.lockMasks[lp.track][lp.page] &= static_cast<uint8_t>(~(1u << lp.param));
			const size_t count = std::min<size_t>(_p.lockRowCount, MmPattern::g_lockRows);
			for(size_t r = _row; r + 1 < count; ++r)
				_p.lockRows[r] = _p.lockRows[r + 1];
			if(count)
				_p.lockRows[count - 1].fill(MmPattern::g_noLock);
			_p.lockRowCount = static_cast<uint8_t>(count ? count - 1 : 0);
		}
		// the row of a parameter, made when it has none (-1: all 62 are in use)
		int rowFor(MmPattern& _p, const ed::MmLockParam& _lp)
		{
			if(const auto r = ed::mmLockRow(_p, _lp); r >= 0)
				return r;
			if(_p.lockRowCount >= MmPattern::g_lockRows)
				return -1;
			_p.lockMasks[_lp.track][_lp.page] |= static_cast<uint8_t>(1u << _lp.param);
			const auto made = ed::mmLockRow(_p, _lp);
			if(made < 0)
			{
				// the masks hold more parameters than the pool has rows (a corrupt dump): no row for this one
				_p.lockMasks[_lp.track][_lp.page] &= static_cast<uint8_t>(~(1u << _lp.param));
				return -1;
			}
			const auto r = static_cast<size_t>(made);
			for(size_t i = _p.lockRowCount; i > r; --i)
				_p.lockRows[i] = _p.lockRows[i - 1];
			_p.lockRows[r].fill(MmPattern::g_noLock);
			++_p.lockRowCount;
			return static_cast<int>(r);
		}
		bool setLock(MmPattern& _p, const ed::MmLockParam& _lp, const size_t _s, const uint8_t _v)
		{
			const auto r = rowFor(_p, _lp);
			if(r < 0)
				return false;
			_p.lockRows[static_cast<size_t>(r)][_s] = _v;
			return true;
		}
		// a row that holds no lock any more goes (its parameter is no longer locked)
		void dropEmptyRow(MmPattern& _p, const ed::MmLockParam& _lp)
		{
			const auto r = ed::mmLockRow(_p, _lp);
			if(r < 0)
				return;
			const auto& row = _p.lockRows[static_cast<size_t>(r)];
			if(std::all_of(row.begin(), row.end(), [](const uint8_t _v) { return _v == MmPattern::g_noLock; }))
				removeRow(_p, static_cast<size_t>(r));
		}
		void clearLock(MmPattern& _p, const ed::MmLockParam& _lp, const size_t _s)
		{
			const auto r = ed::mmLockRow(_p, _lp);
			if(r < 0)
				return;
			_p.lockRows[static_cast<size_t>(r)][_s] = MmPattern::g_noLock;
			dropEmptyRow(_p, _lp);
		}
		// the locked parameters of a track: a synth track's pages 0-6, a MIDI track's page 7
		std::vector<ed::MmLockParam> trackLocks(const MmPattern& _p, const Track& _tr)
		{
			std::vector<ed::MmLockParam> out;
			for(const auto& lp : ed::mmLockParams(_p))
				if(lp.track == _tr.t && (lp.page == 7) == _tr.midi)
					out.push_back(lp);
			return out;
		}
		void clearStepLocks(MmPattern& _p, const Track& _tr, const size_t _s)
		{
			for(const auto& lp : trackLocks(_p, _tr))
				clearLock(_p, lp, _s);
		}

		// ---- a step's value ----
		std::optional<StepValue> stepValue(const Value* _v, std::string& _error)
		{
			StepValue s;
			if(!_v || _v->isNull())
				return s;
			if(!_v->isObject())
			{
				_error = "v: null, {off:true} or {n, a, f, l}";
				return {};
			}
			const auto bit = [&](const char* _k)
			{
				const auto* b = _v->find(_k);
				return b && (b->isBool() ? b->asBool() : b->isNumber() && b->asNumber() != 0);
			};
			if(bit("off"))
			{
				s.kind = StepValue::Kind::Off;
				return s;
			}
			s.kind = StepValue::Kind::On;
			s.a = bit("a");
			s.f = bit("f");
			s.l = bit("l");
			if(const auto* n = _v->find("n"); n && !n->isNull())
			{
				if(!n->isArray() || n->asArray().size() > 16)
				{
					_error = "v.n: a list of up to 16 notes";
					return {};
				}
				for(const auto& x : n->asArray())
				{
					if(!x.isNumber() || x.asNumber() != std::floor(x.asNumber()) || x.asNumber() < 0 || x.asNumber() > 127)
					{
						_error = "v.n: notes are 0-127";
						return {};
					}
					s.notes.push_back(static_cast<uint8_t>(x.asNumber()));
				}
			}
			s.trig = !bit("notrig") || !s.notes.empty();
			return s;
		}

		StepValue stepAt(const MmPattern& _p, const Track& _tr, const size_t _s)
		{
			StepValue v;
			if(_tr.midi)
			{
				if(ed::mmStepSet(_p.midiNoteOff[_tr.t], _s))
					v.kind = StepValue::Kind::Off;
				else if(ed::mmStepSet(_p.midiTrig[_tr.t], _s))
				{
					v.kind = StepValue::Kind::On;
					v.a = v.f = v.l = true;
					for(size_t i = 0; i < _p.midiNoteCount; ++i)
						if(_p.midiNotes[i] != 0xffff)
						{
							const auto e = ed::mmNoteEntry(_p.midiNotes[i]);
							if(e.track == _tr.t && e.step == _s)
								v.notes.push_back(e.note);
						}
					if(!ed::mmStepSet(_p.midiNote[_tr.t], _s))
						v.notes.clear();
				}
				return v;
			}
			if(ed::mmStepSet(_p.noteOff[_tr.t], _s))
			{
				v.kind = StepValue::Kind::Off;
				return v;
			}
			const bool trig = ed::mmStepSet(_p.pitch[_tr.t], _s);
			if(!(trig || ed::mmStepSet(_p.amp[_tr.t], _s) || ed::mmStepSet(_p.filter[_tr.t], _s) || ed::mmStepSet(_p.lfo[_tr.t], _s)))
				return v;
			v.kind = StepValue::Kind::On;
			v.trig = trig;
			v.a = ed::mmStepSet(_p.amp[_tr.t], _s);
			v.f = ed::mmStepSet(_p.filter[_tr.t], _s);
			v.l = ed::mmStepSet(_p.lfo[_tr.t], _s);
			if(_p.notes[_tr.t][_s] != MmPattern::g_noNote)
			{
				v.notes.push_back(_p.notes[_tr.t][_s]);
				if(ed::mmStepSet(_p.chord[_tr.t], _s))
					for(size_t i = 0; i < _p.chordNoteCount; ++i)
						if(_p.chordNotes[i] != 0xffff)
						{
							const auto e = ed::mmNoteEntry(_p.chordNotes[i]);
							if(e.track == _tr.t && e.step == _s)
								v.notes.push_back(e.note);
						}
			}
			return v;
		}

		// Step s of a track becomes _v: an empty step or a NOTE OFF loses its locks; slide and swing stay (tracks of
		// their own). False (and the reason) when a note pool is full.
		bool setStep(MmPattern& _p, const Track& _tr, const size_t _s, StepValue _v, std::string& _error)
		{
			const auto bit = ed::mmStepBit(_s);
			for(auto* m : stepMasks(_p, _tr))
				*m &= ~bit;
			if(_tr.midi)
			{
				auto pool = midiPool(_p);
				pool.remove([&](const ed::MmNoteEntry& _e) { return _e.track == _tr.t && _e.step == _s; });
				if(_v.kind == StepValue::Kind::On)
				{
					_p.midiTrig[_tr.t] |= bit;
					if(!_v.notes.empty())
						_p.midiNote[_tr.t] |= bit;
					for(const auto n : _v.notes)
						if(!pool.insert({_tr.t, static_cast<uint8_t>(_s), n}))
						{
							_error = "A pattern holds at most 400 MIDI notes";
							return false;
						}
				}
				store(_p, pool);
			}
			else
			{
				auto pool = chordPool(_p);
				pool.remove([&](const ed::MmNoteEntry& _e) { return _e.track == _tr.t && _e.step == _s; });
				_p.notes[_tr.t][_s] = MmPattern::g_noNote;
				// a step with neither a trig nor an envelope trig is empty
				if(_v.kind == StepValue::Kind::On && !_v.trig && !_v.a && !_v.f && !_v.l)
					_v.kind = StepValue::Kind::Empty;
				if(_v.kind == StepValue::Kind::On)
				{
					if(_v.trig)
						_p.pitch[_tr.t] |= bit;
					if(_v.a) _p.amp[_tr.t] |= bit;
					if(_v.f) _p.filter[_tr.t] |= bit;
					if(_v.l) _p.lfo[_tr.t] |= bit;
					if(!_v.notes.empty())
					{
						_p.notes[_tr.t][_s] = _v.notes.front();
						if(_v.notes.size() > 1)
							_p.chord[_tr.t] |= bit;
						for(size_t k = 1; k < _v.notes.size(); ++k)
							if(!pool.insert({_tr.t, static_cast<uint8_t>(_s), _v.notes[k]}))
							{
								_error = "A pattern holds at most 192 chord notes";
								return false;
							}
					}
				}
				store(_p, pool);
			}
			if(_v.kind == StepValue::Kind::Off)
				(_tr.midi ? _p.midiNoteOff[_tr.t] : _p.noteOff[_tr.t]) |= bit;
			if(_v.kind != StepValue::Kind::On)
				clearStepLocks(_p, _tr, _s);
			return true;
		}

		void setBit(uint64_t& _bits, const size_t _s, const bool _on) { _bits = _on ? _bits | ed::mmStepBit(_s) : _bits & ~ed::mmStepBit(_s); }

		// ---- pattern edits ----
		int patLength(const MmPattern& _p) { return std::max<int>(2, std::min<int>(64, _p.length)); }

		bool stepIn(const In& _in, const int _s, const int _from = 0, const int _to = 64)
		{
			if(_s < _from || _s >= _to)
			{
				_in.a.error("step " + std::to_string(_s + 1) + " is outside steps " + std::to_string(_from + 1) + "-" + std::to_string(_to));
				return false;
			}
			return true;
		}

		std::optional<MmPattern> step(MmPattern _p, const In& _in)
		{
			std::string error;
			const auto v = stepValue(_in.a.value("v"), error);
			if(!v || !setStep(_p, trackOf(_in.a.integer("t")), static_cast<size_t>(_in.a.integer("s")), *v, error))
			{
				_in.a.error(error);
				return {};
			}
			return _p;
		}

		std::optional<MmPattern> slide(MmPattern _p, const In& _in)
		{
			setBit(slideOf(_p, trackOf(_in.a.integer("t"))), static_cast<size_t>(_in.a.integer("s")), _in.a.flag("on"));
			return _p;
		}

		std::optional<MmPattern> swingStep(MmPattern _p, const In& _in)
		{
			setBit(swingOf(_p, trackOf(_in.a.integer("t"))), static_cast<size_t>(_in.a.integer("s")), _in.a.flag("on"));
			return _p;
		}

		// the page of a lock's parameter: a synth track names one of 0-6, a MIDI track's is 7
		std::optional<int> lockPage(const In& _in, const Track& _tr)
		{
			if(_tr.midi)
			{
				if(_in.a.has("page") && _in.a.integer("page") != 7)
				{
					_in.a.error("page: a MIDI track's values are on its MIDI page (7)");
					return {};
				}
				return 7;
			}
			if(!_in.a.has("page") || _in.a.integer("page") > 6)
			{
				_in.a.error("page: a synth track's page is 0-6 (SYN AMP FLT EFX LF1 LF2 LF3)");
				return {};
			}
			return _in.a.integer("page");
		}

		std::optional<MmPattern> lock(MmPattern _p, const In& _in)
		{
			const auto tr = trackOf(_in.a.integer("t"));
			const auto page = lockPage(_in, tr);
			if(!page)
				return {};
			const auto s = static_cast<size_t>(_in.a.integer("s"));
			const auto lp = lockParam(tr, *page, _in.a.integer("i"));
			if(!_in.a.has("v"))
			{
				clearLock(_p, lp, s);
				return _p;
			}
			if(!ed::mmStepSet(holds(_p, tr), s))
			{
				_in.a.error("A step without a trig cannot hold a lock");
				return {};
			}
			if(!setLock(_p, lp, s, static_cast<uint8_t>(_in.a.integer("v"))))
			{
				_in.a.error("All 62 locked parameters are in use. Clear one before you lock a new parameter.");
				return {};
			}
			return _p;
		}

		std::optional<MmPattern> clearLane(MmPattern _p, const In& _in)
		{
			const auto tr = trackOf(_in.a.integer("t"));
			const auto page = lockPage(_in, tr);
			if(!page)
				return {};
			if(const auto r = ed::mmLockRow(_p, lockParam(tr, *page, _in.a.integer("i"))); r >= 0)
				removeRow(_p, static_cast<size_t>(r));
			return _p;
		}

		std::optional<MmPattern> clearLocks(MmPattern _p, const In& _in)
		{
			const int t = _in.a.integer("t");
			const auto locks = trackLocks(_p, trackOf(t));
			for(const auto& lp : locks)
				removeRow(_p, static_cast<size_t>(ed::mmLockRow(_p, lp)));
			_in.note = locks.empty() ? trackName(t) + " has no locks" : "Cleared every lock of " + trackName(t);
			return _p;
		}

		std::optional<MmPattern> clearPattern(MmPattern _p, const In& _in)
		{
			std::string error;
			for(int t = 0; t < 12; ++t)
			{
				const auto tr = trackOf(t);
				for(size_t s = 0; s < MmPattern::g_steps; ++s)
					setStep(_p, tr, s, StepValue{}, error);
				slideOf(_p, tr) = 0;
			}
			_in.note = "Cleared the pattern: every track's notes, slides and locks";
			return _p;
		}

		// The range edit (the generators, every-n fill): rows [{t, steps: [[s, v]...], slide?, locks?}].
		std::optional<MmPattern> steps(MmPattern _p, const In& _in)
		{
			const int from = _in.a.integer("from"), to = _in.a.integer("to");
			if(to <= from)
			{
				_in.a.error("Empty step range");
				return {};
			}
			const auto& rows = _in.a.value("rows")->asArray();
			if(rows.empty() || rows.size() > 12)
			{
				_in.a.error("rows: 1 to 12 rows");
				return {};
			}
			const auto intIn = [](const Value& _x, const int _min, const int _max) -> std::optional<int>
			{
				if(!_x.isNumber() || _x.asNumber() != std::floor(_x.asNumber()) || _x.asNumber() < _min || _x.asNumber() > _max)
					return {};
				return static_cast<int>(_x.asNumber());
			};
			uint32_t seen = 0;
			for(size_t j = 0; j < rows.size(); ++j)
			{
				const auto where = "rows[" + std::to_string(j) + "]";
				const auto& r = rows[j];
				const auto t = r.isObject() && r.find("t") ? intIn(*r.find("t"), 0, 11) : std::nullopt;
				const auto* list = t ? r.find("steps") : nullptr;
				if(!t || !list || !list->isArray())
				{
					_in.a.error(where + ": expected {t: 0-11, steps: [[s, v]...]}");
					return {};
				}
				if(seen >> *t & 1)
				{
					_in.a.error(where + ": " + trackName(*t) + " is in two rows");
					return {};
				}
				seen |= 1u << *t;
				const auto tr = trackOf(*t);
				std::map<int, StepValue> want;
				for(const auto& e : list->asArray())
				{
					const auto s = e.isArray() && e.asArray().size() == 2 ? intIn(e.asArray()[0], from, to - 1) : std::nullopt;
					std::string error;
					const auto v = s ? stepValue(&e.asArray()[1], error) : std::nullopt;
					if(!v)
					{
						_in.a.error(where + ".steps: " + (error.empty() ? "[s, v] with s in the range" : error));
						return {};
					}
					want[*s] = *v;
				}
				for(int s = from; s < to; ++s)
				{
					std::string error;
					const auto it = want.find(s);
					if(!setStep(_p, tr, static_cast<size_t>(s), it == want.end() ? StepValue{} : it->second, error))
					{
						_in.a.error(error);
						return {};
					}
				}
				if(const auto* sl = r.find("slide"); sl && sl->isArray())
				{
					auto& bits = slideOf(_p, tr);
					bits &= ~deskCore::stepRange(static_cast<size_t>(from), static_cast<size_t>(to));
					for(const auto& x : sl->asArray())
						if(const auto s = intIn(x, from, to - 1))
							bits |= ed::mmStepBit(static_cast<size_t>(*s));
				}
				if(const auto* lk = r.find("locks"); lk && lk->isArray())
				{
					for(const auto& lp : trackLocks(_p, tr))
					{
						for(int s = from; s < to; ++s)
							if(const auto row = ed::mmLockRow(_p, lp); row >= 0)
								_p.lockRows[static_cast<size_t>(row)][static_cast<size_t>(s)] = MmPattern::g_noLock;
						dropEmptyRow(_p, lp);
					}
					for(const auto& x : lk->asArray())
					{
						const auto& a = x.isArray() && x.asArray().size() == 4 ? x.asArray() : Value::Array{};
						const auto pg = a.empty() ? std::nullopt : intIn(a[0], tr.midi ? 7 : 0, tr.midi ? 7 : 6);
						const auto i = a.empty() ? std::nullopt : intIn(a[1], 0, 7);
						const auto s = a.empty() ? std::nullopt : intIn(a[2], from, to - 1);
						const auto v = a.empty() ? std::nullopt : intIn(a[3], 0, 127);
						if(!pg || !i || !s || !v)
						{
							_in.a.error(where + ".locks: [page, i, s, v] with s in the range");
							return {};
						}
						if(!setLock(_p, lockParam(tr, *pg, *i), static_cast<size_t>(*s), static_cast<uint8_t>(*v)))
						{
							_in.a.error("All 62 locked parameters are in use");
							return {};
						}
					}
				}
			}
			_in.note = rows.size() == 1 ? "Set the steps of " + trackName(rows[0].find("t") ? static_cast<int>(rows[0].find("t")->asNumber()) : 0)
				: "Set the steps of " + std::to_string(rows.size()) + " tracks";
			return _p;
		}

		std::optional<MmPattern> rotate(MmPattern _p, const In& _in)
		{
			const int t = _in.a.integer("t"), by = _in.a.integer("by");
			const auto tr = trackOf(t);
			const size_t len = static_cast<size_t>(patLength(_p));
			if(by % static_cast<int>(len) == 0)
				return _p;
			for(auto* m : stepMasks(_p, tr))
				*m = deskCore::rotatedBits(*m, by, len);
			slideOf(_p, tr) = deskCore::rotatedBits(slideOf(_p, tr), by, len);
			const auto moveEntries = [&](auto _pool)
			{
				for(size_t i = 0; i < _pool.count; ++i)
				{
					if(_pool.e[i] == 0xffff)
						continue;
					auto x = ed::mmNoteEntry(_pool.e[i]);
					if(x.track == tr.t && x.step < len)
						x.step = static_cast<uint8_t>(deskCore::rotatedStep(x.step, by, len));
					_pool.e[i] = ed::mmNoteEntryWord(x);
				}
				_pool.sort();
			};
			if(tr.midi)
				moveEntries(midiPool(_p));
			else
			{
				deskCore::rotateRow(_p.notes[tr.t], by, len);
				moveEntries(chordPool(_p));
			}
			for(const auto& lp : trackLocks(_p, tr))
				if(const auto row = ed::mmLockRow(_p, lp); row >= 0)
					deskCore::rotateRow(_p.lockRows[static_cast<size_t>(row)], by, len);
			_in.note = "Rotated " + trackName(t) + (by > 0 ? " later" : " earlier");
			return _p;
		}

		std::optional<MmPattern> doublePattern(MmPattern _p, const In& _in)
		{
			const size_t len = _p.length;
			if(len * 2 > MmPattern::g_steps)
			{
				_in.a.error("A pattern of " + std::to_string(len) + " steps cannot double: 64 steps is the longest");
				return {};
			}
			for(auto* masks : {&_p.amp, &_p.filter, &_p.lfo, &_p.noteOff, &_p.midiTrig, &_p.midiNoteOff, &_p.pitch, &_p.chord, &_p.midiNote,
				&_p.slide, &_p.swing, &_p.midiSlide, &_p.midiSwing})
				for(auto& bits : *masks)
					bits = deskCore::doubledBits(bits, len);
			for(auto& row : _p.notes)
				deskCore::doubleRow(row, len, MmPattern::g_noNote);
			for(size_t r = 0; r < _p.lockRowCount && r < MmPattern::g_lockRows; ++r)
				deskCore::doubleRow(_p.lockRows[r], len, MmPattern::g_noLock);
			// the note pools: the new half's entries are copies of the first half's (a chord note belongs to a chord step)
			const auto copyEntries = [&](auto _pool, const auto _attached, const char* _what)
			{
				_pool.remove([&](const ed::MmNoteEntry& _e) { return _e.step >= len && _e.step < 2 * len; });
				std::vector<ed::MmNoteEntry> add;
				for(size_t i = 0; i < _pool.count; ++i)
					if(_pool.e[i] != 0xffff)
					{
						const auto x = ed::mmNoteEntry(_pool.e[i]);
						if(x.step < len && _attached(x))
							add.push_back({x.track, static_cast<uint8_t>(x.step + len), x.note});
					}
				for(const auto& x : add)
					if(!_pool.insert(x))
					{
						_in.a.error(std::string("The doubled pattern holds more ") + _what + " than the machine can");
						return false;
					}
				store(_p, _pool);
				return true;
			};
			const auto chord = _p.chord;
			if(!copyEntries(chordPool(_p), [&](const ed::MmNoteEntry& _x) { return _x.track < 6 && ed::mmStepSet(chord[_x.track], _x.step); }, "chord notes")
				|| !copyEntries(midiPool(_p), [](const ed::MmNoteEntry& _x) { return _x.track < 6; }, "MIDI notes"))
				return {};
			_p.length = static_cast<uint8_t>(len * 2);
			_in.note = "Doubled the pattern: " + std::to_string(len) + " to " + std::to_string(len * 2) + " steps, the new half a copy";
			return _p;
		}

		std::optional<MmPattern> length(MmPattern _p, const In& _in)
		{
			_p.length = static_cast<uint8_t>(_in.a.integer("v"));
			return _p;
		}

		std::optional<MmPattern> speed(MmPattern _p, const In& _in)
		{
			static const std::array<const char*, 4> names{"1X", "2X", "3/4X", "3/2X"};
			for(size_t i = 0; i < names.size(); ++i)
				if(_in.a.text("v") == names[i])
					_p.multiplier = static_cast<uint8_t>(i);
			return _p;
		}

		std::optional<MmPattern> swing(MmPattern _p, const In& _in)
		{
			_p.swingAmount = static_cast<uint8_t>(_in.a.integer("v") - 50);
			return _p;
		}

		std::optional<MmPattern> transpose(MmPattern _p, const In& _in)
		{
			if(!_in.a.has("t"))
			{
				if(!_in.a.has("v") || _in.a.has("scale") || _in.a.has("key"))
				{
					_in.a.error("the pattern's transpose: v only (a track's: t)");
					return {};
				}
				_p.patternTranspose = static_cast<int8_t>(_in.a.integer("v"));
				return _p;
			}
			const auto tr = trackOf(_in.a.integer("t"));
			auto& x = tr.midi ? _p.midiTranspose : _p.transpose;
			if(_in.a.has("v"))
				x.track[tr.t] = static_cast<int8_t>(_in.a.integer("v"));
			if(_in.a.has("scale"))
				x.scale[tr.t] = static_cast<uint8_t>(_in.a.integer("scale"));
			if(_in.a.has("key"))
				x.key[tr.t] = static_cast<uint8_t>(_in.a.integer("key"));
			return _p;
		}

		std::optional<MmPattern> arp(MmPattern _p, const In& _in)
		{
			const auto tr = trackOf(_in.a.integer("t"));
			auto& a = tr.midi ? _p.midiArp : _p.arp;
			const auto& f = _in.a.text("field");
			const int v = _in.a.integer("v");
			const auto within = [&](const int _min, const int _max)
			{
				if(v >= _min && v <= _max)
					return true;
				_in.a.error(f + ": " + std::to_string(v) + " is outside " + std::to_string(_min) + ".." + std::to_string(_max));
				return false;
			};
			const auto u8 = static_cast<uint8_t>(v);
			if(f == "play") { if(!within(0, 4)) return {}; a.playOjmp[tr.t] = static_cast<uint8_t>((a.playOjmp[tr.t] & 0xf8) | u8); }
			else if(f == "ojmp") { if(!within(0, 15)) return {}; a.playOjmp[tr.t] = static_cast<uint8_t>((a.playOjmp[tr.t] & 0x0f) | (u8 << 4)); }
			else if(f == "mode") { if(!within(0, 3)) return {}; a.mode[tr.t] = u8; }
			else if(f == "range") { if(!within(0, 8)) return {}; a.range[tr.t] = u8; }
			else if(f == "speed") { if(!within(0, 127)) return {}; a.speed[tr.t] = u8; }
			else if(f == "length") { if(!within(1, 16)) return {}; a.length[tr.t] = u8; }
			else if(f == "trigs")
			{
				if(tr.midi)
				{
					_in.a.error("trigs: a MIDI track's arpeggiator has no envelope switches");
					return {};
				}
				if(!within(0, 7)) return {};
				a.trigs[tr.t] = u8;
			}
			else if(f == "step")
			{
				if(!_in.a.has("i"))
				{
					_in.a.error("step: i, the arpeggiator's step 0-15");
					return {};
				}
				if(v != 255 && !within(0, 127))
					return {};
				a.steps[tr.t][static_cast<size_t>(_in.a.integer("i"))] = u8;
			}
			return _p;
		}

		// ---- blocks of steps (DESIGN-step-selection.md §7): steps [from, to) of tracks t to t + n - 1 (n 1 without it), a
		// selection or a track page; where a block put down lands is deskCore::blockLanding, as on the Machinedrum ----
		struct StepRange
		{
			int track, rows, from, to;
		};
		std::string tracksName(const int _t, const int _rows)
		{
			return _rows == 1 ? trackName(_t) : trackName(_t) + "-" + trackName(_t + _rows - 1);
		}
		std::string rangeName(const StepRange& _r)
		{
			return tracksName(_r.track, _r.rows) + ", steps " + std::to_string(_r.from + 1) + "-" + std::to_string(_r.to);
		}
		std::optional<StepRange> stepRange(const In& _in)
		{
			const int t = _in.a.integer("t"), n = _in.a.has("n") ? _in.a.integer("n") : 1, from = _in.a.integer("from"), to = _in.a.integer("to");
			if(t + n > 12)
			{
				_in.a.error("n: " + std::to_string(n) + " tracks from " + trackName(t) + " go past M6");
				return {};
			}
			if(to <= from)
			{
				_in.a.error("Empty step range");
				return {};
			}
			return StepRange{t, n, from, to};
		}

		// The block's steps as values, per track: its steps, slides and every lock.
		Clipboard::Steps takeSteps(const MmPattern& _p, const StepRange& _r)
		{
			Clipboard::Steps c;
			c.length = static_cast<size_t>(_r.to - _r.from);
			for(int t = _r.track; t < _r.track + _r.rows; ++t)
			{
				const auto tr = trackOf(t);
				Clipboard::Steps::Row row;
				row.midi = tr.midi;
				const auto locks = trackLocks(_p, tr);
				const auto slide = tr.midi ? _p.midiSlide[tr.t] : _p.slide[tr.t];
				for(int s = _r.from; s < _r.to; ++s)
				{
					const auto rel = static_cast<uint8_t>(s - _r.from);
					row.steps.push_back(stepAt(_p, tr, static_cast<size_t>(s)));
					if(ed::mmStepSet(slide, static_cast<size_t>(s)))
						row.slide |= ed::mmStepBit(rel);
					for(const auto& lp : locks)
					{
						const auto lr = ed::mmLockRow(_p, lp);
						if(lr < 0)
							continue;
						const auto v = _p.lockRows[static_cast<size_t>(lr)][static_cast<size_t>(s)];
						if(v != MmPattern::g_noLock)
							row.locks[{lp.page, lp.param}][rel] = v;
					}
				}
				c.rows.push_back(std::move(row));
			}
			return c;
		}

		// The kit the pattern plays, for the parameters its machines have: the kit that plays when it is there.
		const MmKit* kitOf(const Documents& _docs, const MmPattern& _p)
		{
			if(_docs.working)
				return &_docs.working->kit;
			const auto it = _docs.kits.find(_p.kit);
			return it == _docs.kits.end() ? nullptr : &it->second;
		}

		// What a block put down did: where it landed, and what it left out.
		struct Put
		{
			deskCore::BlockLanding land;
			size_t otherKind = 0, noParam = 0, full = 0;
		};

		// Puts a block down with its first step at _at on track _track: each row replaces what the steps under it
		// held (its steps, slides and locks), _span steps from _at (the block's length; a page paste may ask for more:
		// the steps past the block are cleared). It stops at the pattern's length and at M6; a row lands only on a
		// track of its kind (synth on synth, MIDI on MIDI; others are counted); a lock of a synthesis parameter the
		// track's machine has not, or one that needs a new row while all 62 are in use, is skipped (counted). False
		// (and the error) when no row has a track of its kind, or a note pool is full.
		bool putSteps(MmPattern& _p, const Clipboard::Steps& _c, const int _track, const int _at, const size_t _span, const In& _in, Put& _put)
		{
			_put.land = deskCore::blockLanding(_c.rows.size(), _span, static_cast<size_t>(_track), static_cast<size_t>(_at), 12,
				static_cast<size_t>(patLength(_p)));
			const auto* kit = kitOf(_in.docs, _p);
			for(size_t r = 0; r < _put.land.rows; ++r)
			{
				const auto& row = _c.rows[r];
				const auto tr = trackOf(_track + static_cast<int>(r));
				if(row.midi != tr.midi)
				{
					++_put.otherKind;
					continue;
				}
				// what the steps under the row held goes: their locks too (a step that stays a trig would keep them)
				for(size_t k = 0; k < _put.land.steps; ++k)
					clearStepLocks(_p, tr, static_cast<size_t>(_at) + k);
				std::string error;
				for(size_t k = 0; k < _put.land.steps; ++k)
				{
					const auto s = static_cast<size_t>(_at) + k;
					if(!setStep(_p, tr, s, k < row.steps.size() ? row.steps[k] : StepValue{}, error))
					{
						_in.a.error(error);
						return false;
					}
					setBit(slideOf(_p, tr), s, row.slide >> k & 1);
				}
				const auto* machine = kit && !tr.midi ? ed::mmMachine(kit->machines[tr.t]) : nullptr;
				for(const auto& [param, steps] : row.locks)
				{
					// a synthesis page parameter this track's machine does not have is skipped
					if(param.first == 0 && machine && !(machine->synth[param.second] && *machine->synth[param.second]))
					{
						++_put.noParam;
						continue;
					}
					for(const auto& [rel, v] : steps)
					{
						const auto s = static_cast<size_t>(_at) + rel;
						if(rel >= _put.land.steps || !ed::mmStepSet(holds(_p, tr), s))
							continue;
						if(!setLock(_p, lockParam(tr, param.first, param.second), s, v))
							++_put.full;
					}
				}
			}
			if(_put.land.rows && _put.otherKind == _put.land.rows)
			{
				_in.a.error("Track pages paste between synth tracks or between MIDI tracks");
				return false;
			}
			return true;
		}
		std::string putNote(const std::string& _verb, const Put& _put, const int _track, const int _at, const MmPattern& _p)
		{
			auto note = deskCore::blockNote(_verb, tracksName(_track, static_cast<int>(_put.land.rows)), static_cast<size_t>(_at), _put.land,
				static_cast<size_t>(patLength(_p)), "M6");
			if(_put.otherKind)
				note += ". " + std::to_string(_put.otherKind) + " track(s) of the other kind left out: synth steps paste onto synth tracks, MIDI steps onto MIDI tracks";
			if(_put.noParam)
				note += ". " + std::to_string(_put.noParam) + " lock(s) skipped: this machine has no such parameter";
			if(_put.full)
				note += ". " + std::to_string(_put.full) + " lock(s) skipped: all 62 locked parameters are in use";
			return note;
		}
		// The first step a block is put down at: within the pattern's length.
		bool putAt(const MmPattern& _p, const In& _in, const int _at)
		{
			if(_at < patLength(_p))
				return true;
			_in.a.error("Step " + std::to_string(_at + 1) + " is past the pattern's length (" + std::to_string(patLength(_p)) + ")");
			return false;
		}

		std::optional<MmPattern> clearSteps(MmPattern _p, const In& _in)
		{
			const auto r = stepRange(_in);
			if(!r)
				return {};
			std::string error;
			for(int t = r->track; t < r->track + r->rows; ++t)
			{
				const auto tr = trackOf(t);
				for(int s = r->from; s < r->to; ++s)
					setStep(_p, tr, static_cast<size_t>(s), StepValue{}, error);
				slideOf(_p, tr) &= ~deskCore::stepRange(static_cast<size_t>(r->from), static_cast<size_t>(r->to));
			}
			_in.note = "Cleared " + rangeName(*r);
			return _p;
		}

		std::optional<MmPattern> copySteps(MmPattern _p, const In& _in)
		{
			const auto r = stepRange(_in);
			if(!r)
				return {};
			_in.clip.steps = takeSteps(_p, *r);
			_in.note = "Copied " + rangeName(*r);
			return _p;
		}

		// The clipboard's block with its first step at from on track t; to (a page paste): [from, to) is cleared
		// past the block.
		std::optional<MmPattern> pasteSteps(MmPattern _p, const In& _in)
		{
			if(!_in.clip.steps)
			{
				_in.a.error("Copy a track page first");
				return {};
			}
			const auto& c = *_in.clip.steps;
			const int t = _in.a.integer("t"), from = _in.a.integer("from");
			const int to = _in.a.has("to") ? _in.a.integer("to") : from + static_cast<int>(c.length);
			if(to <= from)
			{
				_in.a.error("Empty step range");
				return {};
			}
			Put put;
			if(!putAt(_p, _in, from) || !putSteps(_p, c, t, from, static_cast<size_t>(to - from), _in, put))
				return {};
			_in.note = putNote("Pasted into", put, t, from, _p);
			return _p;
		}

		// A block of this pattern copied to another place in it (step at, track dt; its own track without dt), the
		// clipboard untouched: duplicate (at = to) and the ⌘-drag of a selection. The block is read before it is put
		// down, so a drop over its own source is fine.
		std::optional<MmPattern> copyStepsTo(MmPattern _p, const In& _in)
		{
			const auto r = stepRange(_in);
			if(!r)
				return {};
			const int at = _in.a.integer("at"), dt = _in.a.has("dt") ? _in.a.integer("dt") : r->track;
			const auto block = takeSteps(_p, *r);
			Put put;
			if(!putAt(_p, _in, at) || !putSteps(_p, block, dt, at, block.length, _in, put))
				return {};
			_in.note = putNote("Copied " + rangeName(*r) + " to", put, dt, at, _p);
			return _p;
		}

		const Edits<MmPattern>& patternEdits()
		{
			static const Edits<MmPattern> edits{
				{"step", step}, {"slide", slide}, {"swingStep", swingStep}, {"lock", lock}, {"clearLane", clearLane},
				{"clearLocks", clearLocks}, {"clearPattern", clearPattern}, {"steps", steps}, {"rotate", rotate},
				{"doublePattern", doublePattern}, {"length", length}, {"speed", speed}, {"swing", swing}, {"transpose", transpose},
				{"arp", arp}, {"clearSteps", clearSteps}, {"copySteps", copySteps}, {"pasteSteps", pasteSteps}, {"copyStepsTo", copyStepsTo}};
			return edits;
		}

		// ---- the kit that plays (its working kit) ----
		std::optional<MmKit> level(MmKit _k, const In& _in)
		{
			_k.levels[static_cast<size_t>(_in.a.integer("t"))] = static_cast<uint8_t>(_in.a.integer("v"));
			return _k;
		}

		std::optional<MmKit> route(MmKit _k, const In& _in)
		{
			auto& r = _k.routing[static_cast<size_t>(_in.a.integer("t"))];
			r = ed::mmRouting(static_cast<uint8_t>(_in.a.integer("out")), ed::mmRoutingInput(r));
			return _k;
		}

		std::optional<MmKit> input(MmKit _k, const In& _in)
		{
			auto& r = _k.routing[static_cast<size_t>(_in.a.integer("t"))];
			r = ed::mmRouting(ed::mmRoutingOutputs(r), static_cast<uint8_t>(_in.a.integer("v")));
			return _k;
		}

		std::optional<MmKit> param(MmKit _k, const In& _in)
		{
			const auto tr = trackOf(_in.a.integer("t"));
			const auto page = lockPage(_in, tr);
			if(!page)
				return {};
			const auto i = static_cast<size_t>(_in.a.integer("i"));
			const auto v = static_cast<uint8_t>(_in.a.integer("v"));
			if(tr.midi)
				_k.tracks[tr.t].midi[i] = v;
			else
				_k.tracks[tr.t].pages[static_cast<size_t>(*page)][i] = v;
			return _k;
		}

		std::optional<MmKit> trigPos(MmKit _k, const In& _in)
		{
			const int t = _in.a.integer("t");
			if(_in.a.has("v") && _in.a.integer("v") == t)
			{
				_in.a.error("A track does not forward its notes to itself");
				return {};
			}
			_k.trigPos[static_cast<size_t>(t)] = _in.a.has("v") ? static_cast<uint8_t>(_in.a.integer("v")) : MmKit::g_noTrigPos;
			return _k;
		}

		uint8_t withBit(const uint8_t _mask, const int _t, const bool _on)
		{
			return static_cast<uint8_t>(_on ? _mask | (1u << _t) : _mask & ~(1u << _t));
		}

		std::optional<MmKit> legato(MmKit _k, const In& _in)
		{
			const int t = _in.a.integer("t");
			const auto& env = _in.a.text("env");
			auto& mask = env == "amp" ? _k.legatoAmp : env == "filter" ? _k.legatoFilter : _k.legatoLfo;
			mask = withBit(mask, t, _in.a.flag("on"));
			return _k;
		}

		std::optional<MmKit> portamento(MmKit _k, const In& _in)
		{
			// set = ALWAYS, clear = ONLY LEGATO (mm-data-contract.md 4.2, trackMasks)
			_k.portamentoMask = withBit(_k.portamentoMask, _in.a.integer("t"), _in.a.text("v") == "always");
			return _k;
		}


		// ---- a track's sound: the machine and the values it starts with (the page's own defaults, DESIGN-UNIFY.md 4.6:
		// the intent cases pin the page's writes to these) ----
		// A SYN slot's value when a machine is assigned, by the slot's name (page units: an enumeration's index).
		int synDefault(const char* _name)
		{
			static const std::map<std::string, int> d{{"TUNE", 64}, {"INP", 100}, {"MIX", 64}, {"UNIL", 40}, {"UNIW", 20}, {"UNIX", 0},
				{"SUB1", 30}, {"PW", 64}, {"VOC1", 64}, {"VOC2", 64}, {"V-SW", 1}, {"CVOL", 100}, {"LP", 127}, {"GATE", 127},
				{"DEC", 64}, {"1ENV", 80}, {"TONE", 64}};
			const auto it = d.find(_name);
			return it == d.end() ? 0 : it->second;
		}
		// An enumeration's index as the value the firmware stores (the band's lowest value); a plain value as it is.
		uint8_t enumRaw(const int _v, const size_t _n)
		{
			return static_cast<uint8_t>(_n ? ed::mmEnumValue(std::min<int>(_v, static_cast<int>(_n) - 1), static_cast<int>(_n)) : std::clamp(_v, 0, 127));
		}
		// The machine's SYN page as it starts.
		std::array<uint8_t, 8> synDefaults(const uint8_t _machine)
		{
			std::array<uint8_t, 8> v{};
			const auto* m = ed::mmMachine(_machine);
			for(uint8_t i = 0; m && i < 8; ++i)
				if(m->synth[i] && *m->synth[i])
					v[i] = enumRaw(synDefault(m->synth[i]), ed::mmSynthEnum(_machine, i).size());
			return v;
		}
		// The fixed pages' neutral values (AMP FLT EFX, then an LFO page), page units; an LFO's PAGE DEST TRIG WAVE MULT
		// are enumerations.
		constexpr std::array<std::array<uint8_t, 8>, 4> g_neutralPages{{
			{0, 40, 64, 20, 64, 100, 64, 0},	// AMP
			{0, 127, 0, 0, 0, 40, 0, 0},		// FLT
			{64, 64, 0, 32, 64, 0, 0, 127},		// EFX
			{2, 1, 1, 0, 1, 32, 0, 0}}};		// LF1-3
		std::array<uint8_t, 8> neutralPage(const uint8_t _page)
		{
			if(_page < 4)
				return g_neutralPages[_page - 1];
			const std::array<size_t, 5> n{ed::mmLfoPages().size(), 8, ed::mmLfoTrigs().size(), ed::mmLfoWaves().size(), ed::mmLfoMults().size()};
			auto v = g_neutralPages[3];
			for(size_t i = 0; i < n.size(); ++i)
				v[i] = enumRaw(v[i], n[i]);
			return v;
		}
		// Track _t gets machine _id with its SYN defaults; without _keepFx its other pages go neutral too. A processing
		// machine on a track that had none listens (INP A+B on track 1, the neighbour on the others) and lets the sound
		// through (AMP DEC and REL at 127).
		void assignMachine(MmKit& _k, const size_t _t, const uint8_t _id, const bool _keepFx)
		{
			const auto* was = ed::mmMachine(_k.machines[_t]);
			const auto* m = ed::mmMachine(_id);
			_k.machines[_t] = _id;
			_k.tracks[_t].pages[0] = synDefaults(_id);
			if(!_keepFx)
				for(uint8_t pg = 1; pg < 7; ++pg)
					_k.tracks[_t].pages[pg] = neutralPage(pg);
			if(m && m->fx && !(was && was->fx))
			{
				_k.routing[_t] = ed::mmRouting(ed::mmRoutingOutputs(_k.routing[_t]), _t == 0 ? 3 : 0);
				_k.tracks[_t].pages[1][2] = 127;
				_k.tracks[_t].pages[1][3] = 127;
			}
		}

		std::optional<MmKit> machine(MmKit _k, const In& _in)
		{
			const auto t = static_cast<size_t>(_in.a.integer("t"));
			const auto id = _in.a.integer("model");
			const auto* m = ed::mmMachine(static_cast<uint8_t>(id));
			if(!m)
			{
				_in.a.error("model: " + std::to_string(id) + " is not an OS 1.32B machine");
				return {};
			}
			assignMachine(_k, t, static_cast<uint8_t>(id), !_in.a.has("keepFx") || _in.a.flag("keepFx"));
			_in.note = trackName(static_cast<int>(t)) + " is " + m->name;
			return _k;
		}

		// CLEAR MACHINE: GND-SIN with every page at its start.
		std::optional<MmKit> clearSound(MmKit _k, const In& _in)
		{
			const auto t = static_cast<size_t>(_in.a.integer("t"));
			assignMachine(_k, t, 1, false);
			_in.note = trackName(static_cast<int>(t)) + " is GND-SIN again";
			return _k;
		}

		std::optional<MmKit> copySound(MmKit _k, const In& _in)
		{
			const auto t = static_cast<size_t>(_in.a.integer("t"));
			Clipboard::Sound c;
			c.machine = _k.machines[t];
			c.pages = _k.tracks[t].pages;
			_in.clip.sound = c;
			const auto* m = ed::mmMachine(c.machine);
			_in.note = "Copied " + trackName(static_cast<int>(t)) + ": " + (m ? m->name : "its machine") + " and its seven pages";
			return _k;
		}

		std::optional<MmKit> pasteSound(MmKit _k, const In& _in)
		{
			if(!_in.clip.sound)
			{
				_in.a.error("Copy a machine first");
				return {};
			}
			const auto t = static_cast<size_t>(_in.a.integer("t"));
			_k.machines[t] = _in.clip.sound->machine;
			_k.tracks[t].pages = _in.clip.sound->pages;
			const auto* m = ed::mmMachine(_k.machines[t]);
			_in.note = "Pasted " + std::string(m ? m->name : "the machine") + " onto " + trackName(static_cast<int>(t));
			return _k;
		}

		// Many kit values at once (MUTATE, a screen's handle, Control All): [[t, page, i, v]...], as param's.
		std::optional<MmKit> params(MmKit _k, const In& _in)
		{
			const auto& values = _in.a.value("values")->asArray();
			if(values.size() > 6 * 8 * 8)
			{
				_in.a.error("values: at most 384 (every value of the kit once)");
				return {};
			}
			for(size_t j = 0; j < values.size(); ++j)
			{
				const auto& e = values[j];
				const auto& a = e.isArray() && e.asArray().size() == 4 ? e.asArray() : Value::Array{};
				const auto num = [&](const size_t _i, const int _min, const int _max) -> std::optional<int>
				{
					if(a.empty() || !a[_i].isNumber() || a[_i].asNumber() != std::floor(a[_i].asNumber()) || a[_i].asNumber() < _min || a[_i].asNumber() > _max)
						return {};
					return static_cast<int>(a[_i].asNumber());
				};
				const auto t = num(0, 0, 11), pg = num(1, 0, 7), i = num(2, 0, 7), v = num(3, 0, 127);
				if(!t || !pg || !i || !v || (*t >= 6) != (*pg == 7))
				{
					_in.a.error("values[" + std::to_string(j) + "]: expected [t 0-11, page (a synth track 0-6, a MIDI track 7), i 0-7, v 0-127]");
					return {};
				}
				if(*t >= 6)
					_k.tracks[static_cast<size_t>(*t - 6)].midi[static_cast<size_t>(*i)] = static_cast<uint8_t>(*v);
				else
					_k.tracks[static_cast<size_t>(*t)].pages[static_cast<size_t>(*pg)][static_cast<size_t>(*i)] = static_cast<uint8_t>(*v);
			}
			return _k;
		}

		// ASSIGN (the joystick, velocity and key): a source's row (src 0 JOY R, 1 JOY L, 2 JOY U, 3 JOY D, 4 VEL, 5 KEY;
		// row 0-1) gets its page, dest and add; and the track's MIRROR, HPF and LPF switches.
		std::optional<MmKit> assign(MmKit _k, const In& _in)
		{
			const auto t = static_cast<size_t>(_in.a.integer("t"));
			const bool row = _in.a.has("page") || _in.a.has("dest") || _in.a.has("add");
			const bool flags = _in.a.has("mirror") || _in.a.has("hpf") || _in.a.has("lpf");
			if(!row && !flags)
			{
				_in.a.error("assign: nothing to set (page, dest, add with src and row; or mirror, hpf, lpf)");
				return {};
			}
			if(row)
			{
				if(!_in.a.has("src") || !_in.a.has("row"))
				{
					_in.a.error("assign: page, dest and add belong to a source's row (src, row)");
					return {};
				}
				const auto at = static_cast<size_t>(_in.a.integer("src") * 2 + _in.a.integer("row"));
				if(_in.a.has("page")) _k.assignPage[t][at] = static_cast<uint8_t>(_in.a.integer("page"));
				if(_in.a.has("dest")) _k.assignDest[t][at] = static_cast<uint8_t>(_in.a.integer("dest"));
				if(_in.a.has("add")) _k.assignAdd[t][at] = static_cast<int8_t>(_in.a.integer("add"));
			}
			const auto bit = [&](uint8_t& _mask, const char* _key)
			{
				if(_in.a.has(_key))
					_mask = static_cast<uint8_t>(_in.a.flag(_key) ? _mask | (1u << t) : _mask & ~(1u << t));
			};
			bit(_k.mirrorMask, "mirror");
			bit(_k.hpfMask, "hpf");
			bit(_k.lpfMask, "lpf");
			return _k;
		}

		// MULTI ENV: one set on the screen, each track's copy in the kit (ATK DEC SUS REL PORT, then the sixth).
		std::optional<MmKit> multiEnv(MmKit _k, const In& _in)
		{
			const auto i = static_cast<size_t>(_in.a.integer("i"));
			for(auto& tr : _k.tracks)
				tr.multiEnv[i] = static_cast<uint8_t>(_in.a.integer("v"));
			return _k;
		}

		// MULTI TRIG: its mode, split key, split track and timing.
		std::optional<MmKit> multiTrig(MmKit _k, const In& _in)
		{
			if(_in.a.has("mode")) _k.multiTrigMode = static_cast<uint8_t>(_in.a.integer("mode"));
			if(_in.a.has("splitKey")) _k.splitKey = static_cast<uint8_t>(_in.a.integer("splitKey"));
			if(_in.a.has("splitTrack")) _k.splitTrack = static_cast<uint8_t>(_in.a.integer("splitTrack"));
			if(_in.a.has("timing")) _k.multiTrigTiming = static_cast<uint8_t>(_in.a.integer("timing"));
			return _k;
		}

		// A kit name: 7-bit printable, upper case, at most 11 characters.
		std::optional<std::array<uint8_t, MmKit::g_nameSize>> kitNameBytes(const std::string& _name, std::string& _error)
		{
			if(_name.size() > MmKit::g_nameSize)
			{
				_error = "A kit name has at most 11 characters";
				return {};
			}
			std::array<uint8_t, MmKit::g_nameSize> n{};
			for(size_t i = 0; i < _name.size(); ++i)
			{
				const auto c = static_cast<unsigned char>(_name[i]);
				if(c < 0x20 || c > 0x7e)
				{
					_error = "name: 7-bit printable characters only";
					return {};
				}
				n[i] = static_cast<uint8_t>(std::toupper(c));
			}
			return n;
		}

		// The kit that plays, renamed live (SysEx 0x55); SAVE stores the name.
		std::optional<MmKit> kitName(MmKit _k, const In& _in)
		{
			std::string error;
			const auto n = kitNameBytes(_in.a.text("name"), error);
			if(!n)
			{
				_in.a.error(error);
				return {};
			}
			_k.name = *n;
			return _k;
		}

		const Edits<MmKit>& kitEdits()
		{
			static const Edits<MmKit> edits{{"level", level}, {"route", route}, {"input", input}, {"param", param},
				{"trigPos", trigPos}, {"legato", legato}, {"portamento", portamento}, {"machine", machine}, {"clearSound", clearSound},
				{"copySound", copySound}, {"pasteSound", pasteSound}, {"params", params}, {"assign", assign}, {"multiEnv", multiEnv},
				{"multiTrig", multiTrig}, {"kitName", kitName}};
			return edits;
		}

		// ---- the active global ----
		std::optional<MmGlobal> routing(MmGlobal _g, const In& _in)
		{
			static const std::array<const char*, 3> modes{"3xSTEREO+AB=MIX", "3xSTEREO", "6xMONO"};
			for(size_t i = 0; i < modes.size(); ++i)
				if(_in.a.text("v") == modes[i])
					_g.routingMode = static_cast<uint8_t>(i);
			return _g;
		}

		std::optional<MmGlobal> midiTrack(MmGlobal _g, const In& _in)
		{
			const auto t = static_cast<size_t>(_in.a.integer("t"));
			if(_in.a.has("ch"))
				_g.midiSeqChannels[t] = static_cast<uint8_t>(_in.a.integer("ch"));
			if(const auto* cc = _in.a.value("cc"); cc && cc->isArray())
			{
				const auto& l = cc->asArray();
				if(l.size() != 4 || !std::all_of(l.begin(), l.end(), [](const Value& _x)
					{ return _x.isNumber() && _x.asNumber() == std::floor(_x.asNumber()) && _x.asNumber() >= 0 && _x.asNumber() <= 128; }))
				{
					_in.a.error("cc: four CC numbers 0-127, or 128 for AFT");
					return {};
				}
				for(size_t i = 0; i < 4; ++i)
					_g.midiSeqCcs[t][i] = static_cast<uint8_t>(l[i].asNumber());
			}
			return _g;
		}


		// GLOBAL › MIDI: the MIDI CHANNELS (a channel 0-15, null OFF: 0x7f, as the machine stores it) and CONTROL IN
		std::optional<MmGlobal> globalMidi(MmGlobal _g, const In& _in)
		{
			const auto channel = [&](const char* _key, uint8_t& _to)
			{
				if(const auto* v = _in.a.value(_key))
					_to = v->isNull() ? uint8_t{0x7f} : static_cast<uint8_t>(v->asNumber());
			};
			channel("base", _g.baseChannel);
			channel("auto", _g.autoChannel);
			channel("multiTrig", _g.multiTrigChannel);
			channel("multiMap", _g.multiMapChannel);
			if(_in.a.has("span"))
				_g.channelSpan = static_cast<uint8_t>(_in.a.integer("span"));
			if(_in.a.has("clockIn"))
				_g.tempoSync = _in.a.flag("clockIn") ? 1 : 0;
			if(_in.a.has("transportIn"))
				_g.transportIn = _in.a.flag("transportIn") ? 1 : 0;
			if(_in.a.has("clockOut"))
				_g.clockOut = _in.a.flag("clockOut") ? 1 : 0;
			if(_in.a.has("transportOut"))
				_g.transportOut = _in.a.flag("transportOut") ? 1 : 0;
			if(_in.a.has("programChangeOut"))
				_g.programChangeOut = _in.a.flag("programChangeOut") ? 1 : 0;
			return _g;
		}

		// B-051, F3: GLOBAL › Reset to defaults: the global the machine ships with, measured (elektronData::mmFactoryGlobal)
		std::optional<MmGlobal> globalReset(MmGlobal _g, const In&)
		{
			return ed::mmFactoryGlobal(_g.position);
		}

		// ---- the MULTI MAP: up to 32 key ranges [upper key, pattern (255 CUR), offset (255 ---), length, transpose, timing];
		// the ranges in use end where an upper key repeats (the ones past it repeat the last upper key) ----
		size_t mapRows(const MmGlobal& _g)
		{
			const auto& hi = _g.multiMap[0];
			size_t n = 1;
			while(n < MmGlobal::g_mapRanges && hi[n] > hi[n - 1])
				++n;
			return n;
		}
		// The ranges from _n on are unused again: they repeat the last upper key and hold nothing.
		void resetMapRows(MmGlobal& _g, const size_t _n)
		{
			for(size_t r = _n; r < MmGlobal::g_mapRanges; ++r)
			{
				_g.multiMap[0][r] = _g.multiMap[0][_n - 1];
				_g.multiMap[1][r] = 255;
				_g.multiMap[2][r] = 255;
				_g.multiMap[3][r] = 16;
				_g.multiMap[4][r] = 0;
				_g.multiMap[5][r] = 0;
			}
		}
		std::optional<size_t> mapRow(const MmGlobal& _g, const In& _in)
		{
			const auto i = static_cast<size_t>(_in.a.integer("i")), n = mapRows(_g);
			if(i >= n)
			{
				_in.a.error("i: the MULTI MAP has " + std::to_string(n) + " range(s)");
				return {};
			}
			return i;
		}

		std::optional<MmGlobal> multiMap(MmGlobal _g, const In& _in)
		{
			const auto i = mapRow(_g, _in);
			if(!i)
				return {};
			const auto n = mapRows(_g);
			auto& m = _g.multiMap;
			if(_in.a.has("hi"))
			{
				const int lo = *i ? m[0][*i - 1] + 1 : 0, top = *i + 1 < n ? m[0][*i + 1] - 1 : 127, hi = _in.a.integer("hi");
				if(hi < lo || hi > top)
				{
					_in.a.error("hi: " + std::to_string(hi) + " is outside " + std::to_string(lo) + ".." + std::to_string(top) + " (the neighbouring ranges)");
					return {};
				}
				m[0][*i] = static_cast<uint8_t>(hi);
				for(size_t r = n; r < MmGlobal::g_mapRanges; ++r)
					m[0][r] = m[0][n - 1];
			}
			if(_in.a.has("pat"))
			{
				const auto v = _in.a.integer("pat");
				if(v > 127 && v != 255)
				{
					_in.a.error("pat: a pattern 0-127, or 255 for the current one");
					return {};
				}
				m[1][*i] = static_cast<uint8_t>(v);
			}
			if(_in.a.has("ofs"))
			{
				const auto v = _in.a.integer("ofs");
				if(v > 63 && v != 255)
				{
					_in.a.error("ofs: a step 0-63, or 255 for none");
					return {};
				}
				m[2][*i] = static_cast<uint8_t>(v);
			}
			if(_in.a.has("len")) m[3][*i] = static_cast<uint8_t>(_in.a.integer("len"));
			if(_in.a.has("trn")) m[4][*i] = static_cast<uint8_t>(_in.a.integer("trn") & 0xff);
			if(_in.a.has("tim")) m[5][*i] = static_cast<uint8_t>(_in.a.integer("tim"));
			return _g;
		}

		// A range splits at its middle key: the lower half is a copy of it.
		std::optional<MmGlobal> multiMapSplit(MmGlobal _g, const In& _in)
		{
			const auto i = mapRow(_g, _in);
			if(!i)
				return {};
			const auto n = mapRows(_g);
			auto& m = _g.multiMap;
			if(n >= MmGlobal::g_mapRanges)
			{
				_in.a.error("The MULTI MAP holds 32 ranges");
				return {};
			}
			const int lo = *i ? m[0][*i - 1] + 1 : 0, hi = m[0][*i];
			if(hi - lo < 1)
			{
				_in.a.error("A one-key range cannot be split");
				return {};
			}
			for(auto& col : m)
			{
				for(size_t r = n; r > *i; --r)
					col[r] = col[r - 1];
			}
			m[0][*i] = static_cast<uint8_t>((lo + hi) / 2);
			resetMapRows(_g, n + 1);
			_in.note = "Split the range up to " + std::to_string(hi);
			return _g;
		}

		// A range goes; the last one then reaches the top key.
		std::optional<MmGlobal> multiMapDelete(MmGlobal _g, const In& _in)
		{
			const auto i = mapRow(_g, _in);
			if(!i)
				return {};
			const auto n = mapRows(_g);
			auto& m = _g.multiMap;
			if(n < 2)
			{
				_in.a.error("The MULTI MAP keeps one range");
				return {};
			}
			for(auto& col : m)
				for(size_t r = *i; r + 1 < n; ++r)
					col[r] = col[r + 1];
			if(*i == n - 1)
				m[0][n - 2] = 127;
			resetMapRows(_g, n - 1);
			return _g;
		}

		const Edits<MmGlobal>& globalEdits()
		{
			static const Edits<MmGlobal> edits{{"routing", routing}, {"midiTrack", midiTrack}, {"globalMidi", globalMidi}, {"globalReset", globalReset}, {"multiMap", multiMap},
				{"multiMapSplit", multiMapSplit}, {"multiMapDelete", multiMapDelete}};
			return edits;
		}

		// ---- a song: its rows edited as the contract's rows (deskCore::songRows, as on the Machinedrum); the codec
		// makes them the song's bytes again. The bytes after END stay where they are (residue, kept as the firmware
		// left it); a row that is no longer used becomes zeros. ----
		std::optional<MmSong> editSong(const MmSong& _s, const deskCore::songRows::Fn _fn, const In& _in)
		{
			const auto doc = ed::mmSongToJson(_s);
			const auto* rowsValue = doc.find("rows");
			const deskCore::songRows::Rows rows = rowsValue ? rowsValue->asArray() : deskCore::songRows::Rows{};
			std::vector<std::string> errors;
			const deskCore::songRows::Edit e{_in.a.command(), errors, _in.note, _in.clip.songRow, MmSong::g_rows};
			const auto edited = _fn(rows, e);
			for(auto& x : errors)
				_in.a.error(std::move(x));
			if(!edited)
				return {};
			if(*edited == rows)
				return _s;
			auto temp = deskCore::songRows::withMember(doc, "rows", Value(Value::Array(edited->begin(), edited->end())));
			if(const auto* fw = temp.find("firmware"))
				temp = deskCore::songRows::withMember(temp, "firmware", deskCore::songRows::withMember(*fw, "rowsAfterEnd", Value::array()));
			std::vector<std::string> problems;
			const auto read = ed::mmSongFromJson(temp, problems);
			if(!read)
			{
				for(auto& p : problems)
					_in.a.error(std::move(p));
				return {};
			}
			auto out = _s;
			const auto was = ed::mmSongUsedRows(_s), now = ed::mmSongUsedRows(*read);
			for(size_t i = 0; i < now; ++i)
				out.rows[i] = read->rows[i];
			for(size_t i = now; i < was; ++i)
				out.rows[i].bytes = {};
			return out;
		}

		// ---- the library: the kit and pattern slots (deskCore::library's actions over these two shelves) ----
		std::string kitLabel(const int _k) { return "K" + std::string(_k < 9 ? "0" : "") + std::to_string(_k + 1); }
		std::string patternLabel(const int _p) { return ed::mmPatternName(static_cast<unsigned>(_p)); }
		std::string kitText(const MmKit& _k)
		{
			std::string n;
			for(const auto c : _k.name)
			{
				if(!c)
					break;
				n += (c >= 0x20 && c < 0x7f) ? static_cast<char>(c) : '?';
			}
			return n;
		}
		std::optional<Document> stored(const Documents& _docs, const Kind _kind, const int _slot)
		{
			return _docs.get({_kind, static_cast<uint8_t>(_slot)});
		}
		Document asStored(const Document& _d)
		{
			if(const auto* w = std::get_if<WorkingKit>(&_d))
				return Document(w->kit);
			return _d;
		}
	}

	ed::MmKit emptyKit(const ed::MmKit& _like, const uint8_t _slot)
	{
		auto k = _like;
		k.position = _slot;
		k.name.fill(0);
		for(size_t t = 0; t < MmKit::g_tracks; ++t)
		{
			k.machines[t] = 1;	// GND-SIN
			k.tracks[t].pages[0] = synDefaults(1);
			for(uint8_t pg = 1; pg < 7; ++pg)
				k.tracks[t].pages[pg] = neutralPage(pg);
			k.levels[t] = 100;
			k.routing[t] = ed::mmRouting(1, 0);	// bus AB, the neighbour
			k.trigPos[t] = MmKit::g_noTrigPos;
		}
		return k;
	}

	ed::MmPattern emptyPattern(const ed::MmPattern& _like)
	{
		auto p = _like;
		std::string error;
		for(int t = 0; t < 12; ++t)
		{
			const auto tr = trackOf(t);
			for(size_t s = 0; s < MmPattern::g_steps; ++s)
				setStep(p, tr, s, StepValue{}, error);
			slideOf(p, tr) = 0;
		}
		while(p.lockRowCount)
			removeRow(p, 0);
		return p;
	}

	bool kitIsEmpty(const ed::MmKit& _k)
	{
		if(_k.name[0] == 0xff)
			return true;
		for(const auto c : _k.name)
		{
			if(!c)
				break;
			if(c != ' ')
				return false;
		}
		return true;
	}

	bool patternHasTrigs(const ed::MmPattern& _p)
	{
		for(size_t t = 0; t < MmPattern::g_tracks; ++t)
			if(_p.pitch[t] || _p.amp[t] || _p.filter[t] || _p.lfo[t] || _p.midiTrig[t])
				return true;
		return false;
	}

	namespace
	{
		using Shelf = deskCore::library::Shelf<MmModel>;
		bool kitPlays(const EditContext& _c, const int _k) { return _c.currentKit == _k; }

		const Shelf g_kits{
			Kind::Kit, "k", "kit", kitLabel,
			// The kit that plays is copied as it sounds (the working kit), another as stored.
			[](const Documents& _docs, const EditContext& _context, const int _k) -> std::optional<Document>
			{
				if(_context.currentKit == _k)
					if(const auto* w = _docs.workingKitOf(_k))
						return Document(WorkingKit{*w});
				return stored(_docs, Kind::Kit, _k);
			},
			[](const Clipboard& _c) { return _c.kit ? std::optional<Document>(*_c.kit) : std::nullopt; },
			[](Clipboard& _c, const Document& _d) { _c.kit = std::get<MmKit>(_d); },
			"Copy a kit first",
			[](const int _k, const Document& _d) { return "Copied " + kitLabel(_k) + " " + kitText(std::get<MmKit>(_d)); },
			[](const Document& _d) { return kitText(std::get<MmKit>(_d)); },
			[](const Document& _src, const Document& _dst, const int _slot)
			{
				auto k = std::get<MmKit>(_src);
				const auto& dst = std::get<MmKit>(_dst);
				k.position = static_cast<uint8_t>(_slot);
				k.version = dst.version;
				k.revision = dst.revision;
				return Document(k);
			},
			[](const Document& _d, const int _slot) { return Document(emptyKit(std::get<MmKit>(_d), static_cast<uint8_t>(_slot))); },
			": every track GND-SIN", asStored, kitPlays,
			[](const Document& _d, const std::string& _name, std::string& _error) -> std::optional<Document>
			{
				const auto n = kitNameBytes(_name, _error);
				if(!n)
					return {};
				auto k = std::get<MmKit>(_d);
				k.name = *n;
				return Document(k);
			},
			problemsOf};

		const Shelf g_patterns{
			Kind::Pattern, "p", "pattern", patternLabel,
			[](const Documents& _docs, const EditContext&, const int _p) { return stored(_docs, Kind::Pattern, _p); },
			[](const Clipboard& _c) { return _c.pattern ? std::optional<Document>(*_c.pattern) : std::nullopt; },
			[](Clipboard& _c, const Document& _d) { _c.pattern = std::get<MmPattern>(_d); },
			"Copy a pattern first",
			[](const int _p, const Document&) { return "Copied " + patternLabel(_p) + ": notes, locks, arpeggiator, transposes and its kit link"; },
			[](const Document& _d) { return patternLabel(std::get<MmPattern>(_d).position); },
			[](const Document& _src, const Document&, const int _slot)
			{
				auto p = std::get<MmPattern>(_src);
				p.position = static_cast<uint8_t>(_slot);
				return Document(p);
			},
			[](const Document& _d, const int) { return Document(emptyPattern(std::get<MmPattern>(_d))); },
			": no notes or locks (length, speed, swing and its kit link stay)", asStored, [](const EditContext&, int) { return false; }, nullptr,
			problemsOf};

		const Shelf* shelfOf(const int _kind)
		{
			for(const auto* sh : {&g_kits, &g_patterns})
				if(static_cast<int>(sh->kind) == _kind)
					return sh;
			return nullptr;
		}
	}

	namespace
	{

		// ---- the document the row's kind names, its op's function, the change ----
		struct Edited
		{
			Document before;
			Document after;
		};

		template<typename T, typename Wrap = T>
		std::optional<Edited> run(const Edits<T>& _edits, const T& _doc, const std::string& _op, const In& _in)
		{
			const auto it = _edits.find(_op);
			if(it == _edits.end())
			{
				_in.a.error("no edit for command " + _op);	// a table row without its function
				return {};
			}
			auto after = it->second(_doc, _in);
			if(!after)
				return {};
			return Edited{Document(Wrap{_doc}), Document(Wrap{*after})};
		}

		std::optional<Edited> edit(const Documents& _docs, const Kind _kind, const std::string& _op, const In& _in, const EditContext& _c)
		{
			switch(_kind)
			{
			case Kind::Pattern:
			{
				const int p = _in.a.integer("p");
				const auto it = _docs.patterns.find(static_cast<uint8_t>(p));
				if(it == _docs.patterns.end())
				{
					_in.a.error("pattern " + std::to_string(p + 1) + " is not read yet");
					return {};
				}
				return run(patternEdits(), it->second, _op, _in);
			}
			case Kind::WorkingKit:
			{
				const int k = _in.a.integer("k");
				if(k != _c.currentKit)
				{
					_in.a.error("kit " + std::to_string(k + 1) + " is not the kit that plays: " + _op + " edits the kit that plays");
					return {};
				}
				const auto* kit = _docs.workingKitOf(k);
				if(!kit)
				{
					_in.a.error("the kit that plays is not read yet");
					return {};
				}
				return run<MmKit, WorkingKit>(kitEdits(), *kit, _op, _in);
			}
			case Kind::Global:
			{
				const auto it = _c.currentGlobal < 0 ? _docs.globals.end() : _docs.globals.find(static_cast<uint8_t>(_c.currentGlobal));
				if(it == _docs.globals.end())
				{
					_in.a.error("the active global is not read yet");
					return {};
				}
				return run(globalEdits(), it->second, _op, _in);
			}
			case Kind::Song:
			{
				const int sl = _in.a.integer("s");
				const auto it = _docs.songs.find(static_cast<uint8_t>(sl));
				if(it == _docs.songs.end())
				{
					_in.a.error("song " + std::to_string(sl + 1) + " is not read yet");
					return {};
				}
				const auto fn = deskCore::songRows::edits().find(_op);
				if(fn == deskCore::songRows::edits().end())
				{
					_in.a.error("no edit for command " + _op);
					return {};
				}
				auto after = editSong(it->second, fn->second, _in);
				if(!after)
					return {};
				return Edited{Document(it->second), Document(*after)};
			}
			default:
				_in.a.error("unknown command " + _op);
				return {};
			}
		}

	}

	std::vector<std::string> editOps()
	{
		std::vector<std::string> ops;
		const auto add = [&ops](const auto& _edits)
		{
			for(const auto& [op, fn] : _edits)
				ops.push_back(op);
		};
		add(patternEdits());
		add(kitEdits());
		add(globalEdits());
		add(deskCore::songRows::edits());
		for(const auto& op : deskCore::library::ops())
			ops.push_back(op);
		return ops;
	}

	EditResult apply(const Documents& _docs, const Value& _command, const Clipboard& _clipboard, const EditContext& _context)
	{
		EditResult result;
		const auto op = deskCore::opOf(_command);
		const auto* row = MmModel::commands().find(op);
		if(!row || row->owner != deskCore::Owner::Core || row->core != deskCore::CoreOp::Edit || row->kind < 0)
		{
			result.errors.push_back("unknown command " + op);
			return result;
		}
		// the table's own check (the router runs it too): the edits then trust its types and ranges
		if(auto errors = CommandTable::check(*row, _command); !errors.empty())
		{
			result.errors = std::move(errors);
			return result;
		}
		auto clipboard = _clipboard;
		if(row->group == g_library)
		{
			result = deskCore::library::apply<MmModel>(_docs, _command, op, shelfOf(row->kind), clipboard, _context);
			if(result.errors.empty() && !(clipboard == _clipboard))
				result.clipboard = std::move(clipboard);
			return result;
		}
		const Args args(_command, result.errors);
		const In in{args, clipboard, result.note, _docs};
		const auto e = edit(_docs, static_cast<Kind>(row->kind), op, in, _context);
		if(!e || !result.errors.empty())
		{
			result.note.clear();
			return result;
		}
		if(auto problems = problemsOf(e->after); !problems.empty())
		{
			result.errors = std::move(problems);
			result.note.clear();
			return result;
		}
		if(!(e->before == e->after))
			result.changes.push_back({e->before, e->after});
		if(!(clipboard == _clipboard))
			result.clipboard = std::move(clipboard);
		return result;
	}
}
