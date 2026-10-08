#include "syxImport.h"

#include "dumpIo.h"
#include "mdCommands.h"
#include "mdJson.h"
#include "mmJson.h"
#include "mmDump.h"
#include "sysex7bit.h"

#include <algorithm>

namespace elektronData
{
	namespace
	{
		using Bytes = std::vector<uint8_t>;

		struct Span
		{
			size_t offset;
			size_t size;
			bool complete;
		};

		// F0..F7 spans. An F0 that meets another F0 or the end of the file first is incomplete.
		std::vector<Span> splitSpans(const Bytes& _bytes)
		{
			std::vector<Span> spans;
			size_t i = 0;
			while(i < _bytes.size())
			{
				if(_bytes[i] != 0xf0)
				{
					++i;
					continue;
				}
				size_t end = i + 1;
				while(end < _bytes.size() && _bytes[end] != 0xf7 && _bytes[end] != 0xf0)
					++end;
				if(end < _bytes.size() && _bytes[end] == 0xf7)
				{
					spans.push_back({i, end + 1 - i, true});
					i = end + 1;
				}
				else
				{
					spans.push_back({i, end - i, false});
					i = end;
				}
			}
			return spans;
		}

		// The model an Elektron header names: F0 00 20 3C <product> 00.
		SyxModel headerModel(const Bytes& _m)
		{
			if(_m.size() < 7 || _m[1] != 0x00 || _m[2] != 0x20 || _m[3] != 0x3c || _m[5] != 0x00)
				return SyxModel::Unknown;
			if(_m[4] == dumpIo::g_mdProductId)
				return SyxModel::Md;
			if(_m[4] == g_mmProductId)
				return SyxModel::Mm;
			return SyxModel::Unknown;
		}

		SyxKind kindOf(const uint8_t _command)
		{
			// MD and MM share the dump ids.
			switch(_command)
			{
			case g_mdGlobalDump: return SyxKind::Global;
			case g_mdKitDump: return SyxKind::Kit;
			case g_mdPatternDump: return SyxKind::Pattern;
			case g_mdSongDump: return SyxKind::Song;
			default: return SyxKind::Other;
			}
		}

		// The framing checks every dump shares, before a codec sees it.
		SyxStatus frameStatus(const Bytes& _m)
		{
			if(_m.size() < 15)
				return SyxStatus::WrongLength;
			for(size_t i = 1; i + 1 < _m.size(); ++i)
				if(_m[i] > 0x7f)
					return SyxStatus::BadData;
			const auto n = _m.size();
			if(uint32_t((_m[n - 3] << 7) | _m[n - 2]) != uint32_t(n - 10))
				return SyxStatus::WrongLength;
			if(!isDumpTrailerValid(_m))
				return SyxStatus::BadChecksum;
			return SyxStatus::Ok;
		}

		template<typename T>
		SyxStatus store(std::map<uint8_t, T>& _map, const uint8_t _slot, T&& _value)
		{
			const bool duplicate = _map.count(_slot) != 0;
			_map[_slot] = std::move(_value);
			return duplicate ? SyxStatus::DuplicateSlot : SyxStatus::Ok;
		}

		template<typename T>
		SyxStatus storeDecoded(std::map<uint8_t, T>& _map, const uint8_t _slot, std::optional<T>&& _value,
			const SyxStatus _failure)
		{
			if(!_value)
				return _failure;
			return store(_map, _slot, std::move(*_value));
		}

		SyxStatus decodeMd(MdDocuments& _docs, const Bytes& _m, const SyxKind _kind, const uint8_t _slot)
		{
			const auto size = _m.size();
			switch(_kind)
			{
			case SyxKind::Global:
				return storeDecoded(_docs.globals, _slot, decodeMdGlobal(_m),
					size != MdGlobal::g_dumpSize ? SyxStatus::WrongLength : SyxStatus::BadData);
			case SyxKind::Kit:
				return storeDecoded(_docs.kits, _slot, decodeMdKit(_m),
					size != MdKit::g_dumpSize ? SyxStatus::WrongLength : SyxStatus::BadData);
			case SyxKind::Pattern:
				return storeDecoded(_docs.patterns, _slot, decodeMdPattern(_m),
					size != MdPattern::g_shortDumpSize && size != MdPattern::g_extendedDumpSize
						? SyxStatus::WrongLength : SyxStatus::BadData);
			case SyxKind::Song:
				return storeDecoded(_docs.songs, _slot, decodeMdSong(_m), SyxStatus::WrongLength);
			default:
				return SyxStatus::UnknownId;
			}
		}

