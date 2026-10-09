#pragma once

#include "mdGlobal.h"
#include "mdKit.h"
#include "mdPattern.h"
#include "mdSong.h"
#include "mmGlobal.h"
#include "mmKit.h"
#include "mmPattern.h"
#include "mmSong.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace elektronData
{
	// .syx files of Machinedrum or Monomachine user data (P7): parse into plain documents, summarise,
	// plan an import against what the machine holds, and write documents back. Data in, data out:
	// sending the plan to a machine is the caller's job.

	enum class SyxModel
	{
		Unknown,	// no recognised dump
		Md,			// F0 00 20 3C 02 00
		Mm,			// F0 00 20 3C 03 00
		Mixed		// both: the first recognised model wins, the other one's messages are problems
	};

	enum class SyxKind
	{
		Global,
		Kit,
		Pattern,
		Song,
		Other		// not a user-data dump
	};

	enum class SyxStatus
	{
		Ok,
		Truncated,		// F0 without F7 (the file ends or a new F0 starts first)
		OtherModel,		// not a dump of this file's model (another Elektron product, or any other SysEx)
		UnknownId,		// this model, but not a global, kit, pattern or song dump
		WrongLength,	// the trailer's length field, or a size the codec does not know
		BadChecksum,
		BadData,		// bytes above 0x7f, or a payload the codec rejects
		SlotOutOfRange,
		DuplicateSlot	// a later dump of the same kind and slot: it replaces the earlier one
	};

	struct SyxMessageReport
	{
		size_t index = 0;		// the message's place in the file, from 0
		size_t offset = 0;		// its F0 in the file
		size_t size = 0;		// F0..F7 inclusive
		SyxKind kind = SyxKind::Other;
		int slot = -1;			// the dump's position byte, -1 when there is none
		SyxStatus status = SyxStatus::Ok;
		SyxModel model = SyxModel::Unknown;	// the model its header names (Unknown: not an MD or MM message)
		int command = -1;		// the message id after the header, -1 when there is none
		int version = -1;		// a dump's format bytes (version, revision), -1 when there are none
		int revision = -1;
	};

	struct MdDocuments
	{
		std::map<uint8_t, MdGlobal> globals;
		std::map<uint8_t, MdKit> kits;
		std::map<uint8_t, MdPattern> patterns;
		std::map<uint8_t, MdSong> songs;
	};

	struct MmDocuments
	{
		std::map<uint8_t, MmGlobal> globals;
		std::map<uint8_t, MmKit> kits;
		std::map<uint8_t, MmPattern> patterns;
		std::map<uint8_t, MmSong> songs;
	};

	struct SyxFile
	{
		SyxModel model = SyxModel::Unknown;
		MdDocuments md;		// filled when the documents' model is the MD
		MmDocuments mm;		// filled when it is the MM
		std::vector<SyxMessageReport> messages;	// every message, in file order
		std::vector<SyxMessageReport> problems;	// the ones that are not Ok
	};

	SyxFile parseSyx(const std::vector<uint8_t>& _bytes);

	// The model the documents belong to: Md or Mm for a mixed file too, Unknown when there are none.
	SyxModel documentsModel(const SyxFile& _file);

	// Slots per kind: MD 8 / 64 / 128 / 32, MM 8 / 128 / 128 / 24; 0 for Unknown, Mixed or Other.
	size_t syxSlotCount(SyxModel _model, SyxKind _kind);

	// "A01".."H16" for patterns 0-127.
	std::string syxPatternLabel(uint8_t _slot);

	const char* syxKindName(SyxKind _kind);
	const char* syxStatusName(SyxStatus _status);
	const char* syxModelName(SyxModel _model);

	// ---- preview

	struct SyxNamedSlot
	{
		uint8_t slot = 0;
		std::string name;	// printable 7-bit ASCII up to the first NUL (or 0xff); may be empty
	};

	struct SyxPatternEntry
	{
		uint8_t slot = 0;
		std::string label;	// A01..H16
		uint8_t kit = 0;	// the kit the pattern plays
	};

	struct SyxSummary
	{
		SyxModel model = SyxModel::Unknown;
		size_t globals = 0;
		size_t kits = 0;
		size_t patterns = 0;
		size_t songs = 0;
		std::vector<SyxNamedSlot> kitNames;
		std::vector<SyxPatternEntry> patternList;
		std::vector<SyxNamedSlot> songNames;
		bool fullBackup = false;	// every slot of every kind is present
		size_t problems = 0;
	};

	SyxSummary summarizeSyx(const SyxFile& _file);

	// ---- import plan

	struct SyxItem
	{
		SyxKind kind = SyxKind::Other;
		uint8_t slot = 0;

		bool operator==(const SyxItem& _o) const { return kind == _o.kind && slot == _o.slot; }
		bool operator<(const SyxItem& _o) const
		{
			return kind != _o.kind ? kind < _o.kind : slot < _o.slot;
		}
	};

	// Every document of the file, in write order (globals, kits, patterns, songs; slots ascending).
	std::vector<SyxItem> syxItems(const SyxFile& _file);
	// The file's documents of the given kinds.
	std::vector<SyxItem> syxItems(const SyxFile& _file, const std::vector<SyxKind>& _kinds);

	struct SyxPlanItem
	{
		SyxKind kind = SyxKind::Other;
		uint8_t slot = 0;		// the target slot: the document's own slot
		bool overwrites = false;	// the machine's slot holds non-empty data
	};

	// The selected items the file holds, in write order; items the file does not hold are left out.
	// _machine is what the machine holds; a slot missing from it counts as empty.
	std::vector<SyxPlanItem> planSyxImport(const SyxFile& _file, const std::vector<SyxItem>& _selection,
		const MdDocuments& _machine);
	std::vector<SyxPlanItem> planSyxImport(const SyxFile& _file, const std::vector<SyxItem>& _selection,
		const MmDocuments& _machine);

	// Empty = nothing a user would lose. Globals are never empty.
	bool isEmpty(const MdKit& _kit);
	bool isEmpty(const MdPattern& _pattern);
	bool isEmpty(const MdSong& _song);
	bool isEmpty(const MmKit& _kit);
	bool isEmpty(const MmPattern& _pattern);
	bool isEmpty(const MmSong& _song);

	// ---- export

	// One .syx stream: globals, kits, patterns, songs, slots ascending, each with the codec's encoder.
	std::vector<uint8_t> writeSyx(const MdDocuments& _documents);
	std::vector<uint8_t> writeSyx(const MmDocuments& _documents);
	// The file's documents in its model; empty for Unknown.
	std::vector<uint8_t> writeSyx(const SyxFile& _file);

	// One document as a message, e.g. to send a plan item; empty when the file does not hold it.
	std::vector<uint8_t> syxMessage(const SyxFile& _file, const SyxItem& _item);

	// ---- as-is import (B-019)
	//
	// An import is a MIDI cable into the machine: the file's messages go to it unchanged, in file order, and
	// the firmware decides what it takes. The editor only reads back what the machine holds afterwards and
	// says, per document, what happened. What cannot be sent at all is left out up front, with the reason.

	// Elektron's OS update messages (F0 00 20 3C <product> 00 7E ... and the closing 7F): firmware, not user data.
	constexpr uint8_t g_syxOsPacket = 0x7e;
	constexpr uint8_t g_syxOsEnd = 0x7f;

	// "" when the message can go to a machine of _model as it is; else why not: not SysEx of this machine
	// (another model, another maker, not SysEx), broken framing (no F7, a data byte above 7F), or part of an OS
	// update (the editor never sends firmware).
	std::string syxUnsendable(const SyxMessageReport& _message, const std::vector<uint8_t>& _file, SyxModel _model);
	// The message's own bytes, F0..F7 (or what there is of a truncated one).
	std::vector<uint8_t> syxBytes(const std::vector<uint8_t>& _file, const SyxMessageReport& _message);

	// The dump request for one document of _model (the firmware answers with its dump); empty for Other.
	std::vector<uint8_t> syxRequest(SyxModel _model, SyxKind _kind, uint8_t _slot);

	// A dump of _model as the editor's codec reads it, in the form two dumps are compared in: the contract's
	// document without its "firmware" group (format bytes, residue, undecoded bytes, which a machine rewrites as
	// it stores a dump), as JSON text. Empty when the codec cannot read it.
	struct SyxCanonical
	{
		SyxKind kind = SyxKind::Other;
		uint8_t slot = 0;
		std::vector<uint8_t> bytes;
	};
	std::optional<SyxCanonical> syxCanonical(SyxModel _model, const std::vector<uint8_t>& _message);
	// The same form for documents the editor holds; empty when _docs has none in that slot.
	std::vector<uint8_t> syxCanonical(const MdDocuments& _docs, SyxKind _kind, uint8_t _slot);
	std::vector<uint8_t> syxCanonical(const MmDocuments& _docs, SyxKind _kind, uint8_t _slot);

	// Whether the slot holds something a user would lose (globals always do; a slot not known counts as empty).
	bool syxHoldsData(const MdDocuments& _docs, SyxKind _kind, uint8_t _slot);
	bool syxHoldsData(const MmDocuments& _docs, SyxKind _kind, uint8_t _slot);

	// What the machine did with one document of the file, from its read-back.
	enum class SyxOutcome
	{
		Taken,		// it holds the file's document
		Converted,	// it holds something new that is not the file's document (its own format of it)
		Ignored,	// it holds what it held before
		Differs,	// it holds something else than the file's document; what it held before is not known
		Changed,	// it holds something new; the editor cannot read the file's dump to compare
		Unknown,	// neither the file's dump nor what it held before can be compared
		NoReply		// it did not answer the read-back
	};
	// _file: the file's dump (canonical, empty: not readable); _before: what the machine held (nullopt: not
	// known); _after: its read-back (nullopt: no reply).
	SyxOutcome syxOutcome(const std::vector<uint8_t>& _file, const std::optional<std::vector<uint8_t>>& _before,
		const std::optional<std::vector<uint8_t>>& _after);
	const char* syxOutcomeName(SyxOutcome _outcome);

	// B-026: what an imported global changed (its canonical before and read-back), in words for the report: "MIDI
	// base channel 1-4 -> OFF", then the other settings by name. Empty when nothing changed or either is not known.
	std::vector<std::string> syxGlobalChanges(SyxModel _model, const std::optional<std::vector<uint8_t>>& _before,
		const std::optional<std::vector<uint8_t>>& _after);
}
