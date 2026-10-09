// Local test (P7, B-019): real .syx files of the person's own (backups, other people's dumps), parsed as the import
// does. Prints what is in each file (message kinds, their format bytes and sizes, what the import would leave out
// and why, what the editor's validation says about the documents) and checks that the editor reads every dump of
// its model and re-encodes it to its own bytes. The files are third-party data and live outside the repo: their
// paths come from the environment (MD_SYX, MM_SYX: one file each; SYX_FILES: more, separated by ':'), and the
// test exits 77 (skip) when none is given. It prints counts, slots and reasons, never contents.
//
//   MD_SYX=<md.syx> MM_SYX=<mm.syx> syxImportFileTest

#include "elektronData/syxImport.h"
#include "elektronData/mdValidate.h"
#include "elektronData/mmValidate.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace
{
	namespace ed = elektronData;
	using Bytes = std::vector<uint8_t>;
	int g_failures = 0;

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

	// The first byte where two messages differ, or the shorter size.
	size_t firstDifference(const Bytes& _a, const Bytes& _b)
	{
		size_t i = 0;
		while(i < _a.size() && i < _b.size() && _a[i] == _b[i])
			++i;
		return i;
	}

	template<typename Docs>
	void validation(const Docs& _docs)
	{
		std::map<std::string, int> reasons;
		const auto add = [&](const char* _kind, const std::vector<std::string>& _v)
		{
			if(!_v.empty())
				++reasons[std::string(_kind) + ": " + _v.front()];
		};
		for(const auto& [s, d] : _docs.globals) add("global", ed::validate(d));
		for(const auto& [s, d] : _docs.kits) add("kit", ed::validate(d));
		for(const auto& [s, d] : _docs.patterns) add("pattern", ed::validate(d));
		for(const auto& [s, d] : _docs.songs) add("song", ed::validate(d));
		for(const auto& [r, n] : reasons)
			std::printf("    the editor's validation: %d x %s\n", n, r.c_str());
	}

	void run(const std::string& _path, const Bytes& _bytes)
	{
		std::printf("%s (%zu bytes)\n", _path.c_str(), _bytes.size());
		const auto file = ed::parseSyx(_bytes);
		const auto s = ed::summarizeSyx(file);
		std::printf("  model %s, %zu messages: %zu globals, %zu kits, %zu patterns, %zu songs%s, %zu problems\n",
			ed::syxModelName(file.model), file.messages.size(), s.globals, s.kits, s.patterns, s.songs,
			s.fullBackup ? " (a full backup)" : "", file.problems.size());

		// what is in it: per model, kind and format, the count and the sizes
		struct Sizes { int n = 0; size_t min = SIZE_MAX, max = 0; };
		std::map<std::string, Sizes> inventory;
		for(const auto& r : file.messages)
		{
			char key[96];
			std::snprintf(key, sizeof(key), "%s %s%s%s", ed::syxModelName(r.model), r.kind == ed::SyxKind::Other ? "command " : ed::syxKindName(r.kind),
				r.kind == ed::SyxKind::Other ? (r.command >= 0 ? std::to_string(r.command).c_str() : "-") : " format ",
				r.kind == ed::SyxKind::Other ? "" : (std::to_string(r.version) + "." + std::to_string(r.revision)).c_str());
			auto& v = inventory[key];
			++v.n;
			v.min = std::min(v.min, r.size);
			v.max = std::max(v.max, r.size);
		}
		for(const auto& [k, v] : inventory)
			std::printf("    %-40s x%-4d %zu..%zu bytes\n", k.c_str(), v.n, v.min, v.max);
		std::map<std::string, int> problems;
		for(const auto& p : file.problems)
			++problems[std::string(ed::syxKindName(p.kind)) + ": " + ed::syxStatusName(p.status)];
		for(const auto& [p, n] : problems)
			std::printf("    the editor's codec: %d x %s\n", n, p.c_str());
		const auto docsModel = ed::documentsModel(file);
		const auto model = docsModel != ed::SyxModel::Unknown ? docsModel : file.model;
		std::map<std::string, int> unsendable;
		size_t leftOut = 0;
		for(const auto& r : file.messages)
			if(const auto why = ed::syxUnsendable(r, _bytes, model); !why.empty())
			{
				++unsendable[why];
				++leftOut;
			}
		for(const auto& [w, n] : unsendable)
			std::printf("    left out by an import: %d x %s\n", n, w.c_str());
		if(model == ed::SyxModel::Md)
			validation(file.md);
		else if(model == ed::SyxModel::Mm)
			validation(file.mm);

		if(docsModel == ed::SyxModel::Unknown)
		{
			// no user data (an OS update, another device's dumps): an import sends nothing of it
			check(leftOut == file.messages.size(), "no user data in it: an import leaves every message out (" + std::to_string(leftOut) + " of "
				+ std::to_string(file.messages.size()) + ")");
			return;
		}
		size_t framed = 0, unreadable = 0;
		for(const auto& r : file.messages)
		{
			framed += r.status == ed::SyxStatus::Truncated || r.status == ed::SyxStatus::BadData;
			unreadable += r.model == model && r.kind != ed::SyxKind::Other && r.status != ed::SyxStatus::Ok && r.status != ed::SyxStatus::DuplicateSlot;
		}
		check(framed == 0, "no broken messages (" + std::to_string(framed) + ")");
		check(unreadable == 0, "the editor's codec reads every dump of the file's model (" + std::to_string(unreadable) + " not)");

		size_t exact = 0, dumps = 0;
		for(const auto& r : file.messages)
		{
			if(r.status != ed::SyxStatus::Ok)
				continue;
			++dumps;
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
		check(exact == dumps, std::to_string(exact) + "/" + std::to_string(dumps) + " dumps re-encode byte-exactly");

		const auto written = ed::writeSyx(file);
		check(ed::writeSyx(ed::parseSyx(written)) == written, "export -> parse -> export is byte-exact");
	}
}

int main()
{
	std::vector<std::string> paths;
	for(const char* var : {"MD_SYX", "MM_SYX"})
		if(const char* p = std::getenv(var); p && *p)
			paths.emplace_back(p);
	if(const char* more = std::getenv("SYX_FILES"); more && *more)
	{
		std::string list = more;
		size_t from = 0;
		while(from <= list.size())
		{
			const auto to = list.find(':', from);
			const auto p = list.substr(from, to == std::string::npos ? std::string::npos : to - from);
			if(!p.empty())
				paths.push_back(p);
			if(to == std::string::npos)
				break;
			from = to + 1;
		}
	}
	size_t ran = 0;
	for(const auto& p : paths)
	{
		Bytes bytes;
		if(!load(p, bytes))
		{
			std::printf("%s: not found\n", p.c_str());
			continue;
		}
		run(p, bytes);
		++ran;
	}
	if(!ran)
	{
		std::printf("SKIP: no local .syx given (MD_SYX, MM_SYX, SYX_FILES)\n");
		return 77;
	}
	std::printf("%s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
	return g_failures ? 1 : 0;
}