		size_t mmRawSize(const SyxKind _kind)
		{
			switch(_kind)
			{
			case SyxKind::Global: return MmGlobal::g_rawSize;
			case SyxKind::Kit: return MmKit::g_rawSize;
			case SyxKind::Pattern: return MmPattern::g_rawSize;
			case SyxKind::Song: return MmSong::g_rawSize;
			default: return 0;
			}
		}

		SyxStatus decodeMm(MmDocuments& _docs, const Bytes& _m, const SyxKind _kind, const uint8_t _slot)
		{
			const auto dump = unpackMmDump(_m);
			if(!dump)
				return SyxStatus::BadData;
			if(dump->raw.size() != mmRawSize(_kind))
				return SyxStatus::WrongLength;
			switch(_kind)
			{
			case SyxKind::Global: return storeDecoded(_docs.globals, _slot, decodeMmGlobal(_m), SyxStatus::BadData);
			case SyxKind::Kit: return storeDecoded(_docs.kits, _slot, decodeMmKit(_m), SyxStatus::BadData);
			case SyxKind::Pattern: return storeDecoded(_docs.patterns, _slot, decodeMmPattern(_m), SyxStatus::BadData);
			case SyxKind::Song: return storeDecoded(_docs.songs, _slot, decodeMmSong(_m), SyxStatus::BadData);
			default: return SyxStatus::UnknownId;
			}
		}

		template<size_t N>
		std::string nameOf(const std::array<uint8_t, N>& _name)
		{
			std::string s;
			for(const auto c : _name)
			{
				if(c == 0 || c == 0xff)
					break;
				if(c >= 0x20 && c < 0x7f)
					s.push_back(static_cast<char>(c));
			}
			return s;
		}

		template<typename Docs>
		void addItems(std::vector<SyxItem>& _items, const Docs& _docs, const SyxKind _kind)
		{
			auto add = [&](const auto& _map)
			{
				for(const auto& [slot, doc] : _map)
					_items.push_back({_kind, slot});
			};
			switch(_kind)
			{
			case SyxKind::Global: add(_docs.globals); break;
			case SyxKind::Kit: add(_docs.kits); break;
			case SyxKind::Pattern: add(_docs.patterns); break;
			case SyxKind::Song: add(_docs.songs); break;
			default: break;
			}
		}

		template<typename Docs>
		std::vector<SyxItem> itemsOf(const Docs& _docs, const std::vector<SyxKind>& _kinds)
		{
			std::vector<SyxItem> items;
			for(const auto kind : {SyxKind::Global, SyxKind::Kit, SyxKind::Pattern, SyxKind::Song})
				if(std::find(_kinds.begin(), _kinds.end(), kind) != _kinds.end())
					addItems(items, _docs, kind);
			return items;
		}

		// Globals are never empty.
		template<typename T>
		bool isEmptyDocument(const T& _doc) { return isEmpty(_doc); }
		bool isEmptyDocument(const MdGlobal&) { return false; }
		bool isEmptyDocument(const MmGlobal&) { return false; }

		template<typename T>
		bool holdsData(const std::map<uint8_t, T>& _map, const uint8_t _slot)
		{
			const auto it = _map.find(_slot);
			return it != _map.end() && !isEmptyDocument(it->second);
		}

		template<typename Docs>
		bool holds(const Docs& _docs, const SyxItem& _item)
		{
			switch(_item.kind)
			{
			case SyxKind::Global: return _docs.globals.count(_item.slot) != 0;
			case SyxKind::Kit: return _docs.kits.count(_item.slot) != 0;
			case SyxKind::Pattern: return _docs.patterns.count(_item.slot) != 0;
			case SyxKind::Song: return _docs.songs.count(_item.slot) != 0;
			default: return false;
			}
		}

		template<typename Docs>
		bool overwrites(const Docs& _machine, const SyxItem& _item)
		{
			switch(_item.kind)
			{
			case SyxKind::Global: return holdsData(_machine.globals, _item.slot);
			case SyxKind::Kit: return holdsData(_machine.kits, _item.slot);
			case SyxKind::Pattern: return holdsData(_machine.patterns, _item.slot);
			case SyxKind::Song: return holdsData(_machine.songs, _item.slot);
			default: return false;
			}
		}

