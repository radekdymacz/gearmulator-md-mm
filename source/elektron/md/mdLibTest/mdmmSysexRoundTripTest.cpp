// The local release gate's SysEx round trip (doc/release/LOCAL-GATE.md): the firmware's dumps, imported again the
// way the editors import a .syx file, come back as they went.
//   mdmmSysexRoundTripTest <ROM> md|mm [fixture.syx ...]
// 1. A fresh machine (MD OS 1.63 or MM OS 1.32B, headless) is dumped whole over SysEx: every global, kit, pattern and
//    song slot, one request at a time (elektronData::syxRequest), each reply matched by its command and slot byte.
// 2. That dump goes into another fresh machine as the editor's import sends a file (B-019, mdSyxSession.h): the
//    messages unchanged, in write order, the MD's at the pace the emulated UART reads them, the MM's on SYSEX RECV
//    (mmSysexRecv.h, the adapter's own key macro); a dump of the active global is made active after it (0x56,
//    B-026). The machine is dumped again: every message must equal the first dump's byte for byte. Nothing is
//    normalised: a dump of these firmwares carries no time or counter, so any difference is a failure.
// 3. Each fixture (a real backup of the model) goes into a fresh machine the same way (what syxUnsendable refuses
//    and non-dump messages are left out, as the editor's preview leaves them out by default). The machine's dump is
//    compared with the file document by document in the form the editor compares an import (syxCanonical: the
//    contract's JSON without the "firmware" group, i.e. format bytes, residue and undecoded bytes, which a machine
//    rewrites as it stores a dump, e.g. an older pattern format it converts). Byte-identical documents are counted
//    as well, for information. A damaged dump in the fixture (checksum, length, data) fails the run: it is sent, but
//    its slot has no document to compare with. On the MM, a dump the machine counts as bad on SYSEX RECV fails too.
// 4. Then the first dump goes over the fixture's content in that machine and the machine is dumped again: it must
//    equal the first dump byte for byte. This is the round trip of step 2 on a machine whose slots held something
//    else, so it shows that the import landed in every slot where the fixture differed from a fresh machine (step 2
//    alone cannot: both fresh machines start with the same content).
// Prints counts, slots and offsets, never contents. One line per step, then PASS or FAIL as the last line.
// Exits 77 without arguments, 1 on any failure. Manual: needs a user-supplied ROM, and the fixtures are third-party
// data that never enter the repo.

#include "mdFirmwareSession.h"
#include "mmSysexRecv.h"

#include "mdLib/mdromcheck.h"

#include "elektronData/mdCommands.h"
#include "elektronData/mdGlobal.h"
#include "elektronData/mmCommands.h"
#include "elektronData/syxImport.h"

#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace mdFirmwareSession;
namespace ed = elektronData;

namespace
{
	constexpr ed::SyxKind g_kinds[] = {ed::SyxKind::Global, ed::SyxKind::Kit, ed::SyxKind::Pattern, ed::SyxKind::Song};

	struct Target
	{
		md::MachineModel machine = md::MachineModel::Machinedrum;
		ed::SyxModel syx = ed::SyxModel::Md;
		const char* name = "MD";
	};

	// Every document of a machine, by kind and slot: the dump message as the firmware sent it.
	using Dump = std::map<ed::SyxItem, Bytes>;

	int g_failures = 0;

	void check(const bool _ok, const std::string& _what)
	{
		if(_ok)
			return;
		std::printf("  FAIL %s\n", _what.c_str());
		++g_failures;
	}

	uint8_t dumpCommand(const ed::SyxKind _kind)
	{
		switch(_kind)
		{
		case ed::SyxKind::Global: return 0x50;
		case ed::SyxKind::Kit: return 0x52;
		case ed::SyxKind::Pattern: return 0x67;
		case ed::SyxKind::Song: return 0x69;
		default: return 0;
		}
	}

	std::string itemName(const ed::SyxItem& _item)
	{
		return std::string(ed::syxKindName(_item.kind)) + " " + std::to_string(_item.slot);
	}

