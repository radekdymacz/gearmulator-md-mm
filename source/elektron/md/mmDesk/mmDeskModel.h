#pragma once

#include "deskCore/deskCommands.h"
#include "deskCore/deskCore.h"

namespace deskCore
{
	template<typename Model> struct KindSpec;
}
#include "deskCore/deskHistory.h"
#include "deskCore/deskRef.h"

#include "elektronData/json.h"
#include "elektronData/mmGlobal.h"
#include "elektronData/mmKit.h"
#include "elektronData/mmPattern.h"
#include "elektronData/mmSong.h"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace mmDesk
{
	// The Monomachine model for deskCore::Core (P6): the same pipeline as the Machinedrum's.
	// Its page sends edit intents (DESIGN-UNIFY.md: every gesture, mmDeskEdit.cpp); a whole document
	// ({"op":"set","kind","doc","g"}; setDocument is the pure transform "replace it with this validated value") is
	// the intent of an import or a restore only; undo is the core's, one step per gesture (g).
	enum class Kind : uint8_t
	{
		Pattern,
		Kit,
		Song,
		Global,
		WorkingKit
	};

	using Ref = deskCore::Ref<Kind>;

	// The kit that plays, as its own document (P6): the machine's working copy of the kit it
	// loaded from slot kit.position. One identity (slot 0 of its kind); Kind::Kit documents are the
	// stored slots only.
	struct WorkingKit
	{
		elektronData::MmKit kit;

		bool operator==(const WorkingKit& _o) const { return kit == _o.kit; }
	};

	using Document = std::variant<elektronData::MmPattern, elektronData::MmKit, elektronData::MmSong, elektronData::MmGlobal,
		WorkingKit>;

	Ref refOf(const Document& _doc);
	const char* kindName(Kind _k);
	std::optional<Kind> kindFromName(const std::string& _name);
	elektronData::json::Value documentToJson(const Document& _doc);

	// Every document the desk shows: the stored kit slots, and the kit that plays as the working kit.
	struct Documents
	{
		std::map<uint8_t, elektronData::MmPattern> patterns;
		std::map<uint8_t, elektronData::MmKit> kits;		// stored slots
		std::map<uint8_t, elektronData::MmSong> songs;
		std::map<uint8_t, elektronData::MmGlobal> globals;
		std::optional<WorkingKit> working;

		// The working kit when it is the one loaded from slot _kit.
		const elektronData::MmKit* workingKitOf(const int _kit) const
		{
			return working && working->kit.position == _kit ? &working->kit : nullptr;
		}

		std::optional<Document> get(const Ref& _ref) const;
		void set(const Document& _doc);
		void erase(const Ref& _ref);
	};

	struct Change
	{
		// A kit change is a stored-slot dump; a working-kit change a live edit of the kit that plays.
		Document before;
		Document after;

		Ref ref() const { return refOf(after); }
	};

	// What a step of a track holds, as the page shows it (DESIGN-UNIFY.md 4.1, the step intent's value): empty, a
	// NOTE OFF, or a trig with its envelope trigs (a, f, l), its pitch or chord (notes, the base note first; none:
	// pitchless) and whether it carries the trig bit (an envelope-only step, TRIG SELECT, has none).
	struct StepValue
	{
		enum class Kind : uint8_t { Empty, Off, On };
		Kind kind = Kind::Empty;
		bool trig = true;
		bool a = false, f = false, l = false;
		std::vector<uint8_t> notes;

		bool operator==(const StepValue& _o) const
		{
			return kind == _o.kind && (kind != Kind::On || (trig == _o.trig && a == _o.a && f == _o.f && l == _o.l && notes == _o.notes));
		}
	};

	// What copy puts aside for paste: a track page (copySteps, pasteSteps), a track's sound (copySound, pasteSound), a
	// song row (copyRow, pasteRow), a stored kit or pattern (the library's kitCopy, patCopy). Values only; nothing
	// refers back.
	struct Clipboard
	{
		// A synth track's machine and its seven DATA pages (SYN AMP FLT EFX LF1-3).
		struct Sound
		{
			uint8_t machine = 0;
			std::array<std::array<uint8_t, 8>, 7> pages{};

			bool operator==(const Sound& _o) const { return machine == _o.machine && pages == _o.pages; }
		};
		struct Steps
		{
			bool midi = false;								// a MIDI sequencer track's page (pastes onto MIDI tracks only)
			std::vector<StepValue> steps;					// the page's steps, from its first
			uint64_t slide = 0;								// bit = step from the first
			std::map<std::pair<uint8_t, uint8_t>, std::map<uint8_t, uint8_t>> locks;	// (page, param) -> step -> value

			bool operator==(const Steps& _o) const { return midi == _o.midi && steps == _o.steps && slide == _o.slide && locks == _o.locks; }
		};
		std::optional<Steps> steps;
		std::optional<Sound> sound;
		std::optional<elektronData::json::Value> songRow;	// a contract row
		std::optional<elektronData::MmKit> kit;				// the kit library
		std::optional<elektronData::MmPattern> pattern;		// the pattern library

		bool operator==(const Clipboard& _o) const
		{
			return steps == _o.steps && sound == _o.sound && songRow == _o.songRow && kit == _o.kit && pattern == _o.pattern;
		}
	};

	struct EditContext
	{
		int currentKit = -1;		// the kit that plays: the live kit edits change only its working kit
		int currentGlobal = -1;		// the active global: the global edits change it
	};

	struct EditResult
	{
		std::vector<Change> changes;
		std::vector<std::string> errors;
		std::string note;
		std::optional<Clipboard> clipboard;
	};

	// {"op":"set","kind","doc"}: the document, validated (elektronData::validate), replaces the one
	// the desk shows (the working kit is the kit that plays). Pure.
	EditResult setDocument(const Documents& _docs, const elektronData::json::Value& _command, const EditContext& _context);

	// One edit intent (DESIGN-UNIFY.md 4.1, mmDeskEdit.cpp), pure: the documents and the clipboard in, the changes
	// (and a new clipboard after a copy) out. The command's row in the table says which document it edits (its
	// kind column) and checks its arguments; pattern edits carry p, the live kit edits k (the kit that plays: they
	// change its working kit), the global edits change the active global. Every changed document is validated
	// (elektronData::validate) before it is returned; an edit that would make an invalid one is refused.
	EditResult apply(const Documents& _docs, const elektronData::json::Value& _command, const Clipboard& _clipboard,
		const EditContext& _context);
	// The ops the edits have a function for: the contract test checks them against the table's Core/Edit rows.
	std::vector<std::string> editOps();

	// The library's cleared slots (kitClear, patClear): a kit of six GND-SIN tracks at their start values, no name (the
	// rest of the slot stays); a pattern without notes, slides or locks (its length, speed, swing, arpeggiator,
	// transposes and kit link stay). What the machine's own CLEAR leaves is not known.
	elektronData::MmKit emptyKit(const elektronData::MmKit& _like, uint8_t _slot);
	elektronData::MmPattern emptyPattern(const elektronData::MmPattern& _like);
	// A slot the library shows as empty: a kit without a name (or the firmware's unused mark), a pattern without a trig.
	bool kitIsEmpty(const elektronData::MmKit& _kit);
	bool patternHasTrigs(const elektronData::MmPattern& _pattern);

	// The Monomachine's command vocabulary (P6): data only; the adapter maps its ops to its own functions.
	using CommandTable = deskCore::CommandTable<>;
	// The command table's group of the library's slot ops (kitCopy ... patClear): the machine asks before they lose
	// something (MmMachine::review).
	constexpr int g_library = 1;

	struct MmModel
	{
		using Kind = mmDesk::Kind;
		using Ref = mmDesk::Ref;
		using Document = mmDesk::Document;
		using Documents = mmDesk::Documents;
		using Change = mmDesk::Change;
		using Clipboard = mmDesk::Clipboard;
		using Context = EditContext;
		using EditResult = mmDesk::EditResult;
		using Table = CommandTable;

		static Ref refOf(const Document& _d) { return mmDesk::refOf(_d); }
		static std::optional<Document> get(const Documents& _docs, const Ref& _ref) { return _docs.get(_ref); }
		static void set(Documents& _docs, const Document& _d) { _docs.set(_d); }
		static void erase(Documents& _docs, const Ref& _ref) { _docs.erase(_ref); }
		// The edit intents (DESIGN-UNIFY.md 4.1), mmDeskEdit.cpp.
		static EditResult apply(const Documents& _docs, const elektronData::json::Value& _command, const Clipboard& _clip, const Context& _c)
		{
			return mmDesk::apply(_docs, _command, _clip, _c);
		}
		static EditResult setDocument(const Documents& _docs, const elektronData::json::Value& _command, const Context& _c)
		{
			return mmDesk::setDocument(_docs, _command, _c);
		}
		// {"type":"doc","kind","slot","pending","source","doc"}; the working kit's slot is the kit it came from.
		static elektronData::json::Value docMessage(const Ref& _ref, const Document& _doc, bool _pending, deskCore::Source _source);
		// The document kinds (deskCore::KindSpec): names, counts, dump sizes, JSON.
		static const std::vector<deskCore::KindSpec<MmModel>>& kinds();
		// What the editor does not do yet on any engine (merged into the capabilities): nothing since MM-P4
		// (what one engine cannot do is that engine's capability, with the reason).
		static const std::vector<deskCore::Unsupported>& unsupported();
		// The page protocol's version (mm-data-contract.md 2, machine.contract): bumped when a member is renamed,
		// removed or re-meant; adding one keeps it.
		static constexpr int contractVersion = 2;
		// The Monomachine's page keeps no clipboard in the core.
		static std::optional<elektronData::json::Value> clipboardDocument(const Clipboard&) { return {}; }
		// The Monomachine Editor's command vocabulary.
		static const Table& commands();
		// P7: the active global as it must be for the machine to follow a DAW's tempo and transport (MIDI
		// clock, Start, Stop): GLOBAL › MIDI SYNC CLOCK IN and TRANSPORT IN on (raw 0x05 and 0x06, tempoSync and transportIn;
		// as booted both are off). Nothing when it already does. Pure (measured: mmDeskFirmwareTest hostclock).
		static std::optional<elektronData::MmGlobal> hostFollowing(const elektronData::MmGlobal& _global);
		// The OS 1.32B machine table and enumerations as "mm-desk/catalogue".
		static elektronData::json::Value catalogue();
		// What a lifecycle state means for the user (machine.lifecycleText; also why a gated command waits).
		static std::string lifecycleText(deskCore::Lifecycle _l);
		// The questions the MM adapter may ask (deskCore::Ask::what): the contract's ask enum.
		static const std::vector<std::string>& asks()
		{
			static const std::vector<std::string> a{"loadKit", "reloadKit", "overwriteSlot", "clearSlot", "discardKit", "transportIgnore", "breakChain"};
			return a;
		}
	};

	inline const CommandTable& commandTable() { return MmModel::commands(); }
}
