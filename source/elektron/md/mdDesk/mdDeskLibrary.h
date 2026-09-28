#pragma once

#include "mdDeskEdit.h"

#include "deskCore/deskCommands.h"

namespace mdDesk
{
	// The kit library and the pattern chooser (P4, mockup v50) as pure slot edits. What
	// the firmware offers for them (mdP4ProbeFirmwareTest library): kit and pattern dumps
	// write a slot, LOAD KIT / SAVE KIT / LOAD PATTERN act on the machine. There is no
	// CLEAR, COPY or RENAME command, so:
	//  - copy reads the document the desk holds (the kit that plays is its working copy
	//    from memory, unsaved edits included);
	//  - paste and drag-copy write a dump into the target slot;
	//  - clear writes an empty kit (every track GND-EMPTY, neutral values, no name) or an
	//    empty pattern (no trigs or locks; length, speed, swing, accent and kit link kept).
	//    What the machine's own CLEAR leaves is not known;
	//  - rename writes a stored slot's dump with the new name; the kit that plays is renamed
	//    live with kitName (0x55), and kitRename refuses it.
	// Kit results are stored-slot writes (DocKind::Kit): into the kit that plays that is a
	// dump plus LOAD KIT, which replaces its unsaved edits. A copy of the kit that plays copies
	// what it sounds like (the working kit).
	//
	// Commands: kitCopy {k}, kitPaste {k}, kitCopyTo {from, to}, kitClear {k}, kitRename
	// {k, name}; patCopy {p}, patPaste {p}, patCopyTo {from, to}, patClear {p}. One action per
	// op (copy, paste, copyTo, clear, rename); the command table's kind column says which shelf
	// (kits or patterns) it acts on, the group column that it is the library's. kitRename of the
	// kit that plays is refused: kitName renames it.
	//
	// The library part of apply (mdDeskEdit.h), which calls it with the command's table row after
	// checking the command against it.
	EditResult applyLibrary(const Documents& _docs, const elektronData::json::Value& _command, const deskCore::Command<>& _row,
		Clipboard& _clipboard, const EditContext& _context);

	elektronData::MdKit emptyKit(const elektronData::MdKit& _like, uint8_t _slot);
	elektronData::MdPattern emptyPattern(const elektronData::MdPattern& _like);
	// A kit counts as empty in the library: no name and every track GND-EMPTY.
	bool isEmptyKit(const elektronData::MdKit& _kit);
}