	double wallSeconds(const std::chrono::steady_clock::time_point _since)
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - _since).count();
	}

	double machineSeconds(const uint64_t _frames)
	{
		return static_cast<double>(_frames) / g_rate;
	}

	std::unique_ptr<Machine> boot(const Bytes& _rom, const std::string& _name, const Target& _target)
	{
		// The machine is large: on the heap.
		return std::make_unique<Machine>(_rom, _name, Bytes{}, true, _target.machine);
	}

	// One document: its request, then the reply with its command and slot. Three tries, 5 s of machine time each
	// (the editor's read-back gives up after three, B-020).
	std::optional<Bytes> readBack(Machine& _m, const Target& _target, const ed::SyxKind _kind, const uint8_t _slot)
	{
		const auto request = ed::syxRequest(_target.syx, _kind, _slot);
		const uint8_t product = _target.syx == ed::SyxModel::Md ? 0x02 : 0x03;
		std::optional<Bytes> reply;
		_m.onSysex = [&](const Bytes& _sysex)
		{
			if(!reply && _sysex.size() > 14 && _sysex[4] == product && _sysex[6] == dumpCommand(_kind)
				&& _sysex[9] == _slot)
				reply = _sysex;
		};
		for(int attempt = 0; attempt < 3 && !reply; ++attempt)
		{
			_m.send(request);
			const auto deadline = _m.now() + g_rate * 5;
			while(!reply && _m.now() < deadline)
				_m.step();
		}
		_m.onSysex = {};
		return reply;
	}

	// The machine's whole content. A slot without a reply is a failure (and left out).
	Dump dumpAll(Machine& _m, const Target& _target, const char* _what)
	{
		const auto wall = std::chrono::steady_clock::now();
		const auto start = _m.now();
		Dump dump;
		std::map<ed::SyxKind, size_t> counts;
		size_t bytes = 0;
		for(const auto kind : g_kinds)
		{
			const auto slots = ed::syxSlotCount(_target.syx, kind);
			for(size_t slot = 0; slot < slots; ++slot)
			{
				const ed::SyxItem item{kind, static_cast<uint8_t>(slot)};
				auto reply = readBack(_m, _target, kind, item.slot);
				check(reply.has_value(), std::string(_what) + ": no reply for " + itemName(item));
				if(!reply)
					continue;
				bytes += reply->size();
				++counts[kind];
				dump.emplace(item, std::move(*reply));
			}
		}
		std::printf("%s: dumped globals=%zu kits=%zu patterns=%zu songs=%zu bytes=%zu in %.1f s machine, %.1f s wall\n",
			_what, counts[ed::SyxKind::Global], counts[ed::SyxKind::Kit], counts[ed::SyxKind::Pattern],
			counts[ed::SyxKind::Song], bytes, machineSeconds(_m.now() - start), wallSeconds(wall));
		return dump;
	}

	int activeGlobal(Machine& _m, const Target& _target)
	{
		if(_target.syx == ed::SyxModel::Md)
		{
			const auto s = ed::parseMdStatusResponse(_m.request(ed::mdStatusRequest(ed::MdStatus::GlobalSlot), 0x72));
			return s ? s->value : -1;
		}
		const auto s = ed::parseMmStatusResponse(_m.request(ed::mmStatusRequest(ed::MmStatus::Global), 0x72));
		return s ? s->value : -1;
	}

	// The editor's import of dumps (mdSyxSession.h through the desks' sendAsIs): the MD's straight into its stream,
	// back to back at the UART's pace (Machine::send returns once the UART has read the message), the MM's on SYSEX
	// RECV. A dump of the active global is made active once it is in (B-026), the MM's after SYSEX RECV is left.
	// Then the editor's settle before it asks for read-backs (250 ms MD, 1.5 s MM, deskStream / mmDeskAdapter).
	void import(Machine& _m, const Target& _target, const std::vector<Bytes>& _dumps, const char* _what)
	{
		const auto wall = std::chrono::steady_clock::now();
		const auto start = _m.now();
		const int active = activeGlobal(_m, _target);
		check(active >= 0, std::string(_what) + ": no reply to the active global status request");
		bool activeGlobalSent = false;
		for(const auto& d : _dumps)
			activeGlobalSent |= d.size() > 9 && d[6] == 0x50 && d[9] == active;

		if(_target.syx == ed::SyxModel::Md)
		{
			for(const auto& d : _dumps)
			{
				_m.send(d);
				if(d.size() > 9 && d[6] == 0x50 && d[9] == active)
					_m.send(ed::mdSetActiveGlobal(static_cast<uint8_t>(active)));
			}
			_m.run(250);
			std::printf("%s: imported %zu dumps in %.1f s machine, %.1f s wall\n", _what, _dumps.size(),
				machineSeconds(_m.now() - start), wallSeconds(wall));
			return;
		}
		const auto r = mmSysexRecv::send(_m, _dumps);
		if(activeGlobalSent)
			_m.send(ed::mmSetActiveGlobal(static_cast<uint8_t>(active)));
		_m.run(1500);
		std::printf("%s: imported %zu dumps on SYSEX RECV (taken %u, errors %u) in %.1f s machine, %.1f s wall\n",
			_what, _dumps.size(), r.taken, r.errors, machineSeconds(_m.now() - start), wallSeconds(wall));
		check(r.taken == _dumps.size() && r.errors == 0, std::string(_what) + ": the Monomachine took "
			+ std::to_string(r.taken) + " of " + std::to_string(_dumps.size()) + " dumps, " + std::to_string(r.errors)
			+ " with errors");
	}

	std::vector<Bytes> messages(const Dump& _dump)
	{
		std::vector<Bytes> out;
		for(const auto& [item, bytes] : _dump)
			out.push_back(bytes);
		return out;
	}

	// The first dump against the second, byte for byte: every document of _want must come back as it is.
	void compareBytes(const Dump& _want, const Dump& _got, const char* _what, const Dump* _before = nullptr)
	{
		size_t equal = 0, proven = 0;
		std::string first;
		for(const auto& [item, want] : _want)
		{
			const auto it = _got.find(item);
			if(it != _got.end() && it->second == want)
			{
				++equal;
				if(_before)
				{
					const auto b = _before->find(item);
					proven += b == _before->end() || b->second != want;
				}
				continue;
			}
			if(!first.empty())
				continue;
			if(it == _got.end())
			{
				first = itemName(item) + " missing";
				continue;
			}
			const auto& got = it->second;
			size_t at = 0;
			while(at < want.size() && at < got.size() && want[at] == got[at])
				++at;
			char text[160];
			std::snprintf(text, sizeof(text), "%s size %zu/%zu, first difference at byte %zu (%02x/%02x)",
				itemName(item).c_str(), want.size(), got.size(), at, at < want.size() ? want[at] : 0,
				at < got.size() ? got[at] : 0);
			first = text;
		}
		std::printf("%s: equal byte for byte %zu/%zu", _what, equal, _want.size());
		if(_before)
			std::printf(", landed over different content %zu", proven);
		std::printf(", first_mismatch=%s\n", first.empty() ? "none" : first.c_str());
		check(equal == _want.size() && _got.size() == _want.size(), std::string(_what) + ": " + first);
	}

	// A fixture's documents against what the machine holds after its import, in the editor's comparison form.
	void compareFixture(const ed::SyxFile& _file, const Bytes& _bytes, const Target& _target, const Dump& _machine,
		const char* _what)
	{
		std::map<ed::SyxKind, std::pair<size_t, size_t>> perKind;	// equal, total
		size_t identical = 0;
		std::string first;
		std::map<ed::SyxItem, Bytes> lastMessage;
		for(const auto& r : _file.messages)
			if(r.kind != ed::SyxKind::Other && r.slot >= 0 && ed::syxUnsendable(r, _bytes, _target.syx).empty())
				lastMessage[{r.kind, static_cast<uint8_t>(r.slot)}] = ed::syxBytes(_bytes, r);
		for(const auto& item : ed::syxItems(_file))
		{
			auto& [equal, total] = perKind[item.kind];
			++total;
			const auto want = _target.syx == ed::SyxModel::Md ? ed::syxCanonical(_file.md, item.kind, item.slot)
				: ed::syxCanonical(_file.mm, item.kind, item.slot);
			const auto it = _machine.find(item);
			const auto got = it == _machine.end() ? std::nullopt : ed::syxCanonical(_target.syx, it->second);
			if(!want.empty() && got && got->bytes == want)
			{
				++equal;
				const auto m = lastMessage.find(item);
				identical += m != lastMessage.end() && m->second == it->second;
				continue;
			}
			if(!first.empty())
				continue;
			if(want.empty())
				first = itemName(item) + ": the editor's codec cannot read the file's dump";
			else if(it == _machine.end())
				first = itemName(item) + ": no read-back";
			else if(!got)
				first = itemName(item) + ": the editor's codec cannot read the machine's dump";
			else
			{
				size_t at = 0;
				while(at < want.size() && at < got->bytes.size() && want[at] == got->bytes[at])
					++at;
				first = itemName(item) + ": the documents differ from character " + std::to_string(at)
					+ " of the canonical form";
			}
		}
		size_t equal = 0, total = 0;
		std::printf("%s: file vs machine (canonical)", _what);
		for(const auto kind : g_kinds)
		{
			const auto& [e, t] = perKind[kind];
			equal += e;
			total += t;
			std::printf(" %ss=%zu/%zu", ed::syxKindName(kind), e, t);
		}
		std::printf(" byte_identical=%zu first_mismatch=%s\n", identical, first.empty() ? "none" : first.c_str());
		check(equal == total, std::string(_what) + ": " + first);
	}

	void fixture(const std::string& _path, const Bytes& _rom, const std::string& _romName, const Target& _target,
		const Dump& _fresh)
	{
		const auto bytes = load(_path);
		const auto file = ed::parseSyx(bytes);
		const auto summary = ed::summarizeSyx(file);
		std::vector<Bytes> dumps;
		std::map<std::string, size_t> skipped;
		size_t others = 0;
		for(const auto& r : file.messages)
		{
			const auto why = ed::syxUnsendable(r, bytes, _target.syx);
			if(!why.empty())
			{
				++skipped[why];
				continue;
			}
			if(r.kind == ed::SyxKind::Other || r.slot < 0)
			{
				++others;
				continue;
			}
			dumps.push_back(ed::syxBytes(bytes, r));
		}
		std::printf("fixture %s: model=%s messages=%zu globals=%zu kits=%zu patterns=%zu songs=%zu problems=%zu "
			"to_send=%zu other_messages_left_out=%zu\n", _path.c_str(), ed::syxModelName(file.model),
			file.messages.size(), summary.globals, summary.kits, summary.patterns, summary.songs, file.problems.size(),
			dumps.size(), others);
		for(const auto& [why, n] : skipped)
			std::printf("  left out (cannot be sent): %zu x %s\n", n, why.c_str());
		// A damaged dump (bad checksum, wrong length, bad data) goes to the machine as the editor sends it, but has no
		// document to compare with: a fixture with one would leave that slot unchecked, so it is a failure of the
		// fixture. A later dump of the same slot (DuplicateSlot) is fine: it replaces the earlier one.
		size_t damaged = 0;
		std::string firstDamaged;
		for(const auto& r : file.problems)
		{
			if(r.status == ed::SyxStatus::DuplicateSlot)
				continue;
			if(!damaged++)
				firstDamaged = "message " + std::to_string(r.index + 1) + " (" + ed::syxKindName(r.kind) + " "
					+ std::to_string(r.slot) + "): " + ed::syxStatusName(r.status);
		}
		check(damaged == 0, "fixture " + _path + " holds " + std::to_string(damaged) + " damaged message(s), first "
			+ firstDamaged);
		check(ed::documentsModel(file) == _target.syx, "fixture " + _path + " is not a " + _target.name + " file");
		check(!dumps.empty(), "fixture " + _path + " holds no dump to send");
		if(dumps.empty() || ed::documentsModel(file) != _target.syx)
			return;

		auto m = boot(_rom, _romName, _target);
		import(*m, _target, dumps, "  fixture import");
		const auto afterFixture = dumpAll(*m, _target, "  fixture machine");
		compareFixture(file, bytes, _target, afterFixture, "  fixture compare");

		import(*m, _target, messages(_fresh), "  fresh dump over the fixture");
		const auto again = dumpAll(*m, _target, "  over-import machine");
		compareBytes(_fresh, again, "  round trip over the fixture", &afterFixture);
	}
}