		template<typename Docs>
		std::vector<SyxPlanItem> plan(const Docs& _file, const std::vector<SyxItem>& _selection, const Docs& _machine)
		{
			auto selection = _selection;
			std::sort(selection.begin(), selection.end());
			selection.erase(std::unique(selection.begin(), selection.end()), selection.end());

			std::vector<SyxPlanItem> items;
			for(const auto& item : selection)
				if(holds(_file, item))
					items.push_back({item.kind, item.slot, overwrites(_machine, item)});
			return items;
		}

		template<typename Map, typename Encode>
		void append(Bytes& _out, const Map& _map, Encode _encode)
		{
			for(const auto& [slot, doc] : _map)
			{
				const auto m = _encode(doc);
				_out.insert(_out.end(), m.begin(), m.end());
			}
		}

		template<typename Map, typename Encode>
		Bytes encodeOne(const Map& _map, const uint8_t _slot, Encode _encode)
		{
			const auto it = _map.find(_slot);
			return it == _map.end() ? Bytes{} : _encode(it->second);
		}
	}

	namespace
	{
		// Checks one complete message and, when it is a good dump of the file's model, stores its document.
		SyxStatus readMessage(SyxFile& _file, const SyxModel _model, const Bytes& _m, const SyxKind _kind)
		{
			if(_model == SyxModel::Unknown || headerModel(_m) != _model)
				return SyxStatus::OtherModel;
			if(_kind == SyxKind::Other)
				return SyxStatus::UnknownId;
			const auto frame = frameStatus(_m);
			if(frame != SyxStatus::Ok)
				return frame;
			const auto slot = _m[9];
			if(slot >= syxSlotCount(_model, _kind))
				return SyxStatus::SlotOutOfRange;
			return _model == SyxModel::Md ? decodeMd(_file.md, _m, _kind, slot) : decodeMm(_file.mm, _m, _kind, slot);
		}
	}

	SyxFile parseSyx(const std::vector<uint8_t>& _bytes)
	{
		SyxFile file;
		const auto spans = splitSpans(_bytes);

		// The file's model: the first message with an MD or MM header.
		SyxModel model = SyxModel::Unknown;
		bool mixed = false;
		for(const auto& span : spans)
		{
			const Bytes head(_bytes.begin() + static_cast<std::ptrdiff_t>(span.offset),
				_bytes.begin() + static_cast<std::ptrdiff_t>(span.offset + std::min<size_t>(span.size, 7)));
			const auto m = headerModel(head);
			if(m == SyxModel::Unknown)
				continue;
			if(model == SyxModel::Unknown)
				model = m;
			else if(m != model)
				mixed = true;
		}
		file.model = mixed ? SyxModel::Mixed : model;

		for(size_t i = 0; i < spans.size(); ++i)
		{
			const auto& span = spans[i];
			const Bytes m(_bytes.begin() + static_cast<std::ptrdiff_t>(span.offset),
				_bytes.begin() + static_cast<std::ptrdiff_t>(span.offset + span.size));

			SyxMessageReport r;
			r.index = i;
			r.offset = span.offset;
			r.size = span.size;
			r.model = headerModel(m);
			if(m.size() > 6 && r.model != SyxModel::Unknown)
			{
				r.kind = kindOf(m[6]);
				r.command = m[6];
			}
			if(m.size() > 9 && r.kind != SyxKind::Other)
			{
				r.slot = m[9];
				r.version = m[7];
				r.revision = m[8];
			}

			r.status = span.complete ? readMessage(file, model, m, r.kind) : SyxStatus::Truncated;

			file.messages.push_back(r);
			if(r.status != SyxStatus::Ok)
				file.problems.push_back(r);
		}
		return file;
	}

	SyxModel documentsModel(const SyxFile& _file)
	{
		const auto& md = _file.md;
		const auto& mm = _file.mm;
		if(!md.globals.empty() || !md.kits.empty() || !md.patterns.empty() || !md.songs.empty())
			return SyxModel::Md;
		if(!mm.globals.empty() || !mm.kits.empty() || !mm.patterns.empty() || !mm.songs.empty())
			return SyxModel::Mm;
		return SyxModel::Unknown;
	}

