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

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace mmDesk
{
	// The Monomachine model for deskCore::Core (P6): the same pipeline as the Machinedrum's.
	// Its page sends whole documents as the intent ({"op":"set","kind","doc","g"}); setDocument is
	// the pure transform "replace it with this validated value"; undo is the core's.
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

	struct Clipboard
	{
		bool operator==(const Clipboard&) const { return true; }
	};

	struct EditContext
	{
		int currentKit = -1;
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

	// The Monomachine's command vocabulary (P6): data only; the adapter maps its ops to its own functions.
	using CommandTable = deskCore::CommandTable<>;

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
		// The Monomachine's page edits nothing in small steps: every edit is a whole document (set).
		static EditResult apply(const Documents&, const elektronData::json::Value& _command, const Clipboard&, const Context&)
		{
			EditResult r;
			r.errors.push_back("unknown command " + deskCore::opOf(_command));
			return r;
		}
		static EditResult setDocument(const Documents& _docs, const elektronData::json::Value& _command, const Context& _c)
		{
			return mmDesk::setDocument(_docs, _command, _c);
		}
		// {"type":"doc","kind","slot","pending","source","doc"}; the working kit's slot is the kit it came from.
		static elektronData::json::Value docMessage(const Ref& _ref, const Document& _doc, bool _pending, deskCore::Source _source);
		// The document kinds (deskCore::KindSpec): names, counts, dump sizes, JSON.
		static const std::vector<deskCore::KindSpec<MmModel>>& kinds();
		// What the editor does not do yet on any engine (merged into the capabilities).
		static const std::vector<deskCore::Unsupported>& unsupported();
		// The Monomachine's page keeps no clipboard in the core.
		static std::optional<elektronData::json::Value> clipboardDocument(const Clipboard&) { return {}; }
		// The Monomachine Editor's command vocabulary.
		static const Table& commands();
		// The OS 1.32B machine table and enumerations as "mm-desk/catalogue".
		static elektronData::json::Value catalogue();
		// What a lifecycle state means for the user (machine.lifecycleText; also why a gated command waits).
		static std::string lifecycleText(deskCore::Lifecycle _l);
		// The questions the MM adapter may ask (deskCore::Ask::what): the contract's ask enum.
		static const std::vector<std::string>& asks()
		{
			static const std::vector<std::string> a{"loadKit", "reloadKit", "overwriteSlot", "discardKit"};
			return a;
		}
	};

	inline const CommandTable& commandTable() { return MmModel::commands(); }
}
