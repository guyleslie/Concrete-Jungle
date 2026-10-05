#!/usr/bin/env python3
"""Retain before/after evidence for the separate CJ-016 clearance contract fixture.

Examples:
    python tools/run_cj016_clearance.py --phase before --run-name depth-contract
    python tools/run_cj016_clearance.py --phase after --run-name depth-contract
    python tools/run_cj016_clearance.py --phase before --dry-run

Build separately. This runner never builds or changes game data. It preserves failed
baseline acceptance as complete evidence when all eight cases and captures exist.
Exit codes: 0 accepted, 1 failed/incomplete fixture, 2 setup error, 130 interrupted.
The static guard cases measure API behaviour, not driving or CPU performance.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import time

# Shared helpers require no bytecode cache writes, including in a restricted workspace.
sys.dont_write_bytecode = True
from run_cj016 import (ROOT, existing_outputs, float_field, git_state, integer_field,
                       positive_seconds, record_fields, save_manifest, stop_process, utc_now)
from run_cj016 import INPUTS as RECOVERY_INPUTS


FIXTURE = "cj016-clearance-v1"
FRAMES = 420
PHASES = tuple(kind + "-" + rate
               for rate in ("60hz", "20hz")
               for kind in ("clear-nearby-boxes", "actual-overlap", "clearance-margin-only", "forward-blocker"))
API_CALLS = dict(zip(PHASES, (120, 30, 30, 30, 40, 10, 10, 10)))
PHYSICS_STEPS = dict(zip(PHASES, (120, 0, 0, 0, 40, 0, 0, 0)))
METRICS = ("api_expected", "api_pose_unchanged", "ownership_preserved")
INPUTS = RECOVERY_INPUTS + (
    "src/traffic_clearance_tests.cpp", "src/traffic_clearance_tests.h", "src/math_utils.h",
    "tools/run_cj016.py", "tools/run_cj016_clearance.py",
)
OPTIONAL_INPUTS = ("assets/data/traffic.cfg",)


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--phase", required=True, choices=("before", "after"))
    parser.add_argument("--run-name", help="retain a new evidence set below the phase directory")
    parser.add_argument("--replace", action="store_true",
                        help="explicitly replace existing selected log and screenshots")
    parser.add_argument("--timeout", type=positive_seconds, default=900.0, metavar="SECONDS",
                        help="wall-clock timeout (default: 900 s)")
    parser.add_argument("--dry-run", action="store_true",
                        help="print arguments and conflicts without launching or writing")
    args = parser.parse_args(argv)
    if args.run_name and not re.fullmatch(r"[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}", args.run_name):
        parser.error("--run-name must be 1-64 letters, digits, underscores or hyphens, starting with a letter or digit")
    return args


def make_case(args):
    directory = Path("build") / "shots" / "cj016-clearance" / args.phase
    if args.run_name:
        directory /= args.run_name
    shot = (directory / "traffic-clearance.png").as_posix()
    return {"name": "traffic-clearance", "scenario": "traffic-clearance", "vehicle": "Taxi",
            "argv": ["./ConcreteJungle.exe", "--scenario", "traffic-clearance", "--frames", str(FRAMES),
                     "--every", "30", "--uncapped", "--shot", shot],
            "log": (directory / "traffic-clearance.log").as_posix(), "shot": shot,
            "status": "pending", "exit_status": None}


def fingerprints():
    result = {}
    for name in INPUTS:
        path = ROOT / name
        if name in OPTIONAL_INPUTS and not path.is_file():
            result[name] = {"status": "absent", "sha256": None}
            continue
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        result[name] = {"status": "present", "sha256": digest.hexdigest()}
    return result


def log_summary(case):
    records = []
    for line in (ROOT / case["log"]).read_text(encoding="utf-8", errors="replace").splitlines():
        match = re.search(r"\bCJ016C ([a-z_]+)\b", line)
        if match:
            records.append({"kind": match.group(1), "line": line,
                            "fields": record_fields(line[match.start():])})
    runs = [record for record in records if record["kind"] == "run"]
    summaries = [record for record in records if record["kind"] == "summary"]
    metrics = [record for record in records if record["kind"] == "metric"]
    results = [record for record in records if record["kind"] == "result"]
    case["fixture_records"] = records
    case["fixture_runs"] = runs
    case["fixture_summaries"] = [record["line"] for record in summaries]
    case["fixture_summary_fields"] = [record["fields"] for record in summaries]
    case["fixture_metrics"] = metrics
    case["fixture_results"] = results
    case["seed_metadata"] = [record["fields"] for record in runs]
    case["failed_metrics"] = sum(record["fields"].get("result") == "FAIL" for record in metrics)
    case["failed_cases"] = sum(record["fields"].get("result") == "FAIL" for record in results)
    summary = summaries[-1]["fields"] if summaries else {}
    run = runs[-1]["fields"] if runs else {}
    expected_metrics = {(phase, name) for phase in PHASES for name in METRICS}
    expected_metrics.update((phase, "rejoined_s") for phase in PHASES if phase.startswith("clear-nearby-boxes-"))
    observed_metrics = {(record["fields"].get("phase"), record["fields"].get("name")) for record in metrics}
    phases_complete = len(results) == 8 and {record["fields"].get("phase") for record in results} == set(PHASES)
    for record in results:
        fields = record["fields"]
        phase = fields.get("phase")
        clear_case = phase is not None and phase.startswith("clear-nearby-boxes-")
        phase_metrics = [metric for metric in metrics if metric["fields"].get("phase") == phase]
        phases_complete = phases_complete and (
            fields.get("fixture") == FIXTURE and fields.get("class") == "Taxi"
            and phase in PHASES and integer_field(fields, "expected_clear") == int(clear_case)
            and integer_field(fields, "api_calls") == API_CALLS.get(phase)
            and integer_field(fields, "physics_steps") == PHYSICS_STEPS.get(phase)
            and integer_field(fields, "checks") == (4 if clear_case else 3)
            and integer_field(fields, "failures")
                == sum(metric["fields"].get("result") == "FAIL" for metric in phase_metrics)
        )
    case["fixture_completed"] = (
        len(runs) == 1 and len(summaries) == 1
        and run.get("fixture") == FIXTURE and run.get("scenario") == case["scenario"]
        and run.get("class") == "Taxi" and run.get("seed") == "000c0016"
        and integer_field(run, "planned_cases") == 8
        and summary.get("fixture") == FIXTURE and summary.get("class") == "Taxi"
        and summary.get("seed") == "000c0016"
        and integer_field(summary, "scheduled") == 8 and integer_field(summary, "completed") == 8
        and integer_field(summary, "render_frames") == FRAMES
        and integer_field(summary, "physics_steps") == 160 and integer_field(summary, "api_calls") == 280
        and float_field(summary, "simulated_s") == 7.0
        and integer_field(summary, "incomplete") == 0 and integer_field(summary, "invalid") == 0
        and integer_field(summary, "checks") == 26 and len(metrics) == 26
        and integer_field(summary, "failures") == case["failed_metrics"]
        and observed_metrics == expected_metrics and phases_complete
        and all(record["fields"].get("fixture") == FIXTURE and record["fields"].get("class") == "Taxi"
                and record["fields"].get("result") in ("PASS", "FAIL") for record in metrics + results)
    )
    case["fixture_accepted"] = (case["fixture_completed"] and summary.get("result") == "PASS"
                                and case["failed_metrics"] == 0 and case["failed_cases"] == 0)
    shot = ROOT / case["shot"]
    captures = {phase: shot.with_name(shot.stem + "_" + phase + ".png") for phase in PHASES}
    case["phase_captures"] = {phase: {"path": path.relative_to(ROOT).as_posix(), "present": path.is_file()}
                              for phase, path in captures.items()}
    case["phase_captures_complete"] = all(path.is_file() for path in captures.values())
    case["final_screenshot_present"] = shot.is_file()
    case["screenshot_count"] = sum(path.is_file() for path in shot.parent.glob(shot.stem + "_*.png"))
    case["screenshot_count"] += int(shot.is_file())
    case["evidence_complete"] = (case["exit_status"] in (0, 1) and case["fixture_completed"]
                                  and case["phase_captures_complete"] and case["final_screenshot_present"])
    if case["status"] == "passed" and not case["evidence_complete"]:
        case["status"] = "failed"
        case["error"] = "Incomplete fixture, phase captures or final screenshot despite zero process exit status."
    elif case["status"] == "passed" and not case["fixture_accepted"]:
        case["status"] = "failed"
        case["error"] = "Fixture acceptance failed; complete baseline evidence remains available."
    elif case["status"] == "failed" and case["evidence_complete"] and not case["fixture_accepted"]:
        case["error"] = "Fixture acceptance failed; complete baseline evidence remains available."


def run_case(case, args, manifest, manifest_path):
    process = None
    interrupted = False
    started = time.monotonic()
    case["status"] = "running"
    case["started_utc"] = utc_now()
    try:
        case["inputs"] = fingerprints()
        case["git"] = git_state()
        case["executable"] = str(ROOT / "ConcreteJungle.exe")
        save_manifest(manifest_path, manifest)
        if args.replace:
            evidence_root = (ROOT / "build" / "shots" / "cj016-clearance").resolve()
            for path in existing_outputs(case):
                if evidence_root not in path.resolve().parents:
                    raise OSError("Refusing to remove evidence outside %s: %s" % (evidence_root, path))
                path.unlink()
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
        if name not in OPTIONAL_INPUTS and not (ROOT / name).is_file():
            print("Required file is missing: %s" % name, file=sys.stderr)
            return 2
    output = (ROOT / case["shot"]).parent
    output.mkdir(parents=True, exist_ok=True)
    lock = ROOT / "build" / "cj016-clearance-runner.lock"
    try:
        descriptor = os.open(lock, os.O_WRONLY | os.O_CREAT | os.O_EXCL)
    except FileExistsError:
        print("Another clearance fixture may be running: %s. Check its process before removing a stale lock."
              % lock, file=sys.stderr)
        return 2
    except OSError as exc:
        print("Cannot create runner lock: %s" % exc, file=sys.stderr)
        return 2
    with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
        stream.write("pid=%d\nstarted_utc=%s\n" % (os.getpid(), utc_now()))
    manifest_path = output / ("manifest-%s-%d.json" % (
        datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ"), os.getpid()))
    manifest = {"schema": 1, "phase": args.phase, "fixture": FIXTURE, "started_utc": utc_now(),
                "cwd": str(ROOT), "runner_argv": sys.argv if argv is None else [str(Path(__file__)), *argv],
                "replace": args.replace, "timeout_seconds": args.timeout, "expected_classes": ["Taxi"],
                "plan": {"execution": "single_process", "frames": FRAMES, "expected_phases": list(PHASES),
                         "checks": 26, "api_calls_per_phase": API_CALLS, "physics_steps_per_phase": PHYSICS_STEPS,
                         "screenshot_every_frames": 30, "uncapped": True,
                         "guard_scope": "static API contract; no AI or physics in guard cases"},
                "status": "running", "cases": [case]}
    interrupted = False
    try:
        manifest["git"] = git_state()
        manifest["inputs"] = fingerprints()
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
        manifest["passed"] = int(case["status"] == "passed")
        manifest["evidence_complete"] = case.get("evidence_complete", False)
        manifest["status"] = "interrupted" if interrupted else (
            "passed" if case["status"] == "passed" else "failed")
        try:
            save_manifest(manifest_path, manifest)
        finally:
            lock.unlink()
    print("Fixture %s; exit=%s; failed_metrics=%s; evidence_complete=%s. Manifest: %s" % (
        manifest["status"], case["exit_status"], case.get("failed_metrics", "unknown"),
        manifest["evidence_complete"], manifest_path), flush=True)
    print("Complete failed baseline evidence remains available; screenshots require visual review.")
    return 130 if interrupted else (0 if case["status"] == "passed" else 1)


if __name__ == "__main__":
    sys.exit(main())
