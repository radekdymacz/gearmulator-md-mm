#pragma once

#include "deskCore/deskCommands.h"
#include "deskCore/deskCore.h"
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
	// Its page sends whole documents as the intent ({"op":"set","kind","doc","g"}); apply is
	// the pure transform "replace it with this validated value"; undo is the core's.
	enum class Kind : uint8_t
	{
		Pattern,
		Kit,
		Song,
		Global
	};

	using Ref = deskCore::Ref<Kind>;
	using Document = std::variant<elektronData::MmPattern, elektronData::MmKit, elektronData::MmSong, elektronData::MmGlobal>;

	Ref refOf(const Document& _doc);
	const char* kindName(Kind _k);
	std::optional<Kind> kindFromName(const std::string& _name);
	elektronData::json::Value documentToJson(const Document& _doc);

	// Every document the desk shows. For the kit that plays, kits[] holds the working kit.
	struct Documents
	{
		std::map<uint8_t, elektronData::MmPattern> patterns;
		std::map<uint8_t, elektronData::MmKit> kits;
		std::map<uint8_t, elektronData::MmSong> songs;
		std::map<uint8_t, elektronData::MmGlobal> globals;

		std::optional<Document> get(const Ref& _ref) const;
		void set(const Document& _doc);
		void erase(const Ref& _ref);
	};

	struct Change
	{
		Document before;
		Document after;
		// Kit changes: false = live edits of the kit that plays; true = a stored-slot dump.
		bool slotWrite = false;

		Ref ref() const { return refOf(after); }
	};

	struct Clipboard
	{
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
	// the desk shows. Pure.
	EditResult apply(const Documents& _docs, const elektronData::json::Value& _command, const EditContext& _context);

	class MmMachine;
	// A machine command's handler: a function of the Monomachine adapter (the table's handler column).
	using MachineHandler = deskCore::Outcome (MmMachine::*)(const elektronData::json::Value&, const Documents&);
	using CommandTable = deskCore::CommandTable<MachineHandler>;

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
		static EditResult apply(const Documents& _docs, const elektronData::json::Value& _command, const Clipboard&, const Context& _c)
		{
			return mmDesk::apply(_docs, _command, _c);
		}
		// {"type":"doc","kind","slot","pending","working","source","doc"}; working = the kit that plays.
		static elektronData::json::Value docMessage(const Ref& _ref, const Document& _doc, bool _pending, deskCore::Source _source);
		static void decorate(elektronData::json::Value&, const deskCore::History<Change>&, const deskCore::Machine<MmModel>&) {}
		// The Monomachine Editor's command vocabulary (mmDeskMachine.cpp: the handler column is the adapter's).
		static const Table& commands();
		// The OS 1.32B machine table and enumerations as "mm-desk/catalogue".
		static elektronData::json::Value catalogue();
		static std::string refusal(deskCore::Lifecycle) { return "The engine is not ready yet."; }
	};

	inline const CommandTable& commandTable() { return MmModel::commands(); }
}
