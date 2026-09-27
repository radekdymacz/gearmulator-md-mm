#include "mdDeskLibrary.h"

#include "elektronData/mdJson.h"
#include "elektronData/mdValidate.h"

#include <algorithm>
#include <cmath>

namespace mdDesk
{
	namespace ed = elektronData;
	using Value = ed::json::Value;

	namespace
	{
		std::optional<int> intArg(const Value& _m, const char* _key, const int _min, const int _max, std::vector<std::string>& _errors)
		{
			const auto* v = _m.find(_key);
			if(!v || !v->isNumber() || v->asNumber() != std::floor(v->asNumber()) || v->asNumber() < _min || v->asNumber() > _max)
			{
				_errors.push_back(std::string(_key) + ": expected " + std::to_string(_min) + ".." + std::to_string(_max));
				return {};
			}
			return static_cast<int>(v->asNumber());
		}

		std::string kitLabel(const int _k) { return "K" + std::string(_k < 9 ? "0" : "") + std::to_string(_k + 1); }

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

		std::vector<std::string> problems(const Document& _doc)
		{
			return std::visit([](const auto& _v) { return ed::validate(_v); }, _doc);
		}
	}

	bool isLibraryCommand(const std::string& _op)
	{
		static const char* ops[] = {"kitCopy", "kitPaste", "kitCopyTo", "kitClear", "kitRename", "patCopy", "patPaste", "patCopyTo", "patClear"};
		return std::any_of(std::begin(ops), std::end(ops), [&](const char* _o) { return _op == _o; });
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

	EditResult applyLibrary(const Documents& _docs, const Value& _command, Clipboard& _clipboard)
	{
		EditResult r;
		const auto* opv = _command.find("op");
		const std::string op = opv && opv->isString() ? opv->asString() : std::string();
		const bool kit = op.rfind("kit", 0) == 0;
		const auto kitAt = [&](const int _k) -> const ed::MdKit*
		{
			const auto it = _docs.kits.find(static_cast<uint8_t>(_k));
			if(it == _docs.kits.end())
				r.errors.push_back("kit " + kitLabel(_k) + " is not loaded yet");
			return it == _docs.kits.end() ? nullptr : &it->second;
		};
		const auto patAt = [&](const int _p) -> const ed::MdPattern*
		{
			const auto it = _docs.patterns.find(static_cast<uint8_t>(_p));
			if(it == _docs.patterns.end())
				r.errors.push_back("pattern " + ed::mdPatternName(static_cast<unsigned>(_p)) + " is not loaded yet");
			return it == _docs.patterns.end() ? nullptr : &it->second;
		};
		const auto put = [&](const Document& _before, Document _after)
		{
			if(auto p = problems(_after); !p.empty())
			{
				r.errors = std::move(p);
				return;
			}
			const bool same = std::visit([&](const auto& _a) { return _a == std::get<std::decay_t<decltype(_a)>>(_before); }, _after);
			if(!same)
				r.changes.push_back({_before, std::move(_after), kit});
		};
		const int max = kit ? 63 : 127;
		const char* key = kit ? "k" : "p";

		if(op == "kitCopy" || op == "patCopy")
		{
			const auto s = intArg(_command, key, 0, max, r.errors);
			if(!s)
				return r;
			if(kit)
			{
				if(const auto* k = kitAt(*s))
				{
					_clipboard.kit = *k;
					r.note = "Copied " + kitLabel(*s) + " " + kitName(*k);
				}
			}
			else if(const auto* p = patAt(*s))
			{
				_clipboard.pattern = *p;
				r.note = "Copied " + ed::mdPatternName(static_cast<unsigned>(*s)) + ": notes, locks and its kit link";
			}
			return r;
		}
		if(op == "kitPaste" || op == "kitCopyTo" || op == "patPaste" || op == "patCopyTo")
		{
			const bool to = op == "kitCopyTo" || op == "patCopyTo";
			const auto target = intArg(_command, to ? "to" : key, 0, max, r.errors);
			std::optional<int> from;
			if(to)
				from = intArg(_command, "from", 0, max, r.errors);
			if(!target || (to && !from))
				return r;
			if(from && *from == *target)
			{
				r.errors.emplace_back("That is the same slot");
				return r;
			}
			if(kit)
			{
				const auto* dst = kitAt(*target);
				const auto* src = from ? kitAt(*from) : (_clipboard.kit ? &*_clipboard.kit : nullptr);
				if(!from && !_clipboard.kit)
					r.errors.emplace_back("Copy a kit first");
				if(!dst || !src)
					return r;
				auto k = *src;
				k.position = static_cast<uint8_t>(*target);
				k.version = dst->version;
				k.revision = dst->revision;
				r.note = std::string(to ? "Copied " : "Pasted ") + kitName(*src) + " into " + kitLabel(*target);
				put(*dst, k);
			}
			else
			{
				const auto* dst = patAt(*target);
				const auto* src = from ? patAt(*from) : (_clipboard.pattern ? &*_clipboard.pattern : nullptr);
				if(!from && !_clipboard.pattern)
					r.errors.emplace_back("Copy a pattern first");
				if(!dst || !src)
					return r;
				auto p = *src;
				p.position = static_cast<uint8_t>(*target);
				r.note = std::string(to ? "Copied " : "Pasted ") + ed::mdPatternName(src->position) + " into "
					+ ed::mdPatternName(static_cast<unsigned>(*target));
				put(*dst, p);
			}
			return r;
		}
		if(op == "kitClear" || op == "patClear")
		{
			const auto s = intArg(_command, key, 0, max, r.errors);
			if(!s)
				return r;
			if(kit)
			{
				if(const auto* k = kitAt(*s))
				{
					r.note = "Cleared " + kitLabel(*s) + ": every track GND-EMPTY";
					put(*k, emptyKit(*k, static_cast<uint8_t>(*s)));
				}
			}
			else if(const auto* p = patAt(*s))
			{
				r.note = "Cleared " + ed::mdPatternName(static_cast<unsigned>(*s)) + ": no trigs or locks";
				put(*p, emptyPattern(*p));
			}
			return r;
		}
		if(op == "kitRename")
		{
			const auto s = intArg(_command, "k", 0, 63, r.errors);
			const auto* name = _command.find("name");
			if(!name || !name->isString())
				r.errors.emplace_back("name: expected text");
			if(!s || !name || !name->isString())
				return r;
			const auto* k = kitAt(*s);
			if(!k)
				return r;
			std::string n = name->asString();
			if(n.size() > ed::MdKit::g_nameSize)
				n.resize(ed::MdKit::g_nameSize);
			auto renamed = *k;
			renamed.name.fill(0);
			for(size_t i = 0; i < n.size(); ++i)
			{
				const auto c = static_cast<unsigned char>(n[i]);
				if(c < 0x20 || c > 0x7e)
				{
					r.errors.emplace_back("name: 7-bit printable characters only");
					return r;
				}
				renamed.name[i] = static_cast<uint8_t>(std::toupper(c));
			}
			r.note = "Renamed " + kitLabel(*s) + " on the machine";
			put(*k, renamed);
			return r;
		}
		r.errors.push_back("unknown library command " + op);
		return r;
	}
}
