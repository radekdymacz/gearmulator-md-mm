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
		Global
	};

	using DocRef = deskCore::Ref<DocKind>;

	using Document = std::variant<elektronData::MdPattern, elektronData::MdKit, elektronData::MdSong,
		elektronData::MdGlobal>;

	DocRef refOf(const Document& _doc);

	// Every document the desk holds. For the kit that plays, kits[] holds the
	// working copy (the stored slot plus live edits), not the stored slot.
	struct Documents
	{
		std::map<uint8_t, elektronData::MdPattern> patterns;
		std::map<uint8_t, elektronData::MdKit> kits;
		std::map<uint8_t, elektronData::MdSong> songs;
		std::optional<elektronData::MdGlobal> global;

		std::optional<Document> get(const DocRef& _ref) const;
		void set(const Document& _doc);
	};

	struct Change
	{
		Document before;
		Document after;
		// Kit changes: false = live edits of the kit that plays (CCs, live SysEx); true = a
		// stored-slot write (the kit library, P4): a kit dump, plus LOAD KIT when it is the
		// kit that plays.
		bool slotWrite = false;

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
		};
		struct Sound
		{
			uint32_t model = 0;
			std::array<uint8_t, elektronData::MdKit::g_paramsPerTrack> params{};
			uint8_t level = 0;
			elektronData::MdLfo lfo;
		};
		std::optional<Steps> steps;
		std::optional<Sound> sound;
		std::optional<elektronData::json::Value> songRow;	// contract row
		std::optional<elektronData::MdKit> kit;				// the kit library (P4)
		std::optional<elektronData::MdPattern> pattern;		// the pattern chooser (P4)
	};

	struct EditResult
	{
		std::vector<Change> changes;		// empty: nothing changed
		std::vector<std::string> errors;	// non-empty: refused, nothing changed
		std::string note;					// one line for the user, may be empty
	};

	// Applies one command. Pattern commands carry "p" (slot), kit commands "k",
	// song commands "s". Every changed document is validated with the hardware
	// limits (elektronData::validate) before it is returned; a command that would
	// produce an invalid document is refused with its problems. Copy commands
	// change only _clipboard.
	EditResult apply(const Documents& _docs, const elektronData::json::Value& _command, Clipboard& _clipboard);

	// The values "clear sound" uses: the machine's own defaults live in the firmware and
	// cannot be read without saving the kit.
	const std::array<uint8_t, 24>& neutralTrackValues();
	uint8_t neutralTrackLevel();

	// Step helpers shared with the page: the accent/slide set a step toggle edits,
	// which is pattern-wide when the pattern's "edit all" flag is set.
	bool accentOn(const elektronData::MdPattern& _p, size_t _track, size_t _step);
	bool slideOn(const elektronData::MdPattern& _p, size_t _track, size_t _step);
}
