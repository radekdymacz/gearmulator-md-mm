#!/usr/bin/env python3
"""Helpers of scripts/mdmm-local-gate.sh (doc/release/LOCAL-GATE.md).

Subcommands (every one that judges prints `numbers=...` and `notes=...` lines for the stage table and exits 0 for
green, 1 for red):

  run            run a command with a time limit and a log file; its process group dies with it
  ctest-missing  ctest -N --show-only=json-v1 on stdin: registered tests whose program was not built
  junit          ctest's --output-junit file: executed, failed and skipped tests, by name
  goldens        the runs a goldens file asks for (scenario x outputs x speed-ups switch)
  capacity       the core-capacity receipt of scripts/macos/check_mdmm_core_capacity.py
  journeys       the report of scripts/mdmm-journeys.sh
  pluginval      the pluginval-summary.md of scripts/ci/mdmm_pluginval.sh
  summary        stages.tsv and info.txt of a run -> summary.md and its text on stdout
"""

from __future__ import annotations

import argparse
import json
import os
import re
import signal
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

OUTPUTS = ("stereo", "all")
SPEEDUPS = ("on", "off")
ENV_ASSIGNMENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*=")


def emit(numbers: str, notes: str = "") -> None:
    print("numbers=" + numbers.replace("\n", " "))
    print("notes=" + notes.replace("\n", " "))


# ---------------------------------------------------------------------------------------------------- run
def cmd_run(args: argparse.Namespace) -> int:
    command = list(args.command)
    if command and command[0] == "--":
        command = command[1:]
    env = dict(os.environ)
    for name in args.unset:
        env.pop(name, None)
    while command and ENV_ASSIGNMENT.match(command[0]):
        name, value = command.pop(0).split("=", 1)
        env[name] = value
    if not command:
        print("run: no command", file=sys.stderr)
        return 2
    with open(args.log, "ab" if args.append else "wb") as log:
        log.write(("$ " + " ".join(command) + "\n").encode())
        log.flush()
        try:
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                cwd=args.cwd or None, env=env, start_new_session=True)
        except OSError as error:
            log.write(f"[gate] cannot start {command[0]}: {error}\n".encode())
            return 127

        def kill_group(sig: int) -> None:
            try:
                os.killpg(process.pid, sig)
            except OSError:
                pass

        def interrupted(signum: int, _frame: object) -> None:
            kill_group(signal.SIGTERM)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                kill_group(signal.SIGKILL)
            raise SystemExit(128 + signum)

        signal.signal(signal.SIGINT, interrupted)
        signal.signal(signal.SIGTERM, interrupted)
        try:
            code = process.wait(timeout=args.timeout if args.timeout > 0 else None)
        except subprocess.TimeoutExpired:
            kill_group(signal.SIGTERM)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                kill_group(signal.SIGKILL)
                process.wait()
            log.write(f"\n[gate] killed after {args.timeout} s (time limit)\n".encode())
            return 124
        kill_group(signal.SIGTERM)  # anything the command left behind in its group
    return code if code >= 0 else 128 - code


# ---------------------------------------------------------------------------------------- ctest-missing
def cmd_ctest_missing(args: argparse.Namespace) -> int:
    try:
        listing = json.load(sys.stdin)
    except ValueError as error:
        emit("ctest -N printed no JSON", str(error))
        return 1
    missing, total = [], 0
    for test in listing.get("tests", []):
        properties = {p["name"]: p["value"] for p in test.get("properties", [])}
        if properties.get("DISABLED"):
            continue
        total += 1
        command = test.get("command") or []
        if command and os.path.exists(command[0]):
            continue
        missing.append(test["name"])
    if missing:
        emit(f"{len(missing)} of {total} registered tests have no program", "program not built: " + " ".join(missing))
        return 1
    emit(f"{total} registered tests, every program built")
    return 0


