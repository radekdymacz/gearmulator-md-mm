#include "mdDeskLibrary.h"
#include "mdDeskModel.h"

#include "deskCore/deskEdits.h"

#include "elektronData/mdJson.h"
#include "elektronData/mdValidate.h"

#include <algorithm>
#include <cctype>
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

		// ---- the shelves: what the kit library and the pattern chooser differ in, as data (the actions are
		// deskCore's library, shared with the Monomachine: deskCore/deskEdits.h) ----

		using Shelf = deskCore::library::Shelf<MdModel>;

		std::optional<Document> stored(const Documents& _docs, const DocKind _kind, const int _slot)
		{
			return _docs.get({_kind, static_cast<uint8_t>(_slot)});
		}

		// A copy of the kit that plays reads the working kit; the clipboard holds it as a kit.
		Document asStored(const Document& _d)
		{
			if(const auto* w = std::get_if<WorkingKit>(&_d))
				return Document(w->kit);
			return _d;
		}

		bool kitPlays(const EditContext& _context, const int _k) { return _context.currentKit && *_context.currentKit == _k; }

		// A stored slot's dump with the new name: 7-bit printable, upper case, at most the name's bytes.
		std::optional<Document> kitRenamed(const Document& _d, const std::string& _name, std::string& _error)
		{
			std::string n = _name;
			if(n.size() > ed::MdKit::g_nameSize)
				n.resize(ed::MdKit::g_nameSize);
			auto renamed = std::get<ed::MdKit>(_d);
			renamed.name.fill(0);
			for(size_t i = 0; i < n.size(); ++i)
			{
				const auto c = static_cast<unsigned char>(n[i]);
				if(c < 0x20 || c > 0x7e)
				{
					_error = "name: 7-bit printable characters only";
					return {};
				}
				renamed.name[i] = static_cast<uint8_t>(std::toupper(c));
			}
			return Document(renamed);
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
			": every track GND-EMPTY", asStored, kitPlays, kitRenamed, problemsOf};

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
			": no trigs or locks", asStored, [](const EditContext&, int) { return false; }, nullptr, problemsOf};

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

	std::string kitNameText(const ed::MdKit& _kit)
	{
		std::string n;
		for(const auto c : _kit.name)
		{
			if(!c)
				break;
			if(c < 0x20 || c > 0x7e)
				return {};
			n += static_cast<char>(c);
		}
		return n;
	}

	bool isEmptyKit(const ed::MdKit& _kit)
	{
		return kitNameText(_kit).empty() && std::all_of(_kit.models.begin(), _kit.models.end(), [](const uint32_t _m) { return _m == 0; });
	}

	std::vector<std::string> libraryOps() { return deskCore::library::ops(); }

	EditResult applyLibrary(const Documents& _docs, const Value& _command, const deskCore::Command<>& _row, Clipboard& _clipboard,
		const EditContext& _context)
	{
		return deskCore::library::apply<MdModel>(_docs, _command, _row.op, shelfOf(_row.kind), _clipboard, _context);
	}
}
