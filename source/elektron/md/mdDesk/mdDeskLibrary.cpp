#include "mdDeskLibrary.h"
#include "mdDeskModel.h"

#include "elektronData/mdJson.h"
#include "elektronData/mdValidate.h"

#include <algorithm>
#include <map>

namespace mdDesk
{
	namespace ed = elektronData;
	using Value = ed::json::Value;

	namespace
	{
		std::string kitLabel(const int _k) { return "K" + std::string(_k < 9 ? "0" : "") + std::to_string(_k + 1); }
		std::string patternLabel(const int _p) { return ed::mdPatternName(static_cast<unsigned>(_p)); }

		std::string kitName(const ed::MdKit& _k)
		{
			std::string n;
			for(const auto c : _k.name)
			{
				if(!c)
					break;
				n += static_cast<char>(c);
			}
			return n;
		}

		// ---- the shelves: what the kit library and the pattern chooser differ in, as data ----

		// One kind of slot the library works on. The table's kind column picks the shelf; the
		// actions below are the same for both.
		struct Shelf
		{
			DocKind kind;
			const char* key;					// the slot argument
			const char* noun;					// "kit is not loaded yet"
			std::string (*label)(int);			// K01, A01
			// What a copy takes from slot _slot (nothing when it is not loaded).
			std::optional<Document> (*source)(const Documents&, const EditContext&, int);
			std::optional<Document> (*clipped)(const Clipboard&);
			void (*keep)(Clipboard&, const Document&);
			const char* nothingCopied;
			std::string (*copied)(int, const Document&);	// the copy's note
			std::string (*title)(const Document&);			// what a paste names
			Document (*placed)(const Document& _src, const Document& _dst, int _slot);
			Document (*cleared)(const Document&, int _slot);
			const char* clearedNote;
		};

		std::optional<Document> stored(const Documents& _docs, const DocKind _kind, const int _slot)
		{
			return _docs.get({_kind, static_cast<uint8_t>(_slot)});
		}

		const Shelf g_kits{
			DocKind::Kit, "k", "kit", kitLabel,
			// The kit that plays is copied as it sounds (the working kit), another as stored.
			[](const Documents& _docs, const EditContext& _context, const int _k) -> std::optional<Document>
			{
				if(_context.currentKit && *_context.currentKit == _k)
					if(const auto* w = _docs.workingKitOf(static_cast<uint8_t>(_k)))
						return Document(*w);
				return stored(_docs, DocKind::Kit, _k);
			},
			[](const Clipboard& _c) { return _c.kit ? std::optional<Document>(*_c.kit) : std::nullopt; },
			[](Clipboard& _c, const Document& _d) { _c.kit = std::get<ed::MdKit>(_d); },
			"Copy a kit first",
			[](const int _k, const Document& _d) { return "Copied " + kitLabel(_k) + " " + kitName(std::get<ed::MdKit>(_d)); },
			[](const Document& _d) { return kitName(std::get<ed::MdKit>(_d)); },
			[](const Document& _src, const Document& _dst, const int _slot)
			{
				auto k = std::get<ed::MdKit>(_src);
				const auto& dst = std::get<ed::MdKit>(_dst);
				k.position = static_cast<uint8_t>(_slot);
				k.version = dst.version;
				k.revision = dst.revision;
				return Document(k);
			},
			[](const Document& _d, const int _slot) { return Document(emptyKit(std::get<ed::MdKit>(_d), static_cast<uint8_t>(_slot))); },
			": every track GND-EMPTY"};

		const Shelf g_patterns{
			DocKind::Pattern, "p", "pattern", patternLabel,
			[](const Documents& _docs, const EditContext&, const int _p) { return stored(_docs, DocKind::Pattern, _p); },
			[](const Clipboard& _c) { return _c.pattern ? std::optional<Document>(*_c.pattern) : std::nullopt; },
			[](Clipboard& _c, const Document& _d) { _c.pattern = std::get<ed::MdPattern>(_d); },
			"Copy a pattern first",
			[](const int _p, const Document&) { return "Copied " + patternLabel(_p) + ": notes, locks and its kit link"; },
			[](const Document& _d) { return patternLabel(std::get<ed::MdPattern>(_d).position); },
			[](const Document& _src, const Document&, const int _slot)
			{
				auto p = std::get<ed::MdPattern>(_src);
				p.position = static_cast<uint8_t>(_slot);
				return Document(p);
			},
			[](const Document& _d, const int) { return Document(emptyPattern(std::get<ed::MdPattern>(_d))); },
			": no trigs or locks"};

		// What an action gets. The table has checked the arguments (apply ran its check).
		struct Lib
		{
			const Documents& docs;
			const Value& command;
			Clipboard& clip;
			const EditContext& context;
			const Shelf& shelf;
			EditResult& r;

			int integer(const char* _key) const { return static_cast<int>(command.find(_key)->asNumber()); }
			int slot() const { return integer(shelf.key); }

			std::optional<Document> at(const int _slot) const
			{
				auto d = stored(docs, shelf.kind, _slot);
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
				if(auto p = problemsOf(_after); !p.empty())
				{
					r.errors = std::move(p);
					return;
				}
				if(!(_before == _after))
					r.changes.push_back({_before, std::move(_after)});
			}
		};

		// A copy of the kit that plays reads the working kit; the clipboard holds it as a kit.
		Document asStored(const Document& _d)
		{
			if(const auto* w = std::get_if<WorkingKit>(&_d))
				return Document(w->kit);
			return _d;
		}

		// ---- the actions, one per op ----