	size_t syxSlotCount(const SyxModel _model, const SyxKind _kind)
	{
		if(_model == SyxModel::Md)
		{
			switch(_kind)
			{
			case SyxKind::Global: return MdGlobal::g_slots;
			case SyxKind::Kit: return MdKit::g_slots;
			case SyxKind::Pattern: return 128;
			case SyxKind::Song: return MdSong::g_slots;
			default: return 0;
			}
		}
		if(_model == SyxModel::Mm)
		{
			switch(_kind)
			{
			case SyxKind::Global: return MmGlobal::g_slots;
			case SyxKind::Kit: return MmKit::g_slots;
			case SyxKind::Pattern: return MmPattern::g_slots;
			case SyxKind::Song: return MmSong::g_slots;
			default: return 0;
			}
		}
		return 0;
	}

	std::string syxPatternLabel(const uint8_t _slot)
	{
		const int number = _slot % 16 + 1;
		std::string s(1, static_cast<char>('A' + (_slot / 16) % 8));
		s.push_back(static_cast<char>('0' + number / 10));
		s.push_back(static_cast<char>('0' + number % 10));
		return s;
	}

	const char* syxKindName(const SyxKind _kind)
	{
		switch(_kind)
		{
		case SyxKind::Global: return "global";
		case SyxKind::Kit: return "kit";
		case SyxKind::Pattern: return "pattern";
		case SyxKind::Song: return "song";
		default: return "other";
		}
	}

	const char* syxStatusName(const SyxStatus _status)
	{
		switch(_status)
		{
		case SyxStatus::Ok: return "ok";
		case SyxStatus::Truncated: return "truncated";
		case SyxStatus::OtherModel: return "other model";
		case SyxStatus::UnknownId: return "unknown id";
		case SyxStatus::WrongLength: return "wrong length";
		case SyxStatus::BadChecksum: return "bad checksum";
		case SyxStatus::BadData: return "bad data";
		case SyxStatus::SlotOutOfRange: return "slot out of range";
		case SyxStatus::DuplicateSlot: return "duplicate slot";
		}
		return "?";
	}

	const char* syxModelName(const SyxModel _model)
	{
		switch(_model)
		{
		case SyxModel::Md: return "Machinedrum";
		case SyxModel::Mm: return "Monomachine";
		case SyxModel::Mixed: return "mixed";
		default: return "unknown";
		}
	}

	SyxSummary summarizeSyx(const SyxFile& _file)
	{
		SyxSummary s;
		s.model = _file.model;
		s.problems = _file.problems.size();
		const auto model = documentsModel(_file);

		auto fill = [&](const auto& _docs)
		{
			s.globals = _docs.globals.size();
			s.kits = _docs.kits.size();
			s.patterns = _docs.patterns.size();
			s.songs = _docs.songs.size();
			for(const auto& [slot, kit] : _docs.kits)
				s.kitNames.push_back({slot, nameOf(kit.name)});
			for(const auto& [slot, pattern] : _docs.patterns)
				s.patternList.push_back({slot, syxPatternLabel(slot), pattern.kit});
			for(const auto& [slot, song] : _docs.songs)
				s.songNames.push_back({slot, nameOf(song.name)});
		};
		if(model == SyxModel::Md)
			fill(_file.md);
		else if(model == SyxModel::Mm)
			fill(_file.mm);

		s.fullBackup = model != SyxModel::Unknown
			&& s.globals == syxSlotCount(model, SyxKind::Global)
			&& s.kits == syxSlotCount(model, SyxKind::Kit)
			&& s.patterns == syxSlotCount(model, SyxKind::Pattern)
			&& s.songs == syxSlotCount(model, SyxKind::Song);
		return s;
	}

	std::vector<SyxItem> syxItems(const SyxFile& _file)
	{
		return syxItems(_file, {SyxKind::Global, SyxKind::Kit, SyxKind::Pattern, SyxKind::Song});
	}

	std::vector<SyxItem> syxItems(const SyxFile& _file, const std::vector<SyxKind>& _kinds)
	{
		switch(documentsModel(_file))
		{
		case SyxModel::Md: return itemsOf(_file.md, _kinds);
		case SyxModel::Mm: return itemsOf(_file.mm, _kinds);
		default: return {};
		}
	}