# ----------------------------------------------------------------------------------------------- junit
def cmd_junit(args: argparse.Namespace) -> int:
    try:
        cases = ET.parse(args.file).getroot().findall("testcase")
    except (OSError, ET.ParseError) as error:
        emit("no ctest summary", f"cannot read {args.file}: {error}")
        return 1
    tolerated = {}
    for item in args.tolerate:
        name, _, reason = item.partition("=")
        tolerated[name] = reason
    failed, skipped, disabled, ran, times = [], [], [], 0, []
    for case in cases:
        name = case.get("name", "?")
        if case.get("status") == "disabled":
            disabled.append(name)
        elif case.find("skipped") is not None:
            skipped.append(name)
        else:
            ran += 1
            times.append((float(case.get("time") or 0), name))
            if case.find("failure") is not None or case.find("error") is not None:
                failed.append(name)
    unexpected = [name for name in skipped if name not in tolerated]
    slowest = ", ".join(f"{name} {seconds:.0f} s" for seconds, name in sorted(times, reverse=True)[:3])
    numbers = f"{ran} executed, {len(failed)} failed, {len(skipped)} skipped"
    if disabled:
        numbers += f", {len(disabled)} disabled"
    notes = []
    if failed:
        notes.append("FAILED: " + " ".join(failed))
    if unexpected:
        notes.append("SKIPPED (a skip is red here): " + " ".join(unexpected))
    if skipped and not unexpected:
        notes.append("skipped, tolerated: " + " ".join(f"{n} ({tolerated[n]})" for n in skipped))
    if disabled:
        notes.append("disabled: " + " ".join(disabled))
    if slowest:
        notes.append("slowest: " + slowest)
    emit(numbers, "; ".join(notes))
    return 1 if failed or unexpected else 0


# --------------------------------------------------------------------------------------------- goldens
def cmd_goldens(args: argparse.Namespace) -> int:
    """Print one `RUN` line per run the goldens file asks for, and `MISSING` for what it has no entry for."""
    entries: dict[str, object] = {}
    path = Path(args.file)
    if path.exists():
        try:
            document = json.loads(path.read_text(encoding="utf-8"))
            entries = document["entries"]
        except (ValueError, KeyError, TypeError) as error:
            print(f"BAD\tgoldens file unreadable: {error}")
            return 1
    elif args.mode == "compare":
        print(f"BAD\tno goldens file at {path}")
        return 1
    scenarios = []
    known: dict[tuple[str, str, str], str] = {}
    fingerprints = set()
    for key in entries:
        parts = key.split("/")
        if len(parts) != 5:
            print(f"BAD\tunexpected goldens key {key!r}")
            return 1
        fingerprint, scenario, outputs, speedups, seconds = parts
        fingerprints.add(fingerprint)
        scenarios.append(scenario)
        known[(scenario, outputs, speedups.replace("speedups-", ""))] = seconds.rstrip("s")
    for name in (args.defaults.split() + args.extra.split()):
        scenarios.append(name)
    missing = 0
    for scenario in sorted(set(scenarios)):
        if not scenario.startswith(("md", "mm")):
            print(f"BAD\tscenario {scenario!r} is neither an md nor an mm one")
            return 1
        for outputs in OUTPUTS:
            for speedups in SPEEDUPS:
                seconds = known.get((scenario, outputs, speedups))
                if seconds is None and args.mode == "compare":
                    print(f"MISSING\t{scenario}\t{outputs}\t{speedups}\t-\t{scenario[:2]}")
                    missing += 1
                    continue
                print(f"RUN\t{scenario}\t{outputs}\t{speedups}\t{seconds or args.seconds}\t{scenario[:2]}")
    print("FINGERPRINTS\t" + " ".join(sorted(fingerprints)))
    return 1 if missing else 0


