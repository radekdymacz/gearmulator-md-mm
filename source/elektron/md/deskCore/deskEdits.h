#pragma once

#include "elektronData/json.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace deskCore
{
	// The generic step-set moves both editors' pure edits use (DESIGN-UNIFY.md 4.2): a track's steps as bits
	// (bit s = step s) and as rows (a value per step: notes, lock values). Pure and header-only; what a step
	// holds is the machine's (mdDeskEdit.cpp, mmDeskEdit.cpp), how steps move is here, once.

	// What the generic edits below share (DESIGN-UNIFY.md 4.2): the step-set moves (bits and rows of steps), the song
	// rows (contract rows as JSON values: one row semantics for both machines) and the library's slot actions over
	// any model's kinds (copy, paste, copy-to, clear, rename). What a step, a row or a slot holds is the machine's.

	// Step _s moved _by steps later (earlier when negative), wrapping inside [0, _len).
	inline size_t rotatedStep(const size_t _s, const int _by, const size_t _len)
	{
		const int len = static_cast<int>(_len);
		return static_cast<size_t>(((static_cast<int>(_s) + _by) % len + len) % len);
	}

	// The bits of [0, _len) moved _by steps, wrapping inside [0, _len); the bits from _len on stay.
	inline uint64_t rotatedBits(const uint64_t _bits, const int _by, const size_t _len)
	{
		const uint64_t inside = _len >= 64 ? ~uint64_t{0} : (uint64_t{1} << _len) - 1;
		uint64_t out = _bits & ~inside;
		for(size_t s = 0; s < _len; ++s)
			if(_bits >> s & 1)
				out |= uint64_t{1} << rotatedStep(s, _by, _len);
		return out;
	}

	// A row's values of [0, _len) moved _by steps, wrapping inside [0, _len); the values from _len on stay.
	template<typename Row>
	void rotateRow(Row& _row, const int _by, const size_t _len)
	{
		const Row was = _row;
		for(size_t s = 0; s < _len; ++s)
			_row[rotatedStep(s, _by, _len)] = was[s];
	}

	// The steps [_len, 2 _len) a copy of [0, _len), after [_clearFrom, _clearTo) was cleared (steps a longer
	// pattern would show that held hidden residue); the steps from 2 _len on otherwise stay.
	inline uint64_t doubledBits(uint64_t _bits, const size_t _len, const size_t _clearFrom = 0, const size_t _clearTo = 0)
	{
		const auto bit = [](const size_t _s) { return uint64_t{1} << _s; };
		for(size_t s = _clearFrom; s < _clearTo && s < 64; ++s)
			_bits &= ~bit(s);
		for(size_t s = 0; s < _len && _len + s < 64; ++s)
			_bits = (_bits & ~bit(_len + s)) | ((_bits >> s & 1) << (_len + s));
		return _bits;
	}

	// The row's values of [_len, 2 _len) a copy of [0, _len), after [_clearFrom, _clearTo) was set to _empty.
	template<typename Row, typename V>
	void doubleRow(Row& _row, const size_t _len, const V _empty, const size_t _clearFrom = 0, const size_t _clearTo = 0)
	{
		for(size_t s = _clearFrom; s < _clearTo && s < _row.size(); ++s)
			_row[s] = _empty;
		for(size_t s = 0; s < _len && _len + s < _row.size(); ++s)
			_row[_len + s] = _row[s];
	}

	// The bits of steps [_from, _to).
	inline uint64_t stepRange(const size_t _from, const size_t _to)
	{
		uint64_t r = 0;
		for(size_t s = _from; s < _to && s < 64; ++s)
			r |= uint64_t{1} << s;
		return r;
	}

	// ---- song rows: the contract's rows ({kind, target?...} JSON values), edited as rows (DESIGN-UNIFY.md 4.2) ----
	// kind "end" is the last row; loop, jump and halt rows that carry a target follow the row they name when rows
	// move. The machine's codec turns the rows back into its song (and refuses what it cannot hold).
	namespace songRows
	{
		using Value = elektronData::json::Value;
		using Rows = std::vector<Value>;

		// What one row edit gets: the command, where its errors and note go, the clipboard's row, and how many rows a
		// song holds (END included).
		struct Edit
		{
			const Value& command;
			std::vector<std::string>& errors;
			std::string& note;
			std::optional<Value>& clip;
			size_t maxRows;
		};

		inline Value withMember(const Value& _object, const std::string& _key, Value _value)
		{
			Value out = Value::object();
			bool replaced = false;
			for(const auto& [k, v] : _object.asObject())
			{
				if(k == _key)
				{
					out.set(k, _value);
					replaced = true;
				}
				else
					out.set(k, v);
			}
			if(!replaced)
				out.set(_key, std::move(_value));
			return out;
		}

		inline bool isEnd(const Value& _row)
		{
			const auto* kind = _row.find("kind");
			return kind && kind->isString() && kind->asString() == "end";
		}

		// Loop, jump and halt targets follow their rows: _map[old] = new index.
		inline Rows remapTargets(Rows _rows, const std::vector<size_t>& _map)
		{
			for(auto& row : _rows)
			{
				const auto* kind = row.find("kind");
				const auto* target = row.find("target");
				if(!kind || !target || !target->isNumber() || !kind->isString())
					continue;
				if(kind->asString() != "loop" && kind->asString() != "jump" && kind->asString() != "halt")
					continue;
				const auto old = static_cast<size_t>(target->asNumber());
				if(old < _map.size())
					row = withMember(row, "target", static_cast<int>(_map[old]));
			}
			return _rows;
		}

		// The integer argument _key, inside [_min, _max] (the errors as the command table words them).
		inline std::optional<int> within(const Edit& _e, const char* _key, const int _min, const int _max)
		{
			const auto* v = _e.command.find(_key);
			if(!v || !v->isNumber())
			{
				_e.errors.push_back(std::string(_key) + ": missing number");
				return {};
			}
			const auto i = static_cast<int>(v->asNumber());
			if(i < _min || i > _max)
			{
				_e.errors.push_back(std::string(_key) + ": " + std::to_string(i) + " is outside " + std::to_string(_min) + ".." + std::to_string(_max));
				return {};
			}
			return i;
		}
		inline std::optional<int> rowArg(const Rows& _rows, const Edit& _e, const char* _key)
		{
			return within(_e, _key, 0, static_cast<int>(_rows.size()) - 1);
		}

		inline std::optional<Rows> copyRow(Rows _rows, const Edit& _e)
		{
			const auto i = rowArg(_rows, _e, "i");
			if(!i)
				return {};
			if(isEnd(_rows[size_t(*i)]))
			{
				_e.errors.push_back("END cannot be copied");
				return {};
			}
			_e.clip = _rows[size_t(*i)];
			_e.note = "Copied row " + std::to_string(*i + 1);
			return _rows;
		}

		inline std::optional<Rows> rowSet(Rows _rows, const Edit& _e)
		{
			const auto i = rowArg(_rows, _e, "i");
			if(!i)
				return {};
			_rows[size_t(*i)] = *_e.command.find("row");
			return _rows;
		}

		// Insert at i; the END row can only move down.
		inline std::optional<Rows> insertRow(Rows _rows, const Edit& _e, const Value& _row)
		{
			if(_rows.size() >= _e.maxRows)
			{
				_e.errors.push_back("A song holds " + std::to_string(_e.maxRows) + " rows");
				return {};
			}
			const auto at = size_t(_e.command.find("i")->asNumber());
			std::vector<size_t> map(_rows.size());
			for(size_t j = 0; j < map.size(); ++j)
				map[j] = j < at ? j : j + 1;
			_rows = remapTargets(std::move(_rows), map);
			_rows.insert(_rows.begin() + static_cast<std::ptrdiff_t>(at), _row);
			return _rows;
		}

		inline std::optional<Rows> rowInsert(Rows _rows, const Edit& _e)
		{
			if(!rowArg(_rows, _e, "i"))
				return {};
			const auto* row = _e.command.find("row");
			if(!row)
			{
				_e.errors.push_back("row: missing object");
				return {};
			}
			return insertRow(std::move(_rows), _e, *row);
		}

		inline std::optional<Rows> pasteRow(Rows _rows, const Edit& _e)
		{
			if(!rowArg(_rows, _e, "i"))
				return {};
			if(!_e.clip)
			{
				_e.errors.push_back("Copy a song row first");
				return {};
			}
			return insertRow(std::move(_rows), _e, *_e.clip);
		}

		inline std::optional<Rows> rowDelete(Rows _rows, const Edit& _e)
		{
			const auto i = rowArg(_rows, _e, "i");
			if(!i)
				return {};
			if(isEnd(_rows[size_t(*i)]))
			{
				_e.errors.push_back("END cannot be deleted");
				return {};
			}
			const auto at = size_t(*i);
			std::vector<size_t> map(_rows.size());
			for(size_t j = 0; j < map.size(); ++j)
				map[j] = j <= at ? j : j - 1;
			_rows.erase(_rows.begin() + static_cast<std::ptrdiff_t>(at));
			return remapTargets(std::move(_rows), map);
		}

		inline std::optional<Rows> rowMove(Rows _rows, const Edit& _e)
		{
			const auto from = rowArg(_rows, _e, "from"), to = rowArg(_rows, _e, "to");
			if(!from || !to)
				return {};
			if(isEnd(_rows[size_t(*from)]) || isEnd(_rows[size_t(*to)]))
			{
				_e.errors.push_back("END stays the last row");
				return {};
			}
			std::vector<size_t> order(_rows.size());
			for(size_t j = 0; j < order.size(); ++j)
				order[j] = j;
			const auto moved = order[size_t(*from)];
			order.erase(order.begin() + *from);
			order.insert(order.begin() + *to, moved);
			std::vector<size_t> map(_rows.size());
			for(size_t j = 0; j < order.size(); ++j)
				map[order[j]] = j;
			_rows = remapTargets(std::move(_rows), map);
			Rows reordered;
			for(const auto j : order)
				reordered.push_back(_rows[j]);
			return reordered;
		}

		using Fn = std::optional<Rows> (*)(Rows, const Edit&);
		// The row ops, one function each (the same op names on both machines).
		inline const std::map<std::string, Fn>& edits()
		{
			static const std::map<std::string, Fn> e{
				{"copyRow", copyRow}, {"rowSet", rowSet}, {"rowInsert", rowInsert}, {"pasteRow", pasteRow},
				{"rowDelete", rowDelete}, {"rowMove", rowMove}};
			return e;
		}
	}

	// ---- the library: slot actions over a model's kinds (DESIGN-UNIFY.md 4.2) ----
	// One shelf a kind of slot (the kits, the patterns): what differs between them and between the machines is data
	// here (the shelf's functions), the actions are the same. Commands: <kit|pat>Copy {k|p}, Paste {k|p}, CopyTo {from,
	// to}, Clear {k|p}, kitRename {k, name}. The command table has checked the arguments. A slot write is validated and
	// is no change when it is the same; asking before it loses something is the machine's (its review).
	namespace library
	{
		template<typename M>
		struct Shelf
		{
			using Document = typename M::Document;
			using Documents = typename M::Documents;
			using Clipboard = typename M::Clipboard;
			using Context = typename M::Context;

			typename M::Kind kind;
			const char* key;					// the slot argument
			const char* noun;					// "kit is not loaded yet"
			std::string (*label)(int);			// K01, A01
			// What a copy takes from slot _slot (nothing when it is not loaded): the kit that plays as it sounds.
			std::optional<Document> (*source)(const Documents&, const Context&, int);
			std::optional<Document> (*clipped)(const Clipboard&);
			void (*keep)(Clipboard&, const Document&);
			const char* nothingCopied;
			std::string (*copied)(int, const Document&);	// the copy's note
			std::string (*title)(const Document&);			// what a paste names
			Document (*placed)(const Document& _src, const Document& _dst, int _slot);
			Document (*cleared)(const Document&, int _slot);
			const char* clearedNote;
			// The stored form of what a copy read (the kit that plays as a stored kit).
			Document (*stored)(const Document&);
			// Rename (kits): whether slot _slot is the kit that plays (refused: the live rename is kitName), and the
			// slot's document with the new name (an error when the name cannot be one).
			bool (*plays)(const Context&, int);
			std::optional<Document> (*renamed)(const Document&, const std::string& _name, std::string& _error);
			std::vector<std::string> (*problems)(const Document&);
		};

		template<typename M>
		struct Lib
		{
			using Document = typename M::Document;

			const typename M::Documents& docs;
			const elektronData::json::Value& command;
			typename M::Clipboard& clip;
			const typename M::Context& context;
			const Shelf<M>& shelf;
			typename M::EditResult& r;

			int integer(const char* _key) const { return static_cast<int>(command.find(_key)->asNumber()); }
			int slot() const { return integer(shelf.key); }

			std::optional<Document> at(const int _slot) const
			{
				auto d = M::get(docs, {shelf.kind, static_cast<uint8_t>(_slot)});
				if(!d)
					r.errors.push_back(std::string(shelf.noun) + " " + shelf.label(_slot) + " is not loaded yet");
				return d;
			}
			std::optional<Document> from(const int _slot) const
			{
				auto d = shelf.source(docs, context, _slot);
				if(!d)
					r.errors.push_back(std::string(shelf.noun) + " " + shelf.label(_slot) + " is not loaded yet");
				return d;
			}
			// A slot write: validated, and no change when it is the same.
			void put(const Document& _before, Document _after) const
			{
				if(auto p = shelf.problems(_after); !p.empty())
				{
					r.errors = std::move(p);
					return;
				}
				if(!(_before == _after))
					r.changes.push_back({_before, std::move(_after)});
			}
		};

		template<typename M>
		void copy(const Lib<M>& _l)
		{
			const auto s = _l.slot();
			if(const auto d = _l.from(s))
			{
				_l.shelf.keep(_l.clip, _l.shelf.stored(*d));
				_l.r.note = _l.shelf.copied(s, _l.shelf.stored(*d));
			}
		}

		// Writes _src into slot _target; _verb says how the user did it.
		template<typename M>
		void place(const Lib<M>& _l, const std::optional<typename M::Document>& _src, const int _target, const char* _verb)
		{
			const auto dst = _l.at(_target);
			if(!dst || !_src)
				return;
			const auto src = _l.shelf.stored(*_src);
			_l.r.note = std::string(_verb) + _l.shelf.title(src) + " into " + _l.shelf.label(_target);
			_l.put(*dst, _l.shelf.placed(src, *dst, _target));
		}

		template<typename M>
		void paste(const Lib<M>& _l)
		{
			const auto src = _l.shelf.clipped(_l.clip);
			if(!src)
				_l.r.errors.emplace_back(_l.shelf.nothingCopied);
			place(_l, src, _l.slot(), "Pasted ");
		}

		template<typename M>
		void copyTo(const Lib<M>& _l)
		{
			const auto from = _l.integer("from"), to = _l.integer("to");
			if(from == to)
			{
				_l.r.errors.emplace_back("That is the same slot");
				return;
			}
			place(_l, _l.from(from), to, "Copied ");
		}

		template<typename M>
		void clear(const Lib<M>& _l)
		{
			const auto s = _l.slot();
			if(const auto d = _l.at(s))
			{
				_l.r.note = "Cleared " + _l.shelf.label(s) + _l.shelf.clearedNote;
				_l.put(*d, _l.shelf.cleared(*d, s));
			}
		}

		// A stored slot's rename (its dump with the new name). The kit that plays is renamed live with kitName.
		template<typename M>
		void rename(const Lib<M>& _l)
		{
			const auto s = _l.slot();
			if(_l.shelf.plays(_l.context, s))
			{
				_l.r.errors.push_back(_l.shelf.label(s) + " plays: kitRename renames a stored kit; kitName renames the kit that plays");
				return;
			}
			const auto d = _l.at(s);
			if(!d || !_l.shelf.renamed)
				return;
			std::string error;
			const auto named = _l.shelf.renamed(*d, _l.command.find("name")->asString(), error);
			if(!named)
			{
				_l.r.errors.push_back(error);
				return;
			}
			_l.r.note = "Renamed " + _l.shelf.label(s) + " on the machine";
			_l.put(*d, *named);
		}

		// The library ops (the same names on both machines) and the action each runs.
		enum class Action : uint8_t { Copy, Paste, CopyTo, Clear, Rename };
		inline const std::map<std::string, Action>& actions()
		{
			static const std::map<std::string, Action> a{
				{"kitCopy", Action::Copy}, {"kitPaste", Action::Paste}, {"kitCopyTo", Action::CopyTo}, {"kitClear", Action::Clear},
				{"kitRename", Action::Rename}, {"patCopy", Action::Copy}, {"patPaste", Action::Paste}, {"patCopyTo", Action::CopyTo},
				{"patClear", Action::Clear}};
			return a;
		}
		inline std::vector<std::string> ops()
		{
			std::vector<std::string> o;
			for(const auto& [op, a] : actions())
				o.push_back(op);
			return o;
		}

		// One library command on the shelf its row's kind names (nullptr: not a library kind).
		template<typename M>
		typename M::EditResult apply(const typename M::Documents& _docs, const elektronData::json::Value& _command, const std::string& _op,
			const Shelf<M>* _shelf, typename M::Clipboard& _clipboard, const typename M::Context& _context)
		{
			typename M::EditResult r;
			const auto it = actions().find(_op);
			if(it == actions().end() || !_shelf)
			{
				r.errors.push_back(std::string("unknown library command ") + _op);
				return r;
			}
			const Lib<M> l{_docs, _command, _clipboard, _context, *_shelf, r};
			switch(it->second)
			{
			case Action::Copy: copy(l); break;
			case Action::Paste: paste(l); break;
			case Action::CopyTo: copyTo(l); break;
			case Action::Clear: clear(l); break;
			case Action::Rename: rename(l); break;
			}
			return r;
		}
	}
}
