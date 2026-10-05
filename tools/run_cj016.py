#!/usr/bin/env python3
"""Run the CJ-016 traffic recovery fixtures sequentially and retain their evidence.

Examples:
    python tools/run_cj016.py --phase before
    python tools/run_cj016.py --phase after --vehicle Taxi --run-name review-20261005
    python tools/run_cj016.py --phase before --dry-run

Build the executable separately. This script never builds or changes game data.
Failed baseline acceptance checks are recorded as failures, even with exit status 0.
Exit codes: 0 all fixtures accepted, 1 failed/incomplete fixtures, 2 setup error,
130 interrupted. Each invocation retains a timestamped JSON manifest.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parent.parent
VEHICLES = ("Taxi", "Bus", "BoxTruck")
FIXTURE = "cj016-recovery-v1"
FRAMES = 14400
CASES_PER_CLASS = 6
METRICS_PER_CLASS = 52
PHASES = ("enclosed-60hz", "free-60hz", "garage-60hz",
          "enclosed-20hz", "free-20hz", "garage-20hz")
TIMING_SAMPLES = dict(zip(PHASES, (3600, 1800, 1800, 1200, 600, 600)))
INPUTS = (
    "ConcreteJungle.exe", "assets/data/vehicles.cfg", "assets/data/traffic.cfg",
    "src/config.h", "src/traffic_tests.cpp", "src/traffic_tests.h",
    "src/traffic.cpp", "src/traffic.h", "src/traffic_recovery.cpp", "src/traffic_recovery.h",
    "src/vehicle.cpp", "src/vehicle.h", "src/vehicle_types.cpp", "src/vehicle_types.h",
    "src/physics.cpp", "src/physics.h", "src/game.cpp", "src/game.h", "src/main.cpp",
)
OPTIONAL_INPUTS = ("assets/data/traffic.cfg", "src/traffic_recovery.cpp", "src/traffic_recovery.h")


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def positive_seconds(value):
    try:
        seconds = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("timeout must be a number of seconds") from exc
    if not 0 < seconds < float("inf"):
        raise argparse.ArgumentTypeError("timeout must be finite and greater than zero")
    return seconds


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--phase", required=True, choices=("before", "after"))
    parser.add_argument("--vehicle", choices=VEHICLES, help="run only this class")
    parser.add_argument("--run-name", help="retain a new evidence set below the phase directory")
    parser.add_argument("--replace", action="store_true",
                        help="explicitly replace existing selected logs and screenshots")
    parser.add_argument("--timeout", type=positive_seconds, default=900.0, metavar="SECONDS",
                        help="wall-clock timeout per process (default: 900 s)")
    parser.add_argument("--dry-run", action="store_true",
                        help="print the schedule and conflicts without launching or writing")
    args = parser.parse_args(argv)
    if args.run_name and not re.fullmatch(r"[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}", args.run_name):
        parser.error("--run-name must be 1-64 letters, digits, underscores or hyphens, starting with a letter or digit")
    return args


def make_schedule(args):
    directory = Path("build") / "shots" / "cj016" / args.phase
    if args.run_name:
        directory /= args.run_name
    cases = []
    for vehicle in ((args.vehicle,) if args.vehicle else VEHICLES):
        name = "traffic-recovery-" + vehicle
        shot = (directory / (name + ".png")).as_posix()
        command = ["./ConcreteJungle.exe", "--scenario", "traffic-recovery",
                   "--vehicle", vehicle, "--frames", str(FRAMES), "--every", "120",
                   "--uncapped", "--shot", shot]
        cases.append({"name": name, "scenario": "traffic-recovery", "vehicle": vehicle,
                      "argv": command, "log": (directory / (name + ".log")).as_posix(),
                      "shot": shot, "status": "pending", "exit_status": None})
    return cases


def existing_outputs(case):
    shot = ROOT / case["shot"]
    paths = [ROOT / case["log"], shot]
    paths.extend(shot.parent.glob(shot.stem + "_*.png"))
    return sorted({path for path in paths if path.exists()})


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


def git_state():
    result = {}
    for key, command in (("head", ["git", "rev-parse", "HEAD"]),
                         ("status_porcelain", ["git", "status", "--porcelain=v1", "--untracked-files=all"])):
        try:
            completed = subprocess.run(command, cwd=ROOT, capture_output=True,
                                       text=True, encoding="utf-8", errors="replace", timeout=15)
            result[key] = completed.stdout.rstrip("\r\n") if completed.returncode == 0 else None
            if completed.returncode:
                result[key + "_error"] = completed.stderr.strip()
        except (OSError, subprocess.TimeoutExpired) as exc:
            result[key] = None
            result[key + "_error"] = str(exc)
    result["dirty"] = bool(result["status_porcelain"]) if result["status_porcelain"] is not None else None
    return result


def save_manifest(path, manifest):
    temporary = path.with_suffix(".json.tmp")
    with temporary.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(manifest, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    temporary.replace(path)


def stop_process(process):
    """Wait for the direct game process to exit before another window can start."""
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def record_fields(line):
    """Retain emitted string values, including seed spelling and floating-point dt."""
    try:
        tokens = shlex.split(line)
    except ValueError:
        return {}
    return dict(token.split("=", 1) for token in tokens if "=" in token)


def integer_field(fields, name):
    try:
        return int(fields[name])
    except (KeyError, TypeError, ValueError):
        return None


def float_field(fields, name):
    try:
        return float(fields[name])
    except (KeyError, TypeError, ValueError):
        return None


def nonnegative_finite_field(fields, name):
    value = float_field(fields, name)
    return value is not None and math.isfinite(value) and value >= 0


def log_summary(case):
    lines = (ROOT / case["log"]).read_text(encoding="utf-8", errors="replace").splitlines()
    records = []
    for line in lines:
        match = re.search(r"\bCJ016 ([a-z_]+)\b", line)
        if match:
            records.append({"kind": match.group(1), "line": line,
                            "fields": record_fields(line[match.start():])})
    runs = [record for record in records if record["kind"] == "run"]
    summaries = [record for record in records if record["kind"] == "summary"]
    metrics = [record for record in records if record["kind"] == "metric"]
    results = [record for record in records if record["kind"] == "result"]
    timings = [record for record in records if record["kind"] == "timing"]
    diagnostics = [record for record in records if record["kind"] == "diagnostics"]
    case["fixture_runs"] = runs
    case["fixture_summaries"] = [record["line"] for record in summaries]
    case["fixture_summary_fields"] = [record["fields"] for record in summaries]
    case["fixture_results"] = results
    case["fixture_metrics"] = metrics
    case["fixture_timings"] = timings
    case["fixture_diagnostics"] = diagnostics
    case["failed_metrics"] = sum(record["fields"].get("result") == "FAIL" for record in metrics)
    case["failed_cases"] = sum(record["fields"].get("result") == "FAIL" for record in results)
    case["seed_metadata"] = [record["fields"] for record in runs]
    case["fixture_summary_present"] = bool(summaries)
    summary = summaries[-1]["fields"] if summaries else {}
    run = runs[-1]["fields"] if runs else {}
    case["timing_complete"] = (
        len(timings) == CASES_PER_CLASS
        and {record["fields"].get("phase") for record in timings} == set(PHASES)
        and all(record["fields"].get("fixture") == FIXTURE
                and record["fields"].get("class") == case["vehicle"]
                and integer_field(record["fields"], "samples")
                    == TIMING_SAMPLES.get(record["fields"].get("phase"))
                and all(nonnegative_finite_field(record["fields"], name)
                        for name in ("ai_avg_ms", "ai_p95_ms", "physics_avg_ms"))
                for record in timings)
    )
    case["fixture_completed"] = (
        len(runs) == 1 and len(summaries) == 1
        and run.get("fixture") == FIXTURE and run.get("class") == case["vehicle"]
        and run.get("scenario") == case["scenario"] and bool(run.get("seed"))
        and integer_field(run, "planned_cases") == CASES_PER_CLASS
        and summary.get("fixture") == FIXTURE and summary.get("class") == case["vehicle"]
        and integer_field(summary, "scheduled") == CASES_PER_CLASS
        and integer_field(summary, "completed") == CASES_PER_CLASS
        and integer_field(summary, "incomplete") == 0
        and integer_field(summary, "invalid") == 0
        and integer_field(summary, "render_frames") == FRAMES
        and integer_field(summary, "physics_steps") == 9600
        and float_field(summary, "simulated_s") == 240.0
        and len(results) == CASES_PER_CLASS
        and {record["fields"].get("phase") for record in results} == set(PHASES)
        and len(metrics) == METRICS_PER_CLASS
        and integer_field(summary, "checks") == len(metrics)
        and integer_field(summary, "failures") == case["failed_metrics"]
        and case["timing_complete"]
        and all(record["fields"].get("fixture") == FIXTURE
                and record["fields"].get("class") == case["vehicle"] for record in results)
        and all(record["fields"].get("fixture") == FIXTURE
                and record["fields"].get("class") == case["vehicle"]
                and record["fields"].get("phase") in PHASES for record in metrics)
    )
    case["fixture_accepted"] = (case["fixture_completed"]
                                and summary.get("result") == "PASS"
                                and integer_field(summary, "failures") == 0
                                and integer_field(summary, "invalid") == 0
                                and all(record["fields"].get("result") == "PASS" for record in metrics)
                                and all(record["fields"].get("result") == "PASS" for record in results))
    shot = ROOT / case["shot"]
    case["final_screenshot_present"] = shot.is_file()
    case["screenshot_count"] = sum(path.is_file() for path in shot.parent.glob(shot.stem + "_*.png"))
    case["screenshot_count"] += int(shot.is_file())
    # The harness exits 1 for measured acceptance failures; it still completed its evidence.
    case["evidence_complete"] = (case["exit_status"] in (0, 1) and case["fixture_completed"]
                                  and case["final_screenshot_present"])
    if case["status"] == "passed" and not case["evidence_complete"]:
        case["status"] = "failed"
        case["error"] = "Incomplete fixture or missing final screenshot despite zero process exit status."
    elif case["status"] == "passed" and not case["fixture_accepted"]:
        case["status"] = "failed"
        case["error"] = "Fixture acceptance failed; completed baseline evidence remains available."
    elif case["status"] == "failed" and case["evidence_complete"] and not case["fixture_accepted"]:
        case["error"] = "Fixture acceptance failed; completed baseline evidence remains available."


def run_case(case, args, manifest, manifest_path):
    process = None
    started = time.monotonic()
    case["started_utc"] = utc_now()
    case["status"] = "running"
    interrupted = False
    try:
        case["inputs"] = fingerprints()
        case["git"] = git_state()
        case["executable"] = str(ROOT / "ConcreteJungle.exe")
        save_manifest(manifest_path, manifest)
        if args.replace:
            evidence_root = (ROOT / "build" / "shots" / "cj016").resolve()
            for path in existing_outputs(case):
                # Only remove the selected evidence files, within this runner's output tree.
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
    cases = make_schedule(args)
    conflicts = [(case["name"], existing_outputs(case)) for case in cases]
    conflicts = [(name, paths) for name, paths in conflicts if paths]
    for number, case in enumerate(cases, 1):
        print("[%d/%d] %s" % (number, len(cases), shlex.join(case["argv"])))
    if conflicts:
        for name, paths in conflicts:
            print("Existing output: %s (%d files)" % (name, len(paths)), file=sys.stderr)
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

    output = ROOT / "build" / "shots" / "cj016" / args.phase
    if args.run_name:
        output /= args.run_name
    output.mkdir(parents=True, exist_ok=True)
    lock = ROOT / "build" / "cj016-runner.lock"
    try:
        descriptor = os.open(lock, os.O_WRONLY | os.O_CREAT | os.O_EXCL)
    except FileExistsError:
        print("Another suite may be running: %s. Check its process before removing a stale lock."
              % lock, file=sys.stderr)
        return 2
    except OSError as exc:
        print("Cannot create runner lock: %s" % exc, file=sys.stderr)
        return 2
    with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
        stream.write("pid=%d\nstarted_utc=%s\n" % (os.getpid(), utc_now()))

    manifest_path = output / ("manifest-%s-%d.json" % (
        datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ"), os.getpid()))
    manifest = {"schema": 1, "phase": args.phase, "fixture": FIXTURE,
                "started_utc": utc_now(), "cwd": str(ROOT),
                "runner_argv": sys.argv if argv is None else [str(Path(__file__)), *argv],
                "timeout_seconds": args.timeout, "replace": args.replace,
                "expected_classes": [case["vehicle"] for case in cases],
                "plan": {"execution": "sequential", "maximum_concurrent_processes": 1,
                         "frames_per_class": FRAMES, "cases_per_class": CASES_PER_CLASS,
                         "metrics_per_class": METRICS_PER_CLASS,
                         "expected_phases": list(PHASES), "physics_steps_per_class": 9600,
                         "simulated_seconds_per_class": 240,
                         "timing_samples_per_phase": TIMING_SAMPLES,
                         "screenshot_every_frames": 120, "uncapped": True},
                "status": "running", "cases": cases}
    interrupted = False
    try:
        manifest["git"] = git_state()
        manifest["inputs"] = fingerprints()
        save_manifest(manifest_path, manifest)
        print("Manifest: %s" % manifest_path, flush=True)
        for number, case in enumerate(cases, 1):
            print("Running %d/%d: %s" % (number, len(cases), case["name"]), flush=True)
            interrupted = run_case(case, args, manifest, manifest_path)
            print("  %s; exit=%s; %.1f s; failed_metrics=%s; evidence_complete=%s; log=%s" % (
                case["status"], case["exit_status"], case["elapsed_seconds"],
                case.get("failed_metrics", "unknown"), case.get("evidence_complete", False),
                case["log"]), flush=True)
            if interrupted:
                break
    except KeyboardInterrupt:
        interrupted = True
    except OSError as exc:
        manifest["error"] = str(exc)
        print("Suite error: %s" % exc, file=sys.stderr)
    finally:
        passed = sum(case["status"] == "passed" for case in cases)
        complete = sum(case.get("evidence_complete", False) for case in cases)
        manifest["ended_utc"] = utc_now()
        manifest["passed"] = passed
        manifest["not_passed"] = len(cases) - passed
        manifest["evidence_complete_classes"] = complete
        manifest["evidence_complete"] = complete == len(cases)
        manifest["status"] = "interrupted" if interrupted else (
            "passed" if passed == len(cases) else "failed")
        try:
            save_manifest(manifest_path, manifest)
        finally:
            lock.unlink()
    print("Suite %s: %d/%d accepted, %d/%d complete evidence. Manifest: %s" % (
        manifest["status"], passed, len(cases), complete, len(cases), manifest_path), flush=True)
    print("Failed baseline checks remain failures. Screenshots still require visual review.")
    return 130 if interrupted else (0 if passed == len(cases) else 1)


if __name__ == "__main__":
    sys.exit(main())
