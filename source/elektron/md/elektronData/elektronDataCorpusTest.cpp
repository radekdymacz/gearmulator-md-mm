// Corpus test: every Machinedrum dump found under the given files/directories
// must decode and re-encode byte for byte, and survive the JSON contract
// (value -> JSON -> value) unchanged. Files may hold one or several messages.
//
//   elektronDataCorpusTest [--json <out-dir>] <file-or-dir>...
//
// --json also writes each dump's contract document to <out-dir>.

#include "mdCommands.h"
#include "mdGlobal.h"
#include "mdJson.h"
#include "mdKit.h"
#include "mdPattern.h"
#include "mdSong.h"

#include "baseLib/filesystem.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace
{
	using Bytes = std::vector<uint8_t>;

	struct Tally
	{
		size_t ok = 0;
		size_t failed = 0;
	};

	std::map<std::string, Tally> g_tally;
	int g_failures = 0;
	std::string g_jsonOut;
	size_t g_jsonCount = 0;

	std::vector<Bytes> splitMessages(const Bytes& _file)
	{
		std::vector<Bytes> messages;
		for(size_t i = 0; i < _file.size(); ++i)
		{
			if(_file[i] != 0xf0)
				continue;
			size_t end = i + 1;
			while(end < _file.size() && _file[end] != 0xf7)
				++end;
			if(end == _file.size())
				break;
			messages.emplace_back(_file.begin() + static_cast<std::ptrdiff_t>(i),
				_file.begin() + static_cast<std::ptrdiff_t>(end + 1));
			i = end;
		}
		return messages;
	}

	template<typename T, typename Decode, typename Encode, typename ToJson, typename FromJson>
	bool roundTrip(const Bytes& _m, Decode _decode, Encode _encode, ToJson _toJson, FromJson _fromJson,
		std::string& _why)
	{
		const std::optional<T> value = _decode(_m);
		if(!value)
		{
			_why = "does not decode";
			return false;
		}
		if(_encode(*value) != _m)
		{
			_why = "re-encode differs";
			return false;
		}
		const auto text = elektronData::json::write(_toJson(*value));
		if(!g_jsonOut.empty())
		{
			const auto pretty = elektronData::json::write(_toJson(*value), 1);
			const auto path = g_jsonOut + "/" + std::to_string(g_jsonCount++) + ".json";
			baseLib::filesystem::writeFile(path, reinterpret_cast<const uint8_t*>(pretty.data()), pretty.size());
		}
		const auto parsed = elektronData::json::parse(text);
		if(!parsed)
		{
			_why = "JSON does not parse back";
			return false;
		}
		std::vector<std::string> errors;
		const std::optional<T> back = _fromJson(*parsed, errors);
		if(!back)
		{
			_why = "JSON rejected: " + (errors.empty() ? std::string("?") : errors.front());
			return false;
		}
		if(!(*back == *value))
		{
			_why = "JSON round trip changes the value";
			return false;
		}
		return true;
	}

	void checkMessage(const std::string& _source, const Bytes& _m)
	{
		std::string type;
		std::string why;
		bool ok = false;
		switch(elektronData::mdDumpCommand(_m))
		{
		case elektronData::g_mdPatternDump:
			type = "pattern";
			ok = roundTrip<elektronData::MdPattern>(_m, elektronData::decodeMdPattern, elektronData::encodeMdPattern,
				elektronData::patternToJson, elektronData::patternFromJson, why);
			break;
		case elektronData::g_mdKitDump:
			type = "kit";
			ok = roundTrip<elektronData::MdKit>(_m, elektronData::decodeMdKit, elektronData::encodeMdKit,
				elektronData::kitToJson, elektronData::kitFromJson, why);
			break;
		case elektronData::g_mdSongDump:
			type = "song";
			ok = roundTrip<elektronData::MdSong>(_m, elektronData::decodeMdSong, elektronData::encodeMdSong,
				elektronData::songToJson, elektronData::songFromJson, why);
			break;
		case elektronData::g_mdGlobalDump:
			type = "global";
			ok = roundTrip<elektronData::MdGlobal>(_m, elektronData::decodeMdGlobal, elektronData::encodeMdGlobal,
				elektronData::globalToJson, elektronData::globalFromJson, why);
			break;
		default:
			return;
		}
		auto& t = g_tally[type];
		if(ok)
		{
			++t.ok;
			return;
		}
		++t.failed;
		++g_failures;
		std::fprintf(stderr, "FAIL %s (%s, %zu bytes): %s\n", _source.c_str(), type.c_str(), _m.size(), why.c_str());
	}

	void checkFile(const std::string& _path)
	{
		Bytes bytes;
		if(!baseLib::filesystem::readFile(bytes, _path))
		{
			std::fprintf(stderr, "FAIL cannot read %s\n", _path.c_str());
			++g_failures;
			return;
		}
		for(const auto& m : splitMessages(bytes))
			checkMessage(_path, m);
	}

	void checkPath(const std::string& _path)
	{
		if(!baseLib::filesystem::isDirectory(_path))
		{
			checkFile(_path);
			return;
		}
		std::vector<std::string> entries;
		baseLib::filesystem::getDirectoryEntries(entries, _path);
		std::sort(entries.begin(), entries.end());
		for(const auto& e : entries)
		{
			if(baseLib::filesystem::isDirectory(e))
				checkPath(e);
			else if(e.size() > 4 && e.compare(e.size() - 4, 4, ".syx") == 0)
				checkFile(e);
		}
	}
}

int main(const int _argc, char** _argv)
{
	if(_argc < 2)
	{
		std::puts("usage: elektronDataCorpusTest <file-or-dir>...");
		return 77;
	}
	for(int i = 1; i < _argc; ++i)
	{
		if(std::string(_argv[i]) == "--json" && i + 1 < _argc)
		{
			g_jsonOut = _argv[++i];
			continue;
		}
		checkPath(_argv[i]);
	}
	size_t total = 0;
	for(const auto& [type, t] : g_tally)
	{
		std::printf("%-8s %5zu byte-exact + JSON-exact, %zu failed\n", type.c_str(), t.ok, t.failed);
		total += t.ok + t.failed;
	}
	std::printf("elektronDataCorpusTest: %zu dumps, %s\n", total, g_failures || !total ? "FAIL" : "PASS");
	return g_failures || !total ? 1 : 0;
}
