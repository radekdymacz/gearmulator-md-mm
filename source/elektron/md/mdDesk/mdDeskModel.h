#pragma once

#include "mdDeskEdit.h"
#include "mdDeskHistory.h"

#include "deskCore/deskCommands.h"
#include "deskCore/deskCore.h"

#include <optional>
#include <string>

namespace mdDesk
{
	// The Machinedrum model for deskCore::Core (P6): the documents, the pure edits, the
	// page's messages for them, and the command table. No machine: that is MdMachine.
	struct MdModel
	{
		using Kind = DocKind;
		using Ref = DocRef;
		using Document = mdDesk::Document;
		using Documents = mdDesk::Documents;
		using Change = mdDesk::Change;
		using Clipboard = mdDesk::Clipboard;
		using Context = EditContext;
		using EditResult = mdDesk::EditResult;

		static Ref refOf(const Document& _d) { return mdDesk::refOf(_d); }
		static void set(Documents& _docs, const Document& _d) { _docs.set(_d); }
		static void erase(Documents& _docs, const Ref& _ref);
		// apply (mdDeskEdit.h), plus "set": a whole document as the intent, the pure transform
		// "replace it with this validated value" (P6; the Monomachine's edits are these).
		static EditResult apply(const Documents& _docs, const elektronData::json::Value& _command, const Clipboard& _clip,
			const Context& _context);
		// {"type":"doc","kind","slot","pending","source","doc"}
		static elektronData::json::Value docMessage(const Ref& _ref, const Document& _doc, bool _pending, deskCore::Source _source);
		// The contract's older places for the core's parts (desk.undo, desk.redo, ...).
		static void decorate(elektronData::json::Value& _machine, const History& _history, const deskCore::Machine<MdModel>& _m);
	};

	const char* kindName(DocKind _k);
	std::optional<DocKind> kindFromName(const std::string& _name);
	elektronData::json::Value documentToJson(const Document& _doc);

	// The Machinedrum Editor's command vocabulary (every op the page may send).
	const deskCore::CommandTable& commandTable();
}