	std::vector<SyxPlanItem> planSyxImport(const SyxFile& _file, const std::vector<SyxItem>& _selection,
		const MdDocuments& _machine)
	{
		return plan(_file.md, _selection, _machine);
	}

	std::vector<SyxPlanItem> planSyxImport(const SyxFile& _file, const std::vector<SyxItem>& _selection,
		const MmDocuments& _machine)
	{
		return plan(_file.mm, _selection, _machine);
	}

	bool isEmpty(const MdKit& _kit)
	{
		return _kit.name[0] == 0
			&& std::all_of(_kit.models.begin(), _kit.models.end(), [](const uint32_t _m) { return _m == 0; });
	}

	bool isEmpty(const MdPattern& _pattern)
	{
		return std::all_of(_pattern.trigs.begin(), _pattern.trigs.end(), [](const uint64_t _t) { return _t == 0; })
			&& std::all_of(_pattern.lockMasks.begin(), _pattern.lockMasks.end(), [](const uint32_t _l) { return _l == 0; });
	}

	bool isEmpty(const MdSong& _song)
	{
		return _song.rows.empty() || _song.rows.front().pattern == MdSongRow::g_endRow;
	}

	bool isEmpty(const MmKit& _kit)
	{
		return (_kit.name[0] == 0 || _kit.name[0] == 0xff)
			&& std::all_of(_kit.machines.begin(), _kit.machines.end(), [](const uint8_t _m) { return _m == 0; });
	}

	bool isEmpty(const MmPattern& _pattern)
	{
		for(const auto* masks : {&_pattern.amp, &_pattern.filter, &_pattern.lfo, &_pattern.noteOff, &_pattern.midiTrig,
			&_pattern.midiNoteOff})
			for(const auto m : *masks)
				if(m)
					return false;
		return _pattern.lockRowCount == 0;
	}

	bool isEmpty(const MmSong& _song)
	{
		return _song.rows.front().pattern() == MmSong::g_end;
	}

	std::vector<uint8_t> writeSyx(const MdDocuments& _documents)
	{
		Bytes out;
		append(out, _documents.globals, encodeMdGlobal);
		append(out, _documents.kits, encodeMdKit);
		append(out, _documents.patterns, encodeMdPattern);
		append(out, _documents.songs, encodeMdSong);
		return out;
	}

	std::vector<uint8_t> writeSyx(const MmDocuments& _documents)
	{
		Bytes out;
		append(out, _documents.globals, encodeMmGlobal);
		append(out, _documents.kits, encodeMmKit);
		append(out, _documents.patterns, encodeMmPattern);
		append(out, _documents.songs, encodeMmSong);
		return out;
	}

	std::vector<uint8_t> writeSyx(const SyxFile& _file)
	{
		switch(documentsModel(_file))
		{
		case SyxModel::Md: return writeSyx(_file.md);
		case SyxModel::Mm: return writeSyx(_file.mm);
		default: return {};
		}
	}

	// ---- as-is import (B-019)

	std::string syxUnsendable(const SyxMessageReport& _message, const std::vector<uint8_t>& _file, const SyxModel _model)
	{
		const auto bytes = syxBytes(_file, _message);
		if(_message.status == SyxStatus::Truncated || bytes.size() < 2 || bytes.back() != 0xf7)
			return "broken: it has no end (no F7), the file is cut or damaged";
		if(_message.model == SyxModel::Unknown)
			return bytes.size() > 4 && bytes[1] == 0x00 && bytes[2] == 0x20 && bytes[3] == 0x3c
				? "SysEx of another Elektron machine" : bytes.size() > 1 && bytes[1] == 0x7e
				? "a universal SysEx message (a sample dump or a device inquiry), not data of this machine"
				: "SysEx of another maker's device";
		if(_message.model != _model)
			return std::string("SysEx of the ") + syxModelName(_message.model) + ", not of the " + syxModelName(_model);
		for(size_t i = 1; i + 1 < bytes.size(); ++i)
			if(bytes[i] > 0x7f)
				return "broken: a byte above 7F inside the message";
		// Elektron's OS update files are 0x7E packets and a closing 0x7F (Elektron_SPS1-1UW_OS1.63.syx,
		// Elektron_SFX6-60_OS1.32B.syx): firmware, not user data. Sent to a machine on its OS upgrade screen they
		// rewrite its flash; the emulated machine runs the ROM it was given. The editor never sends them.
		if(_message.command == g_syxOsPacket || _message.command == g_syxOsEnd)
			return "part of an OS update (the machine's firmware), not user data: an import never sends it";
		return {};
	}