int main(const int _argc, char** _argv)
{
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	if(_argc < 3)
	{
		std::puts("usage: mdmmSysexRoundTripTest <ROM> md|mm [fixture.syx ...]");
		return 77;
	}
	const std::string model = _argv[2];
	if(model != "md" && model != "mm")
	{
		std::puts("mdmmSysexRoundTripTest: the machine is md or mm");
		std::puts("mdmmSysexRoundTripTest: FAIL (usage)");
		return 1;
	}
	Target target;
	if(model == "mm")
	{
		target.machine = md::MachineModel::Monomachine;
		target.syx = ed::SyxModel::Mm;
		target.name = "MM";
	}

	const auto wall = std::chrono::steady_clock::now();
	try
	{
		const std::string romName = _argv[1];
		const auto rom = load(romName);
		const auto romCheck = md::checkRom(rom, target.machine);
		std::printf("mdmmSysexRoundTripTest: rom=%s model=%s fingerprint=%016llx check=\"%s\"\n", romName.c_str(),
			model.c_str(), static_cast<unsigned long long>(md::romFingerprint(rom)), romCheck.text.c_str());
		if(!romCheck.ok)
		{
			std::puts("mdmmSysexRoundTripTest: FAIL (not the supported image for this model)");
			return 1;
		}

		Dump fresh;
		{
			auto a = boot(rom, romName, target);
			fresh = dumpAll(*a, target, "fresh machine");
		}
		{
			auto b = boot(rom, romName, target);
			import(*b, target, messages(fresh), "round trip import into a fresh machine");
			const auto back = dumpAll(*b, target, "round trip machine");
			compareBytes(fresh, back, "round trip into a fresh machine");
		}
		for(int i = 3; i < _argc; ++i)
			fixture(_argv[i], rom, romName, target, fresh);
		if(_argc <= 3)
			std::puts("no fixture given: the import is not shown to land over different content (step 4 needs one)");
	}
	catch(const std::exception& e)
	{
		std::printf("mdmmSysexRoundTripTest: %s\n", e.what());
		++g_failures;
	}
	std::printf("mdmmSysexRoundTripTest: %.0f s wall\n", wallSeconds(wall));
	if(g_failures)
	{
		std::printf("mdmmSysexRoundTripTest: FAIL (%d failure(s))\n", g_failures);
		return 1;
	}
	std::puts("mdmmSysexRoundTripTest: PASS");
	return 0;
}
