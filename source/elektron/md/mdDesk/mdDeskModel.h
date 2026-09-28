#pragma once

#include "mdDeskEdit.h"
#include "mdDeskHistory.h"

#include "deskCore/deskCommands.h"
#include "deskCore/deskCore.h"

namespace deskCore
{
	template<typename Model> struct KindSpec;
}

#include <optional>
#include <string>

namespace mdDesk
{
	// The Machinedrum's command vocabulary (P6): data only; the adapter maps its ops to its own functions.
	using CommandTable = deskCore::CommandTable<>;
	// The table's group column: the kit library and the pattern chooser (mdDeskLibrary.h).
	constexpr int g_library = 1;

	// The Machinedrum model for deskCore (P6): the documents, the pure edits, the page's messages
	// for them, and the command table. No machine: that is the adapter (MdAdapter).
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
		using Table = CommandTable;

		static Ref refOf(const Document& _d) { return mdDesk::refOf(_d); }
		static std::optional<Document> get(const Documents& _docs, const Ref& _ref) { return _docs.get(_ref); }
		static void set(Documents& _docs, const Document& _d) { _docs.set(_d); }
		static void erase(Documents& _docs, const Ref& _ref);
		// apply (mdDeskEdit.h): a command's pure edit.
		static EditResult apply(const Documents& _docs, const elektronData::json::Value& _command, const Clipboard& _clip,
			const Context& _context);
		// "set": a whole document as the intent, the pure transform "replace it with this validated value".
		static EditResult setDocument(const Documents& _docs, const elektronData::json::Value& _command, const Context& _context);
		// {"type":"doc","kind","slot","pending","source","doc"}
		static elektronData::json::Value docMessage(const Ref& _ref, const Document& _doc, bool _pending, deskCore::Source _source);
		// The document kinds (deskCore::KindSpec): names, counts, dump sizes, JSON.
		static const std::vector<deskCore::KindSpec<MdModel>>& kinds();
		// What the clipboard holds, for the page's Paste (machine.clipboard).
		static std::optional<elektronData::json::Value> clipboardDocument(const Clipboard& _clip);
		// What the editor does not do yet on any engine: nothing for the Machinedrum.
		static const std::vector<deskCore::Unsupported>& unsupported() { static const std::vector<deskCore::Unsupported> none; return none; }
		// The Machinedrum Editor's command vocabulary (every op the page may send).
		static const Table& commands();
		// P7: the active global as it must be for the machine to follow a DAW's tempo and transport (the
		// plug-in sends them as MIDI clock, Start, Stop): TEMPO IN external, CTRL IN on. Nothing when it
		// already does. Pure (measured: mdP4ProbeFirmwareTest hostclock).
		static std::optional<elektronData::MdGlobal> hostFollowing(const elektronData::MdGlobal& _global);
		// The OS 1.63 machine table as a "md-desk/machines" document (the page gets it on ready).
		static elektronData::json::Value catalogue();
		// What a lifecycle state means for the user (machine.lifecycleText; also why a gated command waits).
		static std::string lifecycleText(deskCore::Lifecycle _l);
		// The questions the MD adapter may ask (deskCore::Ask::what): the contract's ask enum.
		static const std::vector<std::string>& asks()
		{
			static const std::vector<std::string> a{"breakChain", "discardKit", "loadKit", "reloadKit", "overwriteKit", "overwriteSlot",
				"clearSlot", "relinkKit"};
			return a;
		}
	};

	const char* kindName(DocKind _k);
	std::optional<DocKind> kindFromName(const std::string& _name);
	elektronData::json::Value documentToJson(const Document& _doc);

	inline const CommandTable& commandTable() { return MdModel::commands(); }
}