	std::vector<uint8_t> syxBytes(const std::vector<uint8_t>& _file, const SyxMessageReport& _message)
	{
		const auto from = std::min(_message.offset, _file.size());
		const auto to = std::min(_message.offset + _message.size, _file.size());
		return {_file.begin() + static_cast<std::ptrdiff_t>(from), _file.begin() + static_cast<std::ptrdiff_t>(to)};
	}

	std::vector<uint8_t> syxRequest(const SyxModel _model, const SyxKind _kind, const uint8_t _slot)
	{
		if(_model == SyxModel::Md)
		{
			switch(_kind)
			{
			case SyxKind::Global: return mdGlobalRequest(_slot);
			case SyxKind::Kit: return mdKitRequest(_slot);
			case SyxKind::Pattern: return mdPatternRequest(_slot);
			case SyxKind::Song: return mdSongRequest(_slot);
			default: return {};
			}
		}
		if(_model == SyxModel::Mm)
		{
			switch(_kind)
			{
			case SyxKind::Global: return mmGlobalRequest(_slot);
			case SyxKind::Kit: return mmKitRequest(_slot);
			case SyxKind::Pattern: return mmPatternRequest(_slot);
			case SyxKind::Song: return mmSongRequest(_slot);
			default: return {};
			}
		}
		return {};
	}

	namespace
	{
		json::Value jsonOf(const MdGlobal& _d) { return globalToJson(_d); }
		json::Value jsonOf(const MdKit& _d) { return kitToJson(_d); }
		json::Value jsonOf(const MdPattern& _d) { return patternToJson(_d); }
		json::Value jsonOf(const MdSong& _d) { return songToJson(_d); }
		json::Value jsonOf(const MmGlobal& _d) { return mmGlobalToJson(_d); }
		json::Value jsonOf(const MmKit& _d) { return mmKitToJson(_d); }
		json::Value jsonOf(const MmPattern& _d) { return mmPatternToJson(_d); }
		json::Value jsonOf(const MmSong& _d) { return mmSongToJson(_d); }

		// What a person edits: the contract's document without its "firmware" group (the format bytes, residue
		// past the counts, undecoded bytes, the LFO's running state), which a machine rewrites as it stores a dump.
		template<typename T>
		Bytes visible(const T& _doc)
		{
			auto v = jsonOf(_doc);
			if(v.isObject())
			{
				auto& o = v.asObject();
				o.erase(std::remove_if(o.begin(), o.end(), [](const json::Value::Member& _m) { return _m.first == "firmware"; }), o.end());
			}
			const auto s = json::write(v);
			return {s.begin(), s.end()};
		}

		template<typename T, typename Decode>
		std::optional<SyxCanonical> canonicalOf(const SyxKind _kind, const Bytes& _m, Decode _decode)
		{
			const auto doc = _decode(_m);
			if(!doc)
				return std::nullopt;
			return SyxCanonical{_kind, _m[9], visible(*doc)};
		}

		template<typename Map>
		Bytes canonicalIn(const Map& _map, const uint8_t _slot)
		{
			const auto it = _map.find(_slot);
			return it == _map.end() ? Bytes{} : visible(it->second);
		}
	}

	std::optional<SyxCanonical> syxCanonical(const SyxModel _model, const std::vector<uint8_t>& _message)
	{
		if(_message.size() < 15 || headerModel(_message) != _model)
			return std::nullopt;
		const auto kind = kindOf(_message[6]);
		if(_model == SyxModel::Md)
		{
			switch(kind)
			{
			case SyxKind::Global: return canonicalOf<MdGlobal>(kind, _message, decodeMdGlobal);
			case SyxKind::Kit: return canonicalOf<MdKit>(kind, _message, decodeMdKit);
			case SyxKind::Pattern: return canonicalOf<MdPattern>(kind, _message, decodeMdPattern);
			case SyxKind::Song: return canonicalOf<MdSong>(kind, _message, decodeMdSong);
			default: return std::nullopt;
			}
		}
		if(_model == SyxModel::Mm)
		{
			switch(kind)
			{
			case SyxKind::Global: return canonicalOf<MmGlobal>(kind, _message, decodeMmGlobal);
			case SyxKind::Kit: return canonicalOf<MmKit>(kind, _message, decodeMmKit);
			case SyxKind::Pattern: return canonicalOf<MmPattern>(kind, _message, decodeMmPattern);
			case SyxKind::Song: return canonicalOf<MmSong>(kind, _message, decodeMmSong);
			default: return std::nullopt;
			}
		}
		return std::nullopt;
	}

