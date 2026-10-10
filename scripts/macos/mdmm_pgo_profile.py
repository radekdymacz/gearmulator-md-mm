#!/usr/bin/env python3
"""The committed MD/MM profile (source/elektron/md/pgo): proof that it holds no firmware, and its record.

  mdmm_pgo_profile.py check <profile.proftext> [--rom <rom.bin>]...
      Parses the whole file as LLVM's text profile: every line must be a header, a comment, a function or
      call-target name made of symbol characters, or a decimal number. Nothing else can be in it, so nothing
      from the firmware can. With --rom it also looks for any run of 31 or more bytes of the ROM in the file.
      Exit 1 on any failure. CI runs it without a ROM; scripts/macos/train_mdmm_pgo.sh runs it with both.

  mdmm_pgo_profile.py record <profile.proftext> <out.json> --source <checkout> --architecture <arch>
      --compiler <text> --workload <text>... --rom MD=<rom.bin> --rom MM=<rom.bin>
      Writes the record the build reads (pgo/committedProfile.cmake): the profile's SHA-256, the git tree of
      each profiled folder (the staleness check), the commit, the compiler and the workload.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import sys

SCHEMA = "gearmulator.mdmm.committed-pgo-profile.v1"
# The folders of the targets that get the profile (source/elektron/md/optimization.cmake).
PROFILED_FOLDERS = (
    "source/elektron/md/mdLib",
    "source/mc68k",
    "source/dsp56300/source/dsp56kEmu",
    "source/dsp56300/source/dsp56kBase",
)
# Mangled C++ names, C names, and clang's "<file>;<name>" or "<file>:<name>" for local functions.
NAME = re.compile(r"^[A-Za-z_.$][A-Za-z0-9_.$;:/<>\-]*$")
NUMBER = re.compile(r"^[0-9]+$")
TARGET = re.compile(r"^([A-Za-z_.$][A-Za-z0-9_.$;:/<>\-]*|\*\*[A-Za-z ]+\*\*):[0-9]+$")
HEADER = re.compile(r"^:[a-z_]+$")
WINDOW = 16  # ROM chunks of 16 bytes: any common run of 2 * 16 - 1 = 31 bytes contains a whole chunk


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse(path: pathlib.Path) -> int:
    """Return the number of functions; raise ValueError naming the first line that is not profile data."""
    raw = path.read_bytes()
    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError as error:
        raise ValueError(f"not ASCII at byte {error.start}") from None
    functions = 0
    for number, line in enumerate(text.split("\n"), 1):
        if line == "" or line.startswith("# "):
            functions += line == "# Func Hash:"
            continue
        if HEADER.match(line) and number == 1:
            continue
        if NUMBER.match(line) or TARGET.match(line) or NAME.match(line):
            continue
        if line.startswith("$") and NUMBER.match(line[1:]):  # MC/DC bitmap bytes
            continue
        raise ValueError(f"line {number} is not profile data: {line[:80]!r}")
    if functions == 0:
        raise ValueError("no functions")
    return functions


def rom_runs(profile: pathlib.Path, rom: pathlib.Path) -> list[int]:
    data = profile.read_bytes()
    image = rom.read_bytes()
    chunks: dict[bytes, int] = {}
    for offset in range(0, len(image) - WINDOW + 1, WINDOW):
        chunk = image[offset:offset + WINDOW]
        # A run of one byte (erased flash, padding) or of digits and newlines says nothing about the firmware.
        if len(set(chunk)) <= 2 or all(c in b"0123456789\n" for c in chunk):
            continue
        chunks.setdefault(chunk, offset)
    found = []
    for offset in range(0, len(data) - WINDOW + 1):
        hit = chunks.get(data[offset:offset + WINDOW])
        if hit is not None:
            found.append(hit)
    return found


def git(source: pathlib.Path, *args: str) -> str:
    return subprocess.run(["git", "-C", str(source), *args], check=True, capture_output=True,
                          text=True).stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    check = sub.add_parser("check")
    check.add_argument("profile", type=pathlib.Path)
    check.add_argument("--rom", type=pathlib.Path, action="append", default=[])
    record = sub.add_parser("record")
    record.add_argument("profile", type=pathlib.Path)
    record.add_argument("output", type=pathlib.Path)
    record.add_argument("--source", type=pathlib.Path, required=True)
    record.add_argument("--architecture", required=True)
    record.add_argument("--compiler", required=True)
    record.add_argument("--workload", action="append", default=[])
    record.add_argument("--rom", action="append", default=[], metavar="MODEL=PATH")
    args = parser.parse_args()

    try:
        functions = parse(args.profile)
    except ValueError as error:
        print(f"mdmm_pgo_profile: {args.profile}: {error}", file=sys.stderr)
        return 1

    if args.command == "check":
        for rom in args.rom:
            runs = rom_runs(args.profile, rom)
            if runs:
                print(f"mdmm_pgo_profile: {args.profile} holds {len(runs)} run(s) of {rom.name}, "
                      f"first at ROM offset {runs[0]:#x}", file=sys.stderr)
                return 1
            print(f"mdmm_pgo_profile: no run of 31 bytes or more of {rom.name}")
        print(f"mdmm_pgo_profile: {args.profile}: {functions} functions, profile data only")
        return 0

    source = args.source.resolve()
    dirty = git(source, "status", "--porcelain", "--", *PROFILED_FOLDERS)
    if dirty:
        print("mdmm_pgo_profile: the profiled folders have uncommitted changes; commit them first:\n" + dirty,
              file=sys.stderr)
        return 1
    firmware = {}
    for item in args.rom:
        model, _, path = item.partition("=")
        firmware[model] = sha256(pathlib.Path(path))
    record_data = {
        "schema": SCHEMA,
        "about": "The committed MD/MM PGO profile (lever L6): what it was trained on. Written by "
                 "scripts/macos/train_mdmm_pgo.sh; read by source/elektron/md/pgo/committedProfile.cmake, "
                 "which warns when a folder under 'sources' has another git tree than here (stale).",
        "profile": {
            "file": args.profile.name,
            "format": "llvm text (front-end instrumentation), sparse",
            "sha256": sha256(args.profile),
            "functions": functions,
        },
        "trained": {
            "commit": git(source, "rev-parse", "HEAD"),
            "architecture": args.architecture,
            "compiler": args.compiler,
            "workload": args.workload,
            "firmware_sha256": firmware,
        },
        "sources": {folder: git(source, "rev-parse", f"HEAD:{folder}") for folder in PROFILED_FOLDERS},
    }
    args.output.write_text(json.dumps(record_data, indent=1) + "\n", encoding="utf-8")
    print(f"mdmm_pgo_profile: wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
