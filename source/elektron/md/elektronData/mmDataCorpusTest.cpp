// MM corpus test: every Monomachine dump found under the given files or
// directories must decode and re-encode byte for byte, survive the JSON contract
// (value -> JSON -> value) unchanged, and validate clean.
//
//   mmDataCorpusTest [--json <out-dir>] [--schema <file>] <file-or-dir>...

#include "mmDump.h"
#include "mmGlobal.h"
#include "mmJson.h"
#include "mmKit.h"
#include "mmPattern.h"
#include "mmSong.h"
#include "mmValidate.h"

#include "jsonSchema.h"

#include "baseLib/filesystem.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace
{
	using Bytes = std::vector<uint8_t>;
	namespace ed = elektronData;

	struct Tally
	{
		size_t ok = 0;
		size_t failed = 0;
	};

	std::map<std::string, Tally> g_tally;
	int g_failures = 0;
	std::string g_jsonOut;
	// --schema: every document must also validate against the contract's JSON Schema (the executable spec).
	std::optional<elektronData::json::Schema> g_schema;
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
			messages.emplace_back(_file.begin() + static_cast<std::ptrdiff_t>(i), _file.begin() + static_cast<std::ptrdiff_t>(end + 1));
			i = end;
		}
		return messages;
	}

	template<typename T, typename Decode, typename Encode, typename ToJson, typename FromJson>
	bool roundTrip(const Bytes& _m, Decode _decode, Encode _encode, ToJson _toJson, FromJson _fromJson, std::string& _why)
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
		const auto problems = ed::validate(*value);
		if(!problems.empty())
		{
			_why = "does not validate: " + problems.front();
			return false;
		}
		const auto doc = _toJson(*value);
		if(g_schema)
			if(const auto problems = g_schema->validate(doc); !problems.empty())
			{
				_why = "schema: " + problems.front();
				return false;
			}
		const auto text = ed::json::write(doc);
		if(!g_jsonOut.empty())
		{
			const auto pretty = ed::json::write(doc, 1);
			const auto path = g_jsonOut + "/" + std::to_string(g_jsonCount++) + ".json";
			baseLib::filesystem::writeFile(path, reinterpret_cast<const uint8_t*>(pretty.data()), pretty.size());
		}
		const auto parsed = ed::json::parse(text);
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
		if(_m.size() < 10 || _m[4] != ed::g_mmProductId)
			return;
		std::string type;
		std::string why;
		bool ok = false;
		switch(_m[6])
		{
		case ed::g_mmPatternDump:
			type = "pattern";
			ok = roundTrip<ed::MmPattern>(_m, ed::decodeMmPattern, ed::encodeMmPattern, ed::mmPatternToJson, ed::mmPatternFromJson, why);
			break;
		case ed::g_mmKitDump:
			type = "kit";
			ok = roundTrip<ed::MmKit>(_m, ed::decodeMmKit, ed::encodeMmKit, ed::mmKitToJson, ed::mmKitFromJson, why);
			break;
		case ed::g_mmSongDump:
			type = "song";
			ok = roundTrip<ed::MmSong>(_m, ed::decodeMmSong, ed::encodeMmSong, ed::mmSongToJson, ed::mmSongFromJson, why);
			break;
		case ed::g_mmGlobalDump:
			type = "global";
			ok = roundTrip<ed::MmGlobal>(_m, ed::decodeMmGlobal, ed::encodeMmGlobal, ed::mmGlobalToJson, ed::mmGlobalFromJson, why);
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
		std::puts("usage: mmDataCorpusTest [--json <out-dir>] [--schema <file>] <file-or-dir>...");
		return 77;
	}
	for(int i = 1; i < _argc; ++i)
	{
		if(std::string(_argv[i]) == "--schema" && i + 1 < _argc)
		{
			std::vector<uint8_t> text;
			const auto* path = _argv[++i];
			const auto root = baseLib::filesystem::readFile(text, path)
				? elektronData::json::parse(std::string(text.begin(), text.end())) : std::nullopt;
			if(!root)
			{
				std::fprintf(stderr, "FAIL cannot read the schema %s\n", path);
				return 1;
			}
			g_schema.emplace(*root);
			continue;
		}
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
		std::printf("%-8s %5zu byte-exact + JSON-exact + valid, %zu failed\n", type.c_str(), t.ok, t.failed);
		total += t.ok + t.failed;
	}
	std::printf("mmDataCorpusTest: %zu dumps, %s\n", total, g_failures || !total ? "FAIL" : "PASS");
	return g_failures || !total ? 1 : 0;
}