	std::vector<uint8_t> syxCanonical(const MdDocuments& _docs, const SyxKind _kind, const uint8_t _slot)
	{
		switch(_kind)
		{
		case SyxKind::Global: return canonicalIn(_docs.globals, _slot);
		case SyxKind::Kit: return canonicalIn(_docs.kits, _slot);
		case SyxKind::Pattern: return canonicalIn(_docs.patterns, _slot);
		case SyxKind::Song: return canonicalIn(_docs.songs, _slot);
		default: return {};
		}
	}

	std::vector<uint8_t> syxCanonical(const MmDocuments& _docs, const SyxKind _kind, const uint8_t _slot)
	{
		switch(_kind)
		{
		case SyxKind::Global: return canonicalIn(_docs.globals, _slot);
		case SyxKind::Kit: return canonicalIn(_docs.kits, _slot);
		case SyxKind::Pattern: return canonicalIn(_docs.patterns, _slot);
		case SyxKind::Song: return canonicalIn(_docs.songs, _slot);
		default: return {};
		}
	}

	bool syxHoldsData(const MdDocuments& _docs, const SyxKind _kind, const uint8_t _slot)
	{
		return _kind == SyxKind::Global || overwrites(_docs, SyxItem{_kind, _slot});
	}

	bool syxHoldsData(const MmDocuments& _docs, const SyxKind _kind, const uint8_t _slot)
	{
		return _kind == SyxKind::Global || overwrites(_docs, SyxItem{_kind, _slot});
	}

	SyxOutcome syxOutcome(const std::vector<uint8_t>& _file, const std::optional<std::vector<uint8_t>>& _before,
		const std::optional<std::vector<uint8_t>>& _after)
	{
		if(!_after)
			return SyxOutcome::NoReply;
		if(!_file.empty() && *_after == _file)
			return SyxOutcome::Taken;
		if(_before && !_before->empty())
		{
			if(*_after == *_before)
				return SyxOutcome::Ignored;
			return _file.empty() ? SyxOutcome::Changed : SyxOutcome::Converted;
		}
		return _file.empty() ? SyxOutcome::Unknown : SyxOutcome::Differs;
	}

	const char* syxOutcomeName(const SyxOutcome _outcome)
	{
		switch(_outcome)
		{
		case SyxOutcome::Taken: return "taken";
		case SyxOutcome::Converted: return "converted";
		case SyxOutcome::Ignored: return "ignored";
		case SyxOutcome::Differs: return "differs";
		case SyxOutcome::Changed: return "changed";
		case SyxOutcome::Unknown: return "unknown";
		case SyxOutcome::NoReply: return "no reply";
		}
		return "?";
	}

	std::vector<uint8_t> syxMessage(const SyxFile& _file, const SyxItem& _item)
	{
		const auto model = documentsModel(_file);
		if(model == SyxModel::Md)
		{
			const auto& d = _file.md;
			switch(_item.kind)
			{
			case SyxKind::Global: return encodeOne(d.globals, _item.slot, encodeMdGlobal);
			case SyxKind::Kit: return encodeOne(d.kits, _item.slot, encodeMdKit);
			case SyxKind::Pattern: return encodeOne(d.patterns, _item.slot, encodeMdPattern);
			case SyxKind::Song: return encodeOne(d.songs, _item.slot, encodeMdSong);
			default: return {};
			}
		}
		if(model == SyxModel::Mm)
		{
			const auto& d = _file.mm;
			switch(_item.kind)
			{
			case SyxKind::Global: return encodeOne(d.globals, _item.slot, encodeMmGlobal);
			case SyxKind::Kit: return encodeOne(d.kits, _item.slot, encodeMmKit);
			case SyxKind::Pattern: return encodeOne(d.patterns, _item.slot, encodeMmPattern);
			case SyxKind::Song: return encodeOne(d.songs, _item.slot, encodeMmSong);
			default: return {};
			}
		}
		return {};
	}
}
