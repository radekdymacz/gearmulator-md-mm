#pragma once

#include "elektronData/json.h"
#include "elektronData/syxImport.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mdJucePlugin
{
	// P7: SysEx import and export for a session, as data. A .syx file the user chose or dropped is parsed
	// (elektronData::parseSyx), shown to the page as a preview, and imported as ordinary document writes:
	// one "set" command per item, all with one gesture id (one undo step), a few per session step, so
	// the page can show progress and cancel between items. Traits (one per model, in mdSessionMd.cpp /
	// mdSessionMm.cpp): using Docs; model; name; docs(file); machine(desk); json(kind, doc set, slot).
	template<typename Traits>
	class SyxJob
	{
	public:
		using Value = elektronData::json::Value;
		using Docs = typename Traits::Docs;

		// The file's preview for the page ({"type":"syxPreview", ...}); remembers the file for an import.
		Value open(const std::vector<uint8_t>& _bytes, const std::string& _fileName, const Docs& _machine)
		{
			namespace ed = elektronData;
			m_file = ed::parseSyx(_bytes);
			m_queue.clear();
			Value m = Value::object();
			m.set("type", "syxPreview");
			m.set("file", _fileName);
			const auto model = ed::documentsModel(m_file);
			if(model != Traits::model)
			{
				m_file = {};
				m.set("ok", false);
				m.set("text", model == ed::SyxModel::Unknown ? std::string("No ") + Traits::name + " dumps in this file."
					: std::string("This is a ") + ed::syxModelName(model) + " file; this is the " + Traits::name + " Editor.");
				return m;
			}
			const auto s = ed::summarizeSyx(m_file);
			// what this firmware cannot take as it is (another OS version's format, values it refuses): left out
			Value leftOut = Value::array();
			const auto fits = [&](const ed::SyxKind _k, const uint8_t _slot)
			{
				const auto why = Traits::fits(_k, Traits::docs(m_file), _slot);
				if(!why.empty() && leftOut.asArray().size() < 40)
					leftOut.push(std::string(ed::syxKindName(_k)) + " " + (_k == ed::SyxKind::Pattern ? ed::syxPatternLabel(_slot) : std::to_string(_slot + 1)) + ": " + why);
				return why.empty();
			};
			const auto plan = ed::planSyxImport(m_file, ed::syxItems(m_file), _machine);
			const auto overwrites = [&](const ed::SyxKind _k, const uint8_t _slot)
			{
				for(const auto& p : plan)
					if(p.kind == _k && p.slot == _slot)
						return p.overwrites;
				return false;
			};
			m.set("ok", true);
			m.set("model", Traits::name);
			m.set("fullBackup", s.fullBackup);
			Value items = Value::object();
			Value kits = Value::array(), pats = Value::array(), songs = Value::array(), globs = Value::array();
			size_t left = 0;
			for(const auto& k : s.kitNames)
			{
				if(!fits(ed::SyxKind::Kit, k.slot)) { ++left; continue; }
				Value v = Value::object(); v.set("slot", k.slot); v.set("name", k.name); v.set("overwrites", overwrites(ed::SyxKind::Kit, k.slot)); kits.push(v);
			}
			for(const auto& p : s.patternList)
			{
				if(!fits(ed::SyxKind::Pattern, p.slot)) { ++left; continue; }
				Value v = Value::object(); v.set("slot", p.slot); v.set("name", p.label); v.set("kit", p.kit); v.set("overwrites", overwrites(ed::SyxKind::Pattern, p.slot)); pats.push(v);
			}
			for(const auto& n : s.songNames)
			{
				if(!fits(ed::SyxKind::Song, n.slot)) { ++left; continue; }
				Value v = Value::object(); v.set("slot", n.slot); v.set("name", n.name); v.set("overwrites", overwrites(ed::SyxKind::Song, n.slot)); songs.push(v);
			}
			for(const auto& [slot, g] : Traits::docs(m_file).globals)
			{
				if(!fits(ed::SyxKind::Global, slot)) { ++left; continue; }
				Value v = Value::object(); v.set("slot", slot); v.set("name", "Global " + std::to_string(slot + 1)); v.set("overwrites", overwrites(ed::SyxKind::Global, slot)); globs.push(v);
			}
			items.set("kit", kits); items.set("pattern", pats); items.set("song", songs); items.set("global", globs);
			m.set("items", items);
			Value problems = Value::array();
			for(const auto& p : m_file.problems)
				if(problems.asArray().size() < 20)
					problems.push(std::string("message ") + std::to_string(p.index + 1) + " (" + ed::syxKindName(p.kind)
						+ (p.slot >= 0 ? " " + std::to_string(p.slot + 1) : std::string()) + "): " + ed::syxStatusName(p.status));
			m.set("problems", problems);
			m.set("problemCount", static_cast<int>(m_file.problems.size()));
			m.set("leftOut", leftOut);
			m.set("leftOutCount", static_cast<int>(left));
			m.set("text", "");
			return m;
		}

		// Queue the chosen kinds (all when empty) for sending, with one gesture id.
		template<typename Keep>
		std::string start(const std::vector<std::string>& _kinds, const uint64_t _gesture, const Keep& _keep)
		{
			namespace ed = elektronData;
			m_queue.clear();
			if(ed::documentsModel(m_file) != Traits::model)
				return "Open a .syx file first.";
			std::vector<ed::SyxKind> kinds;
			for(const auto& k : _kinds)
				for(const auto kk : {ed::SyxKind::Global, ed::SyxKind::Kit, ed::SyxKind::Pattern, ed::SyxKind::Song})
					if(k == ed::syxKindName(kk))
						kinds.push_back(kk);
			auto all = kinds.empty() ? ed::syxItems(m_file) : ed::syxItems(m_file, kinds);
			for(const auto& i : all)
				if(_keep(i) && Traits::fits(i.kind, Traits::docs(m_file), i.slot).empty())
					m_queue.push_back(i);
			// globals first (settings), then kits before the patterns that play them, then songs
			m_total = m_queue.size();
			m_done = 0;
			m_gesture = _gesture;
			return m_queue.empty() ? "Nothing to import." : "";
		}

		void cancel() { m_queue.clear(); }
		bool running() const { return m_done < m_total && !m_queue.empty(); }

		// The next few items as "set" commands (at most _n), and the progress message.
		std::vector<Value> next(const size_t _n)
		{
			std::vector<Value> out;
			while(!m_queue.empty() && out.size() < _n)
			{
				const auto item = m_queue.front();
				m_queue.erase(m_queue.begin());
				Value c = Value::object();
				c.set("op", "set");
				c.set("kind", elektronData::syxKindName(item.kind));
				c.set("doc", Traits::json(item.kind, Traits::docs(m_file), item.slot));
				c.set("g", static_cast<double>(m_gesture));
				out.push_back(std::move(c));
				++m_done;
			}
			return out;
		}

		Value progress(const std::string& _text = {}) const
		{
			Value m = Value::object();
			m.set("type", "syxProgress");
			m.set("done", static_cast<int>(m_done));
			m.set("total", static_cast<int>(m_total));
			m.set("running", !m_queue.empty());
			m.set("text", _text);
			return m;
		}

	private:
		elektronData::SyxFile m_file;
		std::vector<elektronData::SyxItem> m_queue;
		size_t m_total = 0, m_done = 0;
		uint64_t m_gesture = 0;
	};
}
