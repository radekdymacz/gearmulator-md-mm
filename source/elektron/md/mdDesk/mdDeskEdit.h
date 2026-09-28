#pragma once

#include "deskCore/deskRef.h"

#include "elektronData/json.h"
#include "elektronData/mdGlobal.h"
#include "elektronData/mdKit.h"
#include "elektronData/mdPattern.h"
#include "elektronData/mdSong.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace mdDesk
{
	// MD Desk's editing model: pure document transforms. A UI command (a small
	// JSON object, doc/modern-ux/P2-RESULT.md §3) goes in, the changed documents
	// come out. No bytes, no device, no threads: delivering a change to the
	// machine is the transport's job (mdDeskDelivery.h, mdDataLink).

	enum class DocKind : uint8_t
	{
		Pattern,
		Kit,
		Song,
		Global,
		WorkingKit
	};

	using DocRef = deskCore::Ref<DocKind>;

	// The kit that plays, as its own document (P6): the machine's working copy of the kit it
	// loaded from slot kit.position, with the live edits it took. One identity (the working kit,
	// slot 0 of its kind); DocKind::Kit documents are the stored slots only.
	struct WorkingKit
	{
		elektronData::MdKit kit;

		bool operator==(const WorkingKit& _o) const { return kit == _o.kit; }
	};

	using Document = std::variant<elektronData::MdPattern, elektronData::MdKit, elektronData::MdSong,
		elektronData::MdGlobal, WorkingKit>;

	DocRef refOf(const Document& _doc);
	// The hardware limits a document breaks (elektronData::validate), one line each.
	std::vector<std::string> problemsOf(const Document& _doc);

	// Every document the desk holds: the stored kit slots, and the kit that plays as the working kit.
	struct Documents
	{
		std::map<uint8_t, elektronData::MdPattern> patterns;
		std::map<uint8_t, elektronData::MdKit> kits;		// stored slots
		std::map<uint8_t, elektronData::MdSong> songs;
		std::optional<elektronData::MdGlobal> global;
		std::optional<WorkingKit> working;

		// The working kit when it is the one loaded from slot _kit.
		const elektronData::MdKit* workingKitOf(uint8_t _kit) const
		{
			return working && working->kit.position == _kit ? &working->kit : nullptr;
		}

		std::optional<Document> get(const DocRef& _ref) const;
		void set(const Document& _doc);
	};

	struct Change
	{
		// A kit change is a stored-slot write (a dump, plus LOAD KIT into the kit that plays); a
		// working-kit change is a live edit of the kit that plays. The kind says which.
		Document before;
		Document after;

		DocRef ref() const { return refOf(after); }
	};

	// What copy puts aside for paste. Values only; nothing refers back.
	struct Clipboard
	{
		struct Steps
		{
			size_t length = 0;
			uint64_t trigs = 0, accent = 0, slide = 0;
			std::map<uint8_t, std::map<uint8_t, uint8_t>> locks;	// param -> step -> value

			bool operator==(const Steps& _o) const
			{
				return length == _o.length && trigs == _o.trigs && accent == _o.accent && slide == _o.slide && locks == _o.locks;
			}
		};
		struct Sound
		{
			uint32_t model = 0;
			std::array<uint8_t, elektronData::MdKit::g_paramsPerTrack> params{};
			uint8_t level = 0;
			elektronData::MdLfo lfo;

			bool operator==(const Sound& _o) const
			{
				return model == _o.model && params == _o.params && level == _o.level && lfo == _o.lfo;
			}
		};
		std::optional<Steps> steps;
		std::optional<Sound> sound;
		std::optional<elektronData::json::Value> songRow;	// contract row
		std::optional<elektronData::MdKit> kit;				// the kit library (P4)
		std::optional<elektronData::MdPattern> pattern;		// the pattern chooser (P4)

		bool operator==(const Clipboard& _o) const
		{
			return steps == _o.steps && sound == _o.sound && songRow == _o.songRow && kit == _o.kit && pattern == _o.pattern;
		}
	};

	struct EditResult
	{
		std::vector<Change> changes;		// empty: nothing changed
		std::vector<std::string> errors;	// non-empty: refused, nothing changed
		std::string note;					// one line for the user, may be empty
		std::optional<Clipboard> clipboard;	// the new clipboard after a copy command
	};

	// What an edit may depend on besides the documents: the machine's current kit (the only kit
	// the live kit edits may change). Data from the adapter.
	struct EditContext
	{
		std::optional<uint8_t> currentKit;
	};

	// Applies one command, pure: the documents and the clipboard in, the changes (and a new
	// clipboard after a copy) out. The command's row in the model's command table says which
	// document it edits (its kind column) and checks its arguments first; then one function
	// per op runs (op -> function tables), trusting the row's types and constant ranges and
	// checking only what depends on the document. Pattern commands carry "p" (slot), song
	// commands "s"; the live kit commands (kind WorkingKit) carry "k", which must be the kit
	// that plays, and edit the working kit; the kit library and pattern chooser commands
	// (group library, mdDeskLibrary.h) write stored slots. Every changed document is validated
	// with the hardware limits (elektronData::validate) before it is returned; a command that
	// would produce an invalid document is refused with its problems.
	EditResult apply(const Documents& _docs, const elektronData::json::Value& _command, const Clipboard& _clipboard,
		const EditContext& _context = {});
	// The ops the edit tables have a function for (the library's included): the contract test
	// checks them against the command table's Core/Edit rows, both ways.
	std::vector<std::string> editOps();

	// The values "clear sound" uses: the machine's own defaults live in the firmware and
	// cannot be read without saving the kit.
	const std::array<uint8_t, 24>& neutralTrackValues();
	uint8_t neutralTrackLevel();

	// Step helpers shared with the page: the accent/slide set a step toggle edits,
	// which is pattern-wide when the pattern's "edit all" flag is set.
	bool accentOn(const elektronData::MdPattern& _p, size_t _track, size_t _step);
	bool slideOn(const elektronData::MdPattern& _p, size_t _track, size_t _step);
}