# ------------------------------------------------------------------------------------------------ capacity
def cmd_capacity(args: argparse.Namespace) -> int:
    try:
        receipt = json.loads(Path(args.file).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        emit("no core-capacity receipt", str(error))
        return 1
    parts, notes = [], []
    for model, data in sorted(receipt.get("models", {}).items()):
        gate = data["gate"]
        parts.append(f"{model} p50 {gate['capacity_median_p50_budget_fraction']:.2f} of the budget "
            f"(limit {gate['capacity_limit']:.2f}), paced p99 {gate['paced_median_p99_budget_fraction']:.2f}")
        if not gate["core_microgate_passed"]:
            notes.append(f"{model} over the capacity limit")
        if gate["paced_render_overruns"]:
            notes.append(f"{model}: {gate['paced_render_overruns']} paced callbacks over their render budget "
                         "(observation, not a gate)")
    passed = bool(receipt.get("core_microgate_passed"))
    emit("; ".join(parts) or "no models in the receipt", "; ".join(notes))
    return 0 if passed else 1


# ------------------------------------------------------------------------------------------------ journeys
def cmd_journeys(args: argparse.Namespace) -> int:
    try:
        lines = Path(args.file).read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as error:
        emit("no journeys report", str(error))
        return 1
    failed = [line.split()[1] for line in lines if re.match(r"^\s*JOURNEY \S+ FAIL", line)]
    skips = [line.strip() for line in lines if re.match(r"^\s*JOURNEY \S+ SKIP", line)]
    covered = [line for line in skips if "onScreen" in line]
    other = [line for line in skips if "onScreen" not in line]
    problems = [line for line in lines if line.startswith("FAIL")]
    totals = next((line for line in lines if line.startswith("JOURNEYS:")), "JOURNEYS: no totals")
    notes = []
    if failed:
        notes.append("FAILED: " + " ".join(sorted(set(failed))))
    if problems:
        notes.append("; ".join(problems[:3]))
    if covered:
        notes.append(f"{len(covered)} journeys skipped because their window was not drawing (needs Journey.onScreen): "
            "run again with the window uncovered and the Mac awake")
    if other:
        names = sorted({line.split()[1] for line in other})
        notes.append(f"{len(names)} skipped for other reasons (tolerated): " + " ".join(names[:12])
                     + (" ..." if len(names) > 12 else ""))
    emit(totals.replace("JOURNEYS: ", ""), "; ".join(notes))
    return 1 if failed or problems or covered or "no totals" in totals else 0


# ------------------------------------------------------------------------------------------------ pluginval
def cmd_pluginval(args: argparse.Namespace) -> int:
    try:
        lines = Path(args.file).read_text(encoding="utf-8").splitlines()
    except OSError as error:
        emit("no pluginval summary", str(error))
        return 1
    rows = [[c.strip() for c in line.strip("|").split("|")]
            for line in lines if line.startswith("| ") and "Plug-in" not in line]
    rows = [row for row in rows if len(row) >= 3]
    bad = [f"{row[0]} s{row[1]}: {row[2]}" for row in rows if row[2] != "PASS"]
    emit(f"{len(rows) - len(bad)} of {len(rows)} runs passed (plug-in x strictness)", "; ".join(bad))
    return 1 if bad or not rows else 0


# ----------------------------------------------------------------------------------------------- summary
def read_rows(path: Path, columns: int) -> list[list[str]]:
    if not path.exists():
        return []
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        fields = line.split("\t")
        if len(fields) < columns:
            fields += [""] * (columns - len(fields))
        rows.append(fields[:columns])
    return rows


def read_info(path: Path) -> dict[str, str]:
    info: dict[str, str] = {}
    if path.exists():
        for line in path.read_text(encoding="utf-8").splitlines():
            key, _, value = line.partition("=")
            if key:
                info[key] = value
    return info


def cell(text: str) -> str:
    return text.replace("|", "/").replace("\n", " ").strip() or "-"


def cmd_summary(args: argparse.Namespace) -> int:
    out = Path(args.out)
    info = read_info(out / "info.txt")
    stages = read_rows(out / "stages.tsv", 6)
    goldens = read_rows(out / "goldens.tsv", 6)
    cannot = read_rows(out / "cannot-run.txt", 2)
    fingerprints = [line for line in (out / "fingerprints.txt").read_text(encoding="utf-8").splitlines() if line] \
        if (out / "fingerprints.txt").exists() else []
    red = [row for row in stages if row[2] in ("FAIL", "PENDING")]
    skipped = [row for row in stages if row[2] == "SKIP"]
    manual = [row for row in stages if row[2] == "MANUAL"]
    recorded = info.get("goldens_recorded") == "1"
    if info.get("aborted") == "1":
        verdict = "RED (aborted before the end)"
    elif red:
        verdict = "RED: " + ", ".join(f"{row[0]} {row[1]} ({row[2]})" for row in red)
    elif recorded:
        verdict = "GOLDENS RECORDED: not a verdict until the changed goldens are signed off"
    elif skipped or info.get("only"):
        left_out = [row[0] for row in skipped] + ([f"only stages {info['only']}"] if info.get("only") else [])
        verdict = "GREEN, PARTIAL: not a release gate (" + "; ".join(left_out) + ")"
    else:
        verdict = "GREEN"
    if manual and not red:
        verdict += "; manual steps still open: " + ", ".join(f"{row[0]} {row[1]}" for row in manual)
    lines = [f"# MD/MM local release gate: {verdict}", ""]
    if recorded:
        lines += ["> **The goldens were re-recorded in this run** (`" + info.get("goldens_file", "")
                  + "`). Read `git diff` of that file; the new numbers need Radek's sign-off before they are committed "
                  "(doc/release/LOCAL-GATE.md, Goldens).", ""]
    identity = [
        ("Commit", f"{info.get('describe', '?')} ({info.get('commit', '?')}), branch {info.get('branch', '?')}, "
            f"{'working tree DIRTY' if info.get('dirty') == '1' else 'working tree clean'}"),
        ("Mode", info.get("mode", "?")),
        ("Build", f"{info.get('build_dir', '?')} ({info.get('arch', '?')}, {info.get('build_type') or '?'}, "
            f"ThinLTO {info.get('thinlto') or '?'}, DSP optimisation {info.get('dsp_optimised') or '?'})"),
        ("Host", f"{info.get('host', '?')}, macOS {info.get('macos', '?')}, {info.get('xcode', '?')}, "
            f"{info.get('cmake', '?')}"),
        ("MD ROM", f"{info.get('md_rom', 'none')} sha256 {info.get('md_rom_sha256', '-')}"),
        ("MM ROM", f"{info.get('mm_rom', 'none')} sha256 {info.get('mm_rom_sha256', '-')}"),
        ("Goldens", f"{info.get('goldens_file', '?')} sha256 {info.get('goldens_sha256', '-')}"
            f"{', MODIFIED against HEAD' if info.get('goldens_modified') == '1' else ''}"),
        ("Started", info.get("started", "?")),
        ("Total time", info.get("total", "?")),
        ("Run folder", str(out)),
    ]
    for label, value in identity:
        lines.append(f"- **{label}:** {value}")
    if fingerprints:
        lines += ["", "ROM fingerprints as the tests print them:"] + [f"- `{line}`" for line in fingerprints]
    lines += ["", "| Stage | Result | Time | Numbers | Failures and skips |", "|---|---|---|---|---|"]
    for stage_id, name, result, seconds, numbers, notes in stages:
        lines.append(f"| {cell(stage_id)} {cell(name)} | **{cell(result)}** | {cell(seconds)} s | {cell(numbers)} | "
                     f"{cell(notes)} |")
    if goldens:
        lines += ["", "## Goldens compared", "",
                  "| Scenario | Outputs | Speed-ups | Seconds | Result | Detail |", "|---|---|---|---|---|---|"]
        for row in goldens:
            lines.append("| " + " | ".join(cell(field) for field in row) + " |")
    if cannot:
        lines += ["", "## Known cannot-run-here (named, not counted as skipped)", ""]
        lines += [f"- `{name}`: {reason}" for name, reason in cannot]
    if skipped:
        lines += ["", "## Skipped stages", ""] + [f"- {row[0]} {row[1]}: {row[5]}" for row in skipped]
    manual_text = out / "manual.txt"
    if manual and manual_text.exists():
        lines += ["", "## Manual steps (the gate cannot do these; they are not part of the verdict until you have)", "",
            "```", manual_text.read_text(encoding="utf-8").rstrip("\n"), "```"]
    text = "\n".join(lines) + "\n"
    (out / "summary.md").write_text(text, encoding="utf-8")
    print(text, end="")
    return 1 if (red or info.get("aborted") == "1") else 0


# ------------------------------------------------------------------------------------------------ main
def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)

    run = sub.add_parser("run")
    run.add_argument("--timeout", type=int, default=0)
    run.add_argument("--log", required=True)
    run.add_argument("--cwd")
    run.add_argument("--append", action="store_true")
    run.add_argument("--unset", action="append", default=[])
    run.add_argument("command", nargs=argparse.REMAINDER)
    run.set_defaults(func=cmd_run)

    missing = sub.add_parser("ctest-missing")
    missing.set_defaults(func=cmd_ctest_missing)

    junit = sub.add_parser("junit")
    junit.add_argument("file")
    junit.add_argument("--tolerate", action="append", default=[], help="NAME=reason: a skip that is not red")
    junit.set_defaults(func=cmd_junit)

    goldens = sub.add_parser("goldens")
    goldens.add_argument("file")
    goldens.add_argument("--mode", choices=("compare", "record"), default="compare")
    goldens.add_argument("--defaults", default="", help="scenarios every goldens file must cover")
    goldens.add_argument("--extra", default="", help="more scenarios (record mode: new ones)")
    goldens.add_argument("--seconds", default="8")
    goldens.set_defaults(func=cmd_goldens)

    for name, func in (("capacity", cmd_capacity), ("journeys", cmd_journeys), ("pluginval", cmd_pluginval)):
        judge = sub.add_parser(name)
        judge.add_argument("file")
        judge.set_defaults(func=func)

    summary = sub.add_parser("summary")
    summary.add_argument("--out", required=True)
    summary.set_defaults(func=cmd_summary)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
