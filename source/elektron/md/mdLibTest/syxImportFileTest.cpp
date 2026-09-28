// Local test (P7): parse two full factory backups, one Machinedrum and one Monomachine, check the
// counts, zero problems, fullBackup, and that every message re-encodes to its own bytes. The files
// are third-party data and live outside the repo: the test exits 77 (skip) when they are absent,
// and prints only counts, slots and reasons, never contents.
//
//   syxImportFileTest [<md.syx> <mm.syx>]

#include "elektronData/syxImport.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
	namespace ed = elektronData;
	using Bytes = std::vector<uint8_t>;
	int g_failures = 0;

	const char* const g_mdPath = "/Users/radek/Downloads/AE_LIVE_ELEKTRONS_BACKUP_010308/md010308.syx";
	const char* const g_mmPath = "/Users/radek/Downloads/AE_LIVE_ELEKTRONS_BACKUP_010308/mm010308.syx";

	void check(const bool _ok, const std::string& _what)
	{
		std::printf("  %s %s\n", _ok ? "ok  " : "FAIL", _what.c_str());
		if(!_ok)
			++g_failures;
	}

	bool load(const std::string& _path, Bytes& _out)
	{
		std::ifstream stream(_path, std::ios::binary);
		if(!stream)
			return false;
		_out.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
		return true;
	}

	struct Expected
	{
		ed::SyxModel model;
		size_t messages, globals, kits, patterns, songs;
	};

	// The first byte where two messages differ, or the shorter size.
	size_t firstDifference(const Bytes& _a, const Bytes& _b)
	{
		size_t i = 0;
		while(i < _a.size() && i < _b.size() && _a[i] == _b[i])
			++i;
		return i;
	}

	void run(const std::string& _path, const Bytes& _bytes, const Expected& _expected)
	{
		std::printf("%s\n", _path.c_str());
		const auto file = ed::parseSyx(_bytes);
		const auto s = ed::summarizeSyx(file);
		std::printf("  model %s, %zu messages: %zu globals, %zu kits, %zu patterns, %zu songs, %zu problems\n",
			ed::syxModelName(file.model), file.messages.size(), s.globals, s.kits, s.patterns, s.songs,
			file.problems.size());

		check(file.model == _expected.model, std::string("model ") + ed::syxModelName(_expected.model));
		check(file.messages.size() == _expected.messages, std::to_string(_expected.messages) + " messages");
		check(s.globals == _expected.globals && s.kits == _expected.kits && s.patterns == _expected.patterns
			&& s.songs == _expected.songs, "counts per kind");
		check(file.problems.empty(), "no problems");
		for(const auto& p : file.problems)
			std::printf("    message %zu (%s %d): %s\n", p.index, ed::syxKindName(p.kind), p.slot,
				ed::syxStatusName(p.status));
		check(s.fullBackup, "full backup");

		size_t exact = 0;
		for(const auto& r : file.messages)
		{
			if(r.status != ed::SyxStatus::Ok)
				continue;
			const Bytes in(_bytes.begin() + static_cast<std::ptrdiff_t>(r.offset),
				_bytes.begin() + static_cast<std::ptrdiff_t>(r.offset + r.size));
			const auto out = ed::syxMessage(file, {r.kind, static_cast<uint8_t>(r.slot)});
			if(out == in)
			{
				++exact;
				continue;
			}
			std::printf("    message %zu (%s %d) re-encodes differently: %zu bytes in, %zu out, first difference at %zu\n",
				r.index, ed::syxKindName(r.kind), r.slot, in.size(), out.size(), firstDifference(in, out));
		}
		check(exact == file.messages.size(), std::to_string(exact) + "/" + std::to_string(file.messages.size())
			+ " messages re-encode byte-exactly");

		const auto written = ed::writeSyx(file);
		check(ed::parseSyx(written).messages.size() == file.messages.size()
			&& ed::writeSyx(ed::parseSyx(written)) == written, "export -> parse -> export is byte-exact");
	}
}

int main(const int _argc, const char* _argv[])
{
	const std::string mdPath = _argc > 2 ? _argv[1] : g_mdPath;
	const std::string mmPath = _argc > 2 ? _argv[2] : g_mmPath;

	Bytes md, mm;
	if(!load(mdPath, md) || !load(mmPath, mm))
	{
		std::printf("SKIP: the local backups are not there\n");
		return 77;
	}

	run(mdPath, md, {ed::SyxModel::Md, 232, 8, 64, 128, 32});
	run(mmPath, mm, {ed::SyxModel::Mm, 288, 8, 128, 128, 24});
	std::printf("%s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
	return g_failures ? 1 : 0;
}
