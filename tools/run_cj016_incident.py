#!/usr/bin/env python3
"""Retain before/after evidence for the CJ-016 driver incident fixture.

Examples:
    python tools/run_cj016_incident.py --phase before --run-name incidents-20261006
    python tools/run_cj016_incident.py --phase after --run-name incidents-20261006
    python tools/run_cj016_incident.py --phase after --dry-run

Build separately. Both phases run a copy of assets/data/traffic.cfg written beside
the evidence and fingerprinted. It scripts the drivers' decision: an aggressive
driver always confronts (INCIDENT confront_chance 1) instead of the shipped chance.
"before" additionally sets INCIDENT enabled 0, the earlier rule under which no
driver ever reacts to a collision. The fixture runs 80 cases (4 situations x 10
seeds x 60/20 Hz) in one process and ends itself when the last case is complete.
Exit codes: 0 accepted, 1 failed/incomplete fixture, 2 setup error, 130 interrupted.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import time

sys.dont_write_bytecode = True
from run_cj016 import (ROOT, existing_outputs, git_state, integer_field, positive_seconds,
                       record_fields, save_manifest, stop_process, utc_now)
from run_cj016 import INPUTS as RECOVERY_INPUTS

FIXTURE = "cj016-incident-v2"
MAX_FRAMES = 400000
KINDS = ("aggressive-pair", "calm-pair", "aggressive-player", "interrupted")
CASES = tuple("%s-%s-s%d" % (kind, rate, seed)
              for rate in ("60hz", "20hz") for kind in KINDS for seed in range(10))
INPUTS = RECOVERY_INPUTS + (
    "src/traffic_incident_tests.cpp", "src/traffic_incident_tests.h", "src/traffic_incidents.cpp",
    "src/traffic_incidents.h", "src/pedestrian.cpp", "src/pedestrian.h", "src/math_utils.h",
    "tools/run_cj016.py", "tools/run_cj016_incident.py",
)


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--phase", required=True, choices=("before", "after"))
    parser.add_argument("--run-name", help="retain a new evidence set below the phase directory")
    parser.add_argument("--replace", action="store_true",
                        help="explicitly replace existing selected log and screenshots")
    parser.add_argument("--timeout", type=positive_seconds, default=1800.0, metavar="SECONDS",
                        help="wall-clock timeout (default: 1800 s)")
    parser.add_argument("--dry-run", action="store_true",
                        help="print arguments and conflicts without launching or writing")
    args = parser.parse_args(argv)
    if args.run_name and not re.fullmatch(r"[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}", args.run_name):
        parser.error("--run-name must be 1-64 letters, digits, underscores or hyphens, starting with a letter or digit")
    return args


def make_case(args):
    directory = Path("build") / "shots" / "cj016-incident" / args.phase
    if args.run_name:
        directory /= args.run_name
    shot = (directory / "traffic-incident.png").as_posix()
    config = (directory / ("traffic-%s.cfg" % args.phase)).as_posix()
    argv = ["./ConcreteJungle.exe", "--scenario", "traffic-incident", "--frames", str(MAX_FRAMES),
            "--uncapped", "--shot", shot, "--traffic-config", config]
    return {"name": "traffic-incident", "scenario": "traffic-incident", "argv": argv, "phase": args.phase,
            "log": (directory / "traffic-incident.log").as_posix(), "shot": shot,
            "traffic_config": config, "status": "pending", "exit_status": None}


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fingerprints(case):
    result = {name: {"status": "present", "sha256": sha256(ROOT / name)} for name in INPUTS}
    if case["traffic_config"]:
        result[case["traffic_config"]] = {"status": "present", "sha256": sha256(ROOT / case["traffic_config"])}
    return result


def write_config(case):
    """Copy the shipped traffic data: scripted confrontation, and before, no incidents."""
    text = (ROOT / "assets" / "data" / "traffic.cfg").read_text(encoding="utf-8")
    text, chance = re.subn(r"(?m)^INCIDENT\s+confront_chance\s+\S+\s*$", "INCIDENT confront_chance 1", text)
    enabled = "0" if case["phase"] == "before" else "1"
    text, count = re.subn(r"(?m)^INCIDENT\s+enabled\s+\S+\s*$", "INCIDENT enabled " + enabled, text)
    if chance != 1 or count != 1:
        raise OSError("assets/data/traffic.cfg must contain exactly one INCIDENT confront_chance and enabled record")
    (ROOT / case["traffic_config"]).write_text(text, encoding="utf-8")


def log_summary(case):
    records = []
    for line in (ROOT / case["log"]).read_text(encoding="utf-8", errors="replace").splitlines():
        match = re.search(r"\bCJ016I ([a-z_]+)\b", line)
        if match:
            records.append({"kind": match.group(1), "line": line, "fields": record_fields(line[match.start():])})
    runs = [r for r in records if r["kind"] == "run"]
    summaries = [r for r in records if r["kind"] == "summary"]
    metrics = [r for r in records if r["kind"] == "metric"]
    results = [r for r in records if r["kind"] == "result"]
    case["fixture_runs"] = [r["fields"] for r in runs]
    case["fixture_summaries"] = [r["line"] for r in summaries]
    case["fixture_results"] = [r["fields"] for r in results]
    case["failed_metrics"] = sum(r["fields"].get("result") == "FAIL" for r in metrics)
    case["failed_cases"] = sum(r["fields"].get("result") == "FAIL" for r in results)
    case["passed_cases"] = sum(r["fields"].get("result") == "PASS" for r in results)
    summary = summaries[-1]["fields"] if summaries else {}
    run = runs[-1]["fields"] if runs else {}
    observed = [r["fields"].get("case") for r in results]
    case["fixture_completed"] = (
        len(runs) == 1 and len(summaries) == 1 and run.get("fixture") == FIXTURE
        and run.get("seed") == "000c0016" and integer_field(run, "planned_cases") == len(CASES)
        and summary.get("fixture") == FIXTURE and integer_field(summary, "scheduled") == len(CASES)
        and integer_field(summary, "completed") == len(CASES) and observed == list(CASES)
        and integer_field(summary, "incomplete") == 0 and integer_field(summary, "invalid") == 0
        and integer_field(summary, "failures") == case["failed_metrics"]
        and integer_field(summary, "checks") == len(metrics)
    )
    case["fixture_accepted"] = (case["fixture_completed"] and summary.get("result") == "PASS"
                                and case["failed_metrics"] == 0)
    shot = ROOT / case["shot"]
    captures = {name: shot.with_name(shot.stem + "_" + name + ".png") for name in CASES}
    case["case_captures_complete"] = all(path.is_file() for path in captures.values())
    case["final_screenshot_present"] = shot.is_file()
    case["evidence_complete"] = (case["exit_status"] in (0, 1) and case["fixture_completed"]
                                  and case["case_captures_complete"] and case["final_screenshot_present"])
    if case["status"] == "passed" and not case["evidence_complete"]:
        case["status"] = "failed"
        case["error"] = "Incomplete fixture, case captures or final screenshot despite zero exit status."
    elif case["status"] == "passed" and not case["fixture_accepted"]:
        case["status"] = "failed"
        case["error"] = "Fixture acceptance failed; complete evidence remains available."
    elif case["status"] == "failed" and case["evidence_complete"] and not case["fixture_accepted"]:
        case["error"] = "Fixture acceptance failed; complete evidence remains available."


def run_case(case, args, manifest, manifest_path):
    process = None
    interrupted = False
    started = time.monotonic()
    case["status"] = "running"
    case["started_utc"] = utc_now()
    try:
        if args.replace:
            evidence_root = (ROOT / "build" / "shots" / "cj016-incident").resolve()
            for path in existing_outputs(case):
                if evidence_root not in path.resolve().parents:
                    raise OSError("Refusing to remove evidence outside %s: %s" % (evidence_root, path))
                path.unlink()
        write_config(case)
        case["inputs"] = fingerprints(case)
        case["git"] = git_state()
        case["executable"] = str(ROOT / "ConcreteJungle.exe")
        save_manifest(manifest_path, manifest)
        with (ROOT / case["log"]).open("wb") as log:
            process = subprocess.Popen(case["argv"], executable=case["executable"], cwd=ROOT,
                                       stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
            try:
                case["exit_status"] = process.wait(timeout=args.timeout)
                case["status"] = "passed" if case["exit_status"] == 0 else "failed"
            except subprocess.TimeoutExpired:
                case["status"] = "timed_out"
                case["error"] = "Exceeded the %.1f s wall-clock timeout." % args.timeout
                stop_process(process)
                case["exit_status"] = process.returncode
    except KeyboardInterrupt:
        interrupted = True
        case["status"] = "interrupted"
        if process is not None:
            stop_process(process)
            case["exit_status"] = process.returncode
    except OSError as exc:
        case["status"] = "error"
        case["error"] = str(exc)
    finally:
        if process is not None and process.poll() is None:
            stop_process(process)
            case["exit_status"] = process.returncode
        case["ended_utc"] = utc_now()
        case["elapsed_seconds"] = round(time.monotonic() - started, 3)
        if process is not None and (ROOT / case["log"]).is_file():
            log_summary(case)
        save_manifest(manifest_path, manifest)
    return interrupted


def main(argv=None):
    args = parse_args(argv)
    case = make_case(args)
    print(shlex.join(case["argv"]))
    conflicts = existing_outputs(case)
    if conflicts:
        print("Existing outputs: %d files." % len(conflicts), file=sys.stderr)
        if not args.replace:
            print("Refusing to overwrite evidence. Use --run-name for new evidence or --replace explicitly.",
                  file=sys.stderr)
            return 2
    if args.dry_run:
        print("Dry run: no files written and no processes launched.")
        return 0
    for name in INPUTS:
        if not (ROOT / name).is_file():
            print("Required file is missing: %s" % name, file=sys.stderr)
            return 2
    output = (ROOT / case["shot"]).parent
    output.mkdir(parents=True, exist_ok=True)
    lock = ROOT / "build" / "cj016-incident-runner.lock"
    try:
        descriptor = os.open(lock, os.O_WRONLY | os.O_CREAT | os.O_EXCL)
    except FileExistsError:
        print("Another incident fixture may be running: %s. Check its process before removing a stale lock."
              % lock, file=sys.stderr)
        return 2
    with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
        stream.write("pid=%d\nstarted_utc=%s\n" % (os.getpid(), utc_now()))
    manifest_path = output / ("manifest-%s-%d.json" % (
        datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ"), os.getpid()))
    manifest = {"schema": 1, "phase": args.phase, "fixture": FIXTURE, "started_utc": utc_now(),
                "cwd": str(ROOT), "runner_argv": sys.argv if argv is None else [str(Path(__file__)), *argv],
                "replace": args.replace, "timeout_seconds": args.timeout,
                "plan": {"execution": "single_process", "max_frames": MAX_FRAMES, "expected_cases": list(CASES),
                         "uncapped": True, "incidents": args.phase == "after", "confront_chance": 1,
                         "case_end": "settled + 2 s, or 60 s"},
                "status": "running", "cases": [case]}
    interrupted = False
    try:
        manifest["git"] = git_state()
        save_manifest(manifest_path, manifest)
        print("Manifest: %s" % manifest_path, flush=True)
        interrupted = run_case(case, args, manifest, manifest_path)
    except KeyboardInterrupt:
        interrupted = True
    except OSError as exc:
        manifest["error"] = str(exc)
        print("Fixture error: %s" % exc, file=sys.stderr)
    finally:
        manifest["ended_utc"] = utc_now()
        manifest["evidence_complete"] = case.get("evidence_complete", False)
        manifest["status"] = "interrupted" if interrupted else (
            "passed" if case["status"] == "passed" else "failed")
        try:
            save_manifest(manifest_path, manifest)
        finally:
            lock.unlink()
    print("Fixture %s; exit=%s; passed_cases=%s/%d; failed_metrics=%s; evidence_complete=%s. Manifest: %s" % (
        manifest["status"], case["exit_status"], case.get("passed_cases", "unknown"), len(CASES),
        case.get("failed_metrics", "unknown"), manifest["evidence_complete"], manifest_path), flush=True)
    return 130 if interrupted else (0 if case["status"] == "passed" else 1)


if __name__ == "__main__":
    sys.exit(main())