		void copy(const Lib& _l)
		{
			const auto s = _l.slot();
			if(const auto d = _l.from(s))
			{
				_l.shelf.keep(_l.clip, asStored(*d));
				_l.r.note = _l.shelf.copied(s, asStored(*d));
			}
		}

		// Writes _src into slot _target; _verb says how the user did it.
		void place(const Lib& _l, const std::optional<Document>& _src, const int _target, const char* _verb)
		{
			const auto dst = _l.at(_target);
			if(!dst || !_src)
				return;
			const auto src = asStored(*_src);
			_l.r.note = std::string(_verb) + _l.shelf.title(src) + " into " + _l.shelf.label(_target);
			_l.put(*dst, _l.shelf.placed(src, *dst, _target));
		}

		void paste(const Lib& _l)
		{
			const auto src = _l.shelf.clipped(_l.clip);
			if(!src)
				_l.r.errors.emplace_back(_l.shelf.nothingCopied);
			place(_l, src, _l.slot(), "Pasted ");
		}

		void copyTo(const Lib& _l)
		{
			const auto from = _l.integer("from"), to = _l.integer("to");
			if(from == to)
			{
				_l.r.errors.emplace_back("That is the same slot");
				return;
			}
			place(_l, _l.from(from), to, "Copied ");
		}

		void clear(const Lib& _l)
		{
			const auto s = _l.slot();
			if(const auto d = _l.at(s))
			{
				_l.r.note = "Cleared " + _l.shelf.label(s) + _l.shelf.clearedNote;
				_l.put(*d, _l.shelf.cleared(*d, s));
			}
		}

		// A stored slot's rename (its dump with the new name). The kit that plays is renamed live with kitName.
		void rename(const Lib& _l)
		{
			const auto s = _l.slot();
			if(_l.context.currentKit && *_l.context.currentKit == s)
			{
				_l.r.errors.push_back(kitLabel(s) + " plays: kitRename renames a stored kit; kitName renames the kit that plays");
				return;
			}
			const auto d = _l.at(s);
			if(!d)
				return;
			std::string n = _l.command.find("name")->asString();
			if(n.size() > ed::MdKit::g_nameSize)
				n.resize(ed::MdKit::g_nameSize);
			auto renamed = std::get<ed::MdKit>(*d);
			renamed.name.fill(0);
			for(size_t i = 0; i < n.size(); ++i)
			{
				const auto c = static_cast<unsigned char>(n[i]);
				if(c < 0x20 || c > 0x7e)
				{
					_l.r.errors.emplace_back("name: 7-bit printable characters only");
					return;
				}
				renamed.name[i] = static_cast<uint8_t>(std::toupper(c));
			}
			_l.r.note = "Renamed " + kitLabel(s) + " on the machine";
			_l.put(*d, renamed);
		}

		using Action = void (*)(const Lib&);

		const std::map<std::string, Action>& actions()
		{
			static const std::map<std::string, Action> a{
				{"kitCopy", copy}, {"kitPaste", paste}, {"kitCopyTo", copyTo}, {"kitClear", clear}, {"kitRename", rename},
				{"patCopy", copy}, {"patPaste", paste}, {"patCopyTo", copyTo}, {"patClear", clear}};
			return a;
		}

		const Shelf* shelfOf(const int _kind)
		{
			for(const auto* s : {&g_kits, &g_patterns})
				if(static_cast<int>(s->kind) == _kind)
					return s;
			return nullptr;
		}
	}

	ed::MdKit emptyKit(const ed::MdKit& _like, const uint8_t _slot)
	{
		auto k = _like;
		k.position = _slot;
		k.name.fill(0);
		for(size_t t = 0; t < ed::MdKit::g_tracks; ++t)
		{
			k.models[t] = 0;	// GND-EMPTY
			std::copy(neutralTrackValues().begin(), neutralTrackValues().end(), k.params[t].begin());
			k.levels[t] = neutralTrackLevel();
			k.lfos[t].track = static_cast<uint8_t>(t);
			k.lfos[t].param = 0;
			k.lfos[t].shape1 = k.lfos[t].shape2 = 0;
			k.lfos[t].update = 0;
			k.trigGroups[t] = ed::MdKit::g_noGroup;
			k.muteGroups[t] = ed::MdKit::g_noGroup;
		}
		return k;
	}

	ed::MdPattern emptyPattern(const ed::MdPattern& _like)
	{
		auto p = _like;
		p.trigs.fill(0);
		p.lockMasks.fill(0);
		for(auto& row : p.lockRows)
			row.fill(0);
		p.accentPattern = p.slidePattern = p.swingPattern = 0;
		p.trackAccent.fill(0);
		p.trackSlide.fill(0);
		p.trackSwing.fill(0);
		return p;
	}

	bool isEmptyKit(const ed::MdKit& _kit)
	{
		return _kit.name[0] == 0 && std::all_of(_kit.models.begin(), _kit.models.end(), [](const uint32_t _m) { return _m == 0; });
	}

	EditResult applyLibrary(const Documents& _docs, const Value& _command, const deskCore::Command<>& _row, Clipboard& _clipboard,
		const EditContext& _context)
	{
		EditResult r;
		const auto it = actions().find(_row.op);
		const auto* shelf = shelfOf(_row.kind);
		if(it == actions().end() || !shelf)
		{
			r.errors.push_back(std::string("unknown library command ") + _row.op);
			return r;
		}
		it->second(Lib{_docs, _command, _clipboard, _context, *shelf, r});
		return r;
	}
}
