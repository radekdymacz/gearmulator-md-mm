#!/usr/bin/env python3
"""Helpers of scripts/mdmm-local-gate.sh (doc/release/LOCAL-GATE.md).

Subcommands (every one that judges prints `numbers=...` and `notes=...` lines for the stage table and exits 0 for
green, 1 for red):

  run            run a command with a time limit and a log file; its process group dies with it
  audio-watch    note every process below a pid that holds an audio stream (macOS: coreaudiod's assertions)
  ctest-missing  ctest -N --show-only=json-v1 on stdin: registered tests whose program was not built
  junit          ctest's --output-junit file: executed, failed and skipped tests, by name
  goldens        the runs a goldens file asks for (scenario x outputs x speed-ups switch)
  soak           a performance capture (schema 2 JSON Lines) judged against the block budget
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


# ------------------------------------------------------------------------------------------------ audio-watch
def process_tree(root: int) -> dict[int, str]:
    """pid -> command name of root and everything below it."""
    listing = subprocess.run(["ps", "-A", "-o", "pid=,ppid=,comm="], capture_output=True, text=True).stdout
    children: dict[int, list[int]] = {}
    names: dict[int, str] = {}
    for line in listing.splitlines():
        parts = line.split(None, 2)
        if len(parts) == 3 and parts[0].isdigit() and parts[1].isdigit():
            children.setdefault(int(parts[1]), []).append(int(parts[0]))
            names[int(parts[0])] = os.path.basename(parts[2])
    tree, queue = {}, [root]
    while queue:
        pid = queue.pop()
        tree[pid] = names.get(pid, "?")
        queue.extend(children.get(pid, []))
    return tree


def audio_streams() -> list[tuple[int, str]]:
    """(pid, resources) of every process coreaudiod keeps awake for an open audio stream (pmset -g assertions)."""
    out = subprocess.run(["pmset", "-g", "assertions"], capture_output=True, text=True).stdout
    found, current = [], None
    for line in out.splitlines():
        if re.search(r"pid \d+\(coreaudiod\).*named: \"com\.apple\.audio\.", line):
            current = None
            continue
        created = re.match(r"\s+Created for PID: (\d+)", line)
        if created:
            current = int(created.group(1))
            continue
        resources = re.match(r"\s+Resources: (audio-\S+)", line)
        if resources and current is not None:
            found.append((current, resources.group(1)))
            current = None
    return found


def cmd_audio_watch(args: argparse.Namespace) -> int:
    """Until terminated: every `interval` seconds, append one line per new (pid, stream) held below --root-pid."""
    import time
    seen: set[tuple[int, str]] = set()
    stop = {"now": False}

    def on_term(_signum: int, _frame: object) -> None:
        stop["now"] = True

    signal.signal(signal.SIGTERM, on_term)
    signal.signal(signal.SIGINT, on_term)
    while True:
        tree = process_tree(args.root_pid)
        for pid, resource in audio_streams():
            if pid in tree and (pid, resource) not in seen:
                seen.add((pid, resource))
                with open(args.out, "a", encoding="utf-8") as out:
                    out.write(f"{tree[pid]} (pid {pid}) holds {resource}\n")
        if stop["now"]:
            return 0
        for _ in range(int(args.interval * 10)):
            if stop["now"]:
                break
            time.sleep(0.1)


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


# ------------------------------------------------------------------------------------------------ soak
BUCKETS = ("<25%", "25-50%", "50-75%", "75-100%", "100-150%", ">=150%")


def load_bucket(ratio: float) -> int:
    for index, edge in enumerate((0.25, 0.5, 0.75, 1.0, 1.5)):
        if ratio < edge:
            return index
    return 5


def read_capture(path: str) -> dict[str, object]:
    """A performance capture (doc/md_mm_performance_diagnostics.md, schema 2): the last summary, the callback trace, the end."""
    capture: dict[str, object] = {"summary": None, "callbacks": {}, "end": None, "session": None, "lines": 0}
    callbacks: dict[int, dict] = capture["callbacks"]  # type: ignore[assignment]
    with open(path, encoding="utf-8", errors="replace") as stream:
        for line in stream:
            try:
                record = json.loads(line)
            except ValueError:
                continue  # the last line of a capture that was cut off
            capture["lines"] = int(capture["lines"]) + 1  # type: ignore[call-overload]
            kind = record.get("type")
            if kind == "summary":
                capture["summary"] = record
            elif kind == "callback":
                callbacks[int(record["index"])] = record
            elif kind in ("end", "session"):
                capture[kind] = record
    return capture


def cmd_soak(args: argparse.Namespace) -> int:
    """Judge a capture against the block budget. Callbacks that start before --warmup seconds (the JIT compiling the
    firmware's hot paths, a first start's flash work) are listed, not judged; every other callback must be inside its budget
    (the two top buckets of the load histogram, minus the recorded warm-up ones) and wait for the synth lock less than --lock-us."""
    try:
        capture = read_capture(args.file)
    except OSError as error:
        emit(f"{args.machine}: no capture", str(error))
        return 1
    summary = capture["summary"]
    if not summary:
        emit(f"{args.machine}: no summary record in the capture", "the host did not run long enough or the capture never started")
        return 1
    histogram = list(summary["realtimeBudgetHistogram"])
    count = int(summary["outerHostCallbackCount"])
    elapsed = summary["elapsedNanoseconds"] / 1e9
    dropped = int(summary["slowCallbacksDropped"])
    callbacks = list(capture["callbacks"].values())  # type: ignore[union-attr]
    over_total = histogram[4] + histogram[5]
    warm_over = [cb for cb in callbacks if cb["startNanoseconds"] / 1e9 < args.warmup and cb["durationNanoseconds"] >= cb["budgetNanoseconds"]]
    # The histogram is cumulative from the start. The callbacks over budget are all in the trace (every callback at 75 % of its
    # budget or more is recorded) unless the trace dropped some: then the warm-up ones cannot be told apart, and all count.
    judged_over = over_total if dropped else max(0, over_total - len(warm_over))
    lock_ns = args.lock_us * 1000
    waits = sorted((cb for cb in callbacks if cb["startNanoseconds"] / 1e9 >= args.warmup and cb["lockWaitNanoseconds"] > lock_ns),
                   key=lambda cb: -cb["lockWaitNanoseconds"])
    after = [cb["lockWaitNanoseconds"] for cb in callbacks if cb["startNanoseconds"] / 1e9 >= args.warmup]
    max_wait_us = max(after) / 1000 if after else 0.0
    end = capture["end"]
    reason = end["reason"] if end else "no end record"
    problems = []
    if not end:
        problems.append("the capture has no end record: the host died or was killed")
    if int(summary["offlineCallbackCount"]):
        problems.append(f"{summary['offlineCallbackCount']} callbacks were offline: the histogram does not count them")
    if elapsed < args.min_seconds:
        problems.append(f"only {elapsed:.0f} s were captured (at least {args.min_seconds:.0f} s wanted; the capture ends at 10 minutes or 8 MiB)")
    if judged_over:
        worst = max((cb for cb in callbacks if cb["startNanoseconds"] / 1e9 >= args.warmup), key=lambda cb: cb["durationNanoseconds"], default=None)
        where = f"; worst at {worst['startNanoseconds'] / 1e9:.0f} s: {worst['durationNanoseconds'] / 1e6:.2f} ms of {worst['budgetNanoseconds'] / 1e6:.2f} ms" if worst else ""
        problems.append(f"{judged_over} callbacks over their budget after the first {args.warmup:.0f} s{where}")
    if waits:
        top = waits[0]
        problems.append(f"{len(waits)} callbacks waited more than {args.lock_us / 1000:.1f} ms for the synth lock after the first {args.warmup:.0f} s "
                        f"(worst {top['lockWaitNanoseconds'] / 1e6:.2f} ms at {top['startNanoseconds'] / 1e9:.0f} s)")
    if args.receipt:
        try:
            peak = float(json.loads(Path(args.receipt).read_text(encoding="utf-8")).get("audio_peak_before_quantization", 0.0))
        except (OSError, ValueError) as error:
            problems.append(f"the host's receipt is unreadable ({error})")
        else:
            if not peak > 1e-3:
                problems.append(f"the machine made no sound (output peak {peak:.1e}): the soak played nothing")
    result = "FAIL" if problems else "PASS"
    if args.tsv:
        with open(args.tsv, "a", encoding="utf-8") as out:
            out.write("\t".join([args.machine, f"{elapsed:.0f}", str(count), ",".join(str(h) for h in histogram), str(over_total),
                                  str(len(warm_over)), str(judged_over), f"{max_wait_us:.0f}", reason, result]) + "\n")
    numbers = (f"{args.machine} {count} callbacks in {elapsed:.0f} s, load " + ", ".join(f"{name} {value}" for name, value in zip(BUCKETS, histogram)))
    if args.brief:
        numbers = f"{args.machine} {count} callbacks, {over_total} over budget ({judged_over} after the first {args.warmup:.0f} s)"
    notes = list(problems)
    if warm_over:
        notes.append(f"{args.machine}: {len(warm_over)} callbacks over budget in the first {args.warmup:.0f} s (start-up JIT, listed not judged)")
    if dropped:
        notes.append(f"{args.machine}: the trace dropped {dropped} records, so warm-up callbacks could not be told apart")
    if args.blocks:
        notes.append(block_view(args))
    notes.append(f"{args.machine}: capture ended '{reason}', longest lock wait after warm-up {max_wait_us / 1000:.2f} ms")
    emit(numbers, "; ".join(note for note in notes if note))
    return 0 if args.info or not problems else 1


def block_view(args: argparse.Namespace) -> str:
    """The host's own view of the same callbacks (latency_host's .blocks.csv), as a second opinion: not judged."""
    import csv
    renders, over = [], 0
    try:
        with open(args.blocks, newline="", encoding="utf-8") as stream:
            for row in csv.DictReader(stream):
                if int(row["sample"]) / args.rate >= args.warmup:
                    value = float(row["render_ms"])
                    renders.append(value)
                    over += value > int(row["count"]) * 1000.0 / args.rate
    except (OSError, KeyError, ValueError) as error:
        return f"{args.machine}: host view unreadable ({error})"
    if not renders:
        return f"{args.machine}: host view empty"
    renders.sort()
    p50, p99 = renders[len(renders) // 2], renders[min(len(renders) - 1, int(len(renders) * 0.99))]
    return f"{args.machine} host view after warm-up: render p50 {p50:.2f} ms, p99 {p99:.2f} ms, max {renders[-1]:.2f} ms, {over} blocks over their period"


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
            notes.append(f"{model}: {gate['paced_render_overruns']} paced callbacks over their render budget (observation, not a gate)")
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
        notes.append(f"{len(names)} skipped for other reasons (tolerated): " + " ".join(names[:12]) + (" ..." if len(names) > 12 else ""))
    emit(totals.replace("JOURNEYS: ", ""), "; ".join(notes))
    return 1 if failed or problems or covered or "no totals" in totals else 0


# ------------------------------------------------------------------------------------------------ pluginval
def cmd_pluginval(args: argparse.Namespace) -> int:
    try:
        lines = Path(args.file).read_text(encoding="utf-8").splitlines()
    except OSError as error:
        emit("no pluginval summary", str(error))
        return 1
    rows = [[c.strip() for c in line.strip("|").split("|")] for line in lines if line.startswith("| ") and "Plug-in" not in line]
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


def audio_line(stages: list[list[str]]) -> str:
    """What the run did to the Mac's sound: the guard's verdict and the stages that muted the output."""
    opened = [row[0] for row in stages if "OPENED AN AUDIO DEVICE" in row[5]]
    muted = [row[0] for row in stages if "output muted for stage" in row[5] or "output turned down" in row[5]]
    guard = ("a stage opened an audio device: " + ", ".join(opened) + " (see the table)") if opened else \
        "the guard saw no audio stream in any stage it watched (every stage but 7d)"
    return guard + ("; output muted for stage " + ", ".join(muted) if muted else "; no stage needed the output muted")


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
        lines += ["> **The goldens were re-recorded in this run** (`" + info.get("goldens_file", "") + "`). Read `git diff` of that "
            "file; the new numbers need Radek's sign-off before they are committed (doc/release/LOCAL-GATE.md, Goldens).", ""]
    identity = [
        ("Commit", f"{info.get('describe', '?')} ({info.get('commit', '?')}), branch {info.get('branch', '?')}, "
            f"{'working tree DIRTY' if info.get('dirty') == '1' else 'working tree clean'}"),
        ("Mode", info.get("mode", "?")),
        ("Build", f"{info.get('build_dir', '?')} ({info.get('arch', '?')}, {info.get('build_type') or '?'}, "
            f"ThinLTO {info.get('thinlto') or '?'}, DSP optimisation {info.get('dsp_optimised') or '?'})"),
        ("Host", f"{info.get('host', '?')}, macOS {info.get('macos', '?')}, {info.get('xcode', '?')}, {info.get('cmake', '?')}"),
        ("MD ROM", f"{info.get('md_rom', 'none')} sha256 {info.get('md_rom_sha256', '-')}"),
        ("MM ROM", f"{info.get('mm_rom', 'none')} sha256 {info.get('mm_rom_sha256', '-')}"),
        ("Goldens", f"{info.get('goldens_file', '?')} sha256 {info.get('goldens_sha256', '-')}"
            f"{', MODIFIED against HEAD' if info.get('goldens_modified') == '1' else ''}"),
        ("Audio", audio_line(stages)),
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
        lines.append(f"| {cell(stage_id)} {cell(name)} | **{cell(result)}** | {cell(seconds)} s | {cell(numbers)} | {cell(notes)} |")
    if goldens:
        lines += ["", "## Goldens compared", "", "| Scenario | Outputs | Speed-ups | Seconds | Result | Detail |", "|---|---|---|---|---|---|"]
        for row in goldens:
            lines.append("| " + " | ".join(cell(field) for field in row) + " |")
    soak_rows = read_rows(out / "soak.tsv", 10)
    if soak_rows:
        lines += ["", "## Soak: the audio thread's load over the capture (callbacks per share of their block budget)", "",
                  "| Machine | Captured | Callbacks | " + " | ".join(BUCKETS) + " | Over budget after warm-up | Longest lock wait | Capture ended | Result |",
                  "|---|---|---|" + "---|" * len(BUCKETS) + "---|---|---|---|"]
        for machine, seconds, callbacks, histogram, _total, _warm, judged, wait_us, reason, result in soak_rows:
            buckets = histogram.split(",") + [""] * len(BUCKETS)
            lines.append(f"| {machine} | {seconds} s | {callbacks} | " + " | ".join(buckets[: len(BUCKETS)]) + f" | {judged} | {float(wait_us or 0) / 1000:.2f} ms | {reason} | **{result}** |")
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

    watch = sub.add_parser("audio-watch")
    watch.add_argument("--root-pid", type=int, required=True)
    watch.add_argument("--interval", type=float, default=2.0)
    watch.add_argument("--out", required=True)
    watch.set_defaults(func=cmd_audio_watch)

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

    soak = sub.add_parser("soak")
    soak.add_argument("file")
    soak.add_argument("--machine", default="?")
    soak.add_argument("--warmup", type=float, default=30.0)
    soak.add_argument("--lock-us", type=float, default=1000.0)
    soak.add_argument("--min-seconds", type=float, default=0.0)
    soak.add_argument("--tsv")
    soak.add_argument("--blocks")
    soak.add_argument("--rate", type=float, default=48000.0)
    soak.add_argument("--info", action="store_true", help="report only: always exit 0")
    soak.add_argument("--brief", action="store_true", help="numbers: the counts only")
    soak.add_argument("--receipt", help="latency_host's .json: the machine must have made a sound")
    soak.set_defaults(func=cmd_soak)

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
