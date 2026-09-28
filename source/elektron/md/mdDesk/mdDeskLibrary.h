#pragma once

#include "mdDeskEdit.h"

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
	//  - rename of a slot that does not play writes its dump with the new name (the kit
	//    that plays is renamed live with kitName, 0x55).
	// Kit results are stored-slot writes (DocKind::Kit): into the kit that plays that is a
	// dump plus LOAD KIT, which replaces its unsaved edits. A copy of the kit that plays copies
	// what it sounds like (the working kit).
	//
	// Commands: kitCopy {k}, kitPaste {k}, kitCopyTo {from, to}, kitClear {k}, kitRename
	// {k, name}; patCopy {p}, patPaste {p}, patCopyTo {from, to}, patClear {p}.
	bool isLibraryCommand(const std::string& _op);
	// The library part of apply (mdDeskEdit.h), which calls it.
	EditResult applyLibrary(const Documents& _docs, const elektronData::json::Value& _command, Clipboard& _clipboard,
		const EditContext& _context);

	elektronData::MdKit emptyKit(const elektronData::MdKit& _like, uint8_t _slot);
	elektronData::MdPattern emptyPattern(const elektronData::MdPattern& _like);
	// A kit counts as empty in the library: no name and every track GND-EMPTY.
	bool isEmptyKit(const elektronData::MdKit& _kit);
}
