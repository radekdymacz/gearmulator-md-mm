#pragma once

// Files dropped on the editor's window (FOUNDATION.md, "Files dropped on the window"): what each is, by its extension, and
// the files of the last drop, which the window keeps. The page never sees a path: it hears {"type":"drop", drop, items:
// [{n, kind, name}], x, y} and answers with the window's commands, which name a file by its drop and its number
// (dropRom, dropSyx, dropSample: deskHost.cpp); the window hands the file to the session as its chooser would. Pure: no
// JUCE; mdDroppedFilesTest.

#include "elektronData/json.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mdJucePlugin::droppedFiles
{
	enum class Kind : uint8_t
	{
		Rom,		// a firmware image: .bin, or a .zip with one (installRom checks it)
		Sysex,		// .syx: the SysEx import window
		Sample,		// WAV or AIFF (what the sample chooser offers): a UW ROM slot
		Unknown		// anything else: the page says what it takes
	};

	inline const char* kindName(const Kind _kind)
	{
		switch(_kind)
		{
		case Kind::Rom:		return "rom";
		case Kind::Sysex:	return "sysex";
		case Kind::Sample:	return "sample";
		case Kind::Unknown:	break;
		}
		return "unknown";
	}

	// The file's name: what follows the last / (or \, a Windows path).
	inline std::string nameOf(const std::string& _path)
	{
		const auto slash = _path.find_last_of("/\\");
		return slash == std::string::npos ? _path : _path.substr(slash + 1);
	}

	// By the extension, in any case: the same extensions the window's choosers offer (mdPageEditor.cpp).
	inline Kind kindOf(const std::string& _path)
	{
		const auto name = nameOf(_path);
		const auto dot = name.find_last_of('.');
		if(dot == std::string::npos || dot == 0)
			return Kind::Unknown;
		std::string ext = name.substr(dot + 1);
		std::transform(ext.begin(), ext.end(), ext.begin(), [](const unsigned char _c) { return static_cast<char>(std::tolower(_c)); });
		if(ext == "bin" || ext == "zip")
			return Kind::Rom;
		if(ext == "syx")
			return Kind::Sysex;
		if(ext == "wav" || ext == "wave" || ext == "aif" || ext == "aiff" || ext == "aifc")
			return Kind::Sample;
		return Kind::Unknown;
	}

	struct Item
	{
		size_t n = 0;		// its place in the drop, from 0: what the page's command names
		Kind kind = Kind::Unknown;
		std::string name;	// the file's name, for the page's words (never its folder)
	};

	inline std::vector<Item> classify(const std::vector<std::string>& _paths)
	{
		std::vector<Item> items;
		items.reserve(_paths.size());
		for(size_t i = 0; i < _paths.size(); ++i)
			items.push_back({i, kindOf(_paths[i]), nameOf(_paths[i])});
		return items;
	}

	// Whether the window takes a drag of these files: one of them at least is a kind it knows (the others are said
	// to be unknown once dropped). A drag of none it knows is refused while it is over the window (the pointer says so).
	inline bool accepts(const std::vector<std::string>& _paths)
	{
		return std::any_of(_paths.begin(), _paths.end(), [](const std::string& _p) { return kindOf(_p) != Kind::Unknown; });
	}

	// {"type":"drop", drop, items:[{n, kind, name}], x, y}: x and y where it was dropped, in the page's CSS pixels.
	inline elektronData::json::Value dropMessage(const int _drop, const std::vector<Item>& _items, const double _x, const double _y)
	{
		using elektronData::json::Value;
		auto items = Value::array();
		for(const auto& it : _items)
		{
			auto v = Value::object();
			v.set("n", static_cast<double>(it.n));
			v.set("kind", kindName(it.kind));
			v.set("name", it.name);
			items.push(std::move(v));
		}
		auto m = Value::object();
		m.set("type", "drop");
		m.set("drop", _drop);
		m.set("items", std::move(items));
		m.set("x", _x);
		m.set("y", _y);
		return m;
	}

	// {"type":"dragFiles", active}: files the window takes are over it (true), or have left it (false; a drop says so too).
	inline elektronData::json::Value dragMessage(const bool _active)
	{
		auto m = elektronData::json::Value::object();
		m.set("type", "dragFiles");
		m.set("active", _active);
		return m;
	}

	// The files of the window's last drop, by their number in it. Each serves once, and only as the kind it was
	// dropped as (a .wav is never installed as a ROM, whatever the page asks); a new drop, clear() or g_keepMs without
	// use forgets them, so a late or repeated command acts on nothing.
	class DropBook
	{
	public:
		static constexpr double g_keepMs = 10.0 * 60.0 * 1000.0;	// a long queue of samples uses one file at a time

		// A new drop: the earlier one's files are forgotten. Its number (from 1, never again in this book).
		int add(const std::vector<std::string>& _paths, const double _nowMs)
		{
			m_files.clear();
			for(const auto& p : _paths)
				m_files.push_back(Held{p, kindOf(p), false});
			m_lastUseMs = _nowMs;
			return ++m_drop;
		}

		// The path of file _n of drop _drop when it is still held as _kind, and it is used up; else nothing.
		std::optional<std::string> take(const int _drop, const size_t _n, const Kind _kind, const double _nowMs)
		{
			if(_drop != m_drop || _n >= m_files.size() || _nowMs - m_lastUseMs > g_keepMs)
				return std::nullopt;
			auto& f = m_files[_n];
			if(f.used || f.kind != _kind)
				return std::nullopt;
			f.used = true;
			m_lastUseMs = _nowMs;
			return f.path;
		}

		void clear() { m_files.clear(); }

		int current() const { return m_drop; }
		// How many files of the current drop are still to be used.
		size_t held() const
		{
			return static_cast<size_t>(std::count_if(m_files.begin(), m_files.end(), [](const Held& _f) { return !_f.used; }));
		}

	private:
		struct Held
		{
			std::string path;
			Kind kind = Kind::Unknown;
			bool used = false;
		};
		int m_drop = 0;
		std::vector<Held> m_files;
		double m_lastUseMs = 0;
	};
}
