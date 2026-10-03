#!/usr/bin/env python3
"""Run the CJ-002 before/after scenarios sequentially and retain their evidence.

Examples:
    python tools/run_cj002.py --phase before --suite all
    python tools/run_cj002.py --phase after --suite handling --vehicle Taxi
    python tools/run_cj002.py --phase before --suite city --scenario crash --replace

The executable must already be built. This script never builds or changes game data.
Each invocation writes an independent JSON manifest under build/shots/cj002/<phase>.
Exit codes: 0 all processes passed, 1 one or more failed, 2 setup error, 130 interrupted.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
import re
from pathlib import Path
import shlex
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parent.parent
VEHICLES = (
    "Stinger", "Viper", "Bruiser", "Taxi", "Pickup", "Van", "Limo",
    "Ambulance", "Police", "Bus", "BoxTruck", "Semi", "FireTruck",
    "Garbage", "Sportbike", "Chopper", "Scooter",
)
CITY = ("crash", "derby", "chase", "foot", "day", "rampage")
INPUTS = ("ConcreteJungle.exe", "assets/data/vehicles.cfg", "src/vehicle_tests.cpp")


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
    parser.add_argument("--suite", choices=("city", "handling", "collision", "all"), default="all")
    parser.add_argument("--vehicle", choices=VEHICLES, help="run one class; requires --suite handling")
    parser.add_argument("--scenario", choices=CITY, help="run one city scenario; requires --suite city")
    parser.add_argument("--replace", action="store_true",
                        help="explicitly replace existing outputs for the selected cases")
    parser.add_argument("--run-name", help="save a new evidence set below the phase directory without replacing prior runs")
    parser.add_argument("--timeout", type=positive_seconds, default=900.0, metavar="SECONDS",
                        help="wall-clock timeout per process (default: 900 s)")
    parser.add_argument("--dry-run", action="store_true",
                        help="print the schedule and overwrite conflicts without launching or writing")
    args = parser.parse_args(argv)
    if args.run_name and not re.fullmatch(r"[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}", args.run_name):
        parser.error("--run-name must be 1-64 letters, digits, underscores or hyphens, starting with a letter or digit")
    if args.vehicle and args.suite != "handling":
        parser.error("--vehicle requires --suite handling")
    if args.scenario and args.suite != "city":
        parser.error("--scenario requires --suite city")
    return args


def make_schedule(args):
    directory = Path("build") / "shots" / "cj002" / args.phase
    if args.run_name:
        directory /= args.run_name
    cases = []

    def add(name, scenario, frames, every, vehicle=None, uncapped=False):
        shot = (directory / (name + ".png")).as_posix()
        command = ["./ConcreteJungle.exe", "--shot", shot, "--frames", str(frames),
                   "--every", str(every), "--scenario", scenario]
        if vehicle:
            command += ["--vehicle", vehicle]
        if uncapped:
            command += ["--uncapped"]
        cases.append({"name": name, "scenario": scenario, "vehicle": vehicle,
                      "argv": command, "log": (directory / (name + ".log")).as_posix(),
                      "shot": shot, "status": "pending", "exit_status": None})

    if args.suite in ("city", "all"):
        for scenario in ((args.scenario,) if args.scenario else CITY):
            add(scenario, scenario, 3600 if scenario == "rampage" else 1500,
                120 if scenario == "rampage" else 30)
    if args.suite in ("handling", "all"):
        for vehicle in ((args.vehicle,) if args.vehicle else VEHICLES):
            add("handling-" + vehicle, "handling", 14400, 120, vehicle, True)
    if args.suite in ("collision", "all"):
        add("crash-handling", "crash-handling", 14400, 30, uncapped=True)
    return cases


def existing_outputs(case):
    shot = ROOT / case["shot"]
    paths = [ROOT / case["log"], shot]
    # Both numbered screenshots and phase-labelled captures use this prefix.
    paths.extend(shot.parent.glob(shot.stem + "_*.png"))
    return sorted({path for path in paths if path.exists()})


def fingerprints():
    result = {}
    for name in INPUTS:
        digest = hashlib.sha256()
        with (ROOT / name).open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        result[name] = digest.hexdigest()
    return result


def git_state():
    result = {}
    for key, command in (("head", ["git", "rev-parse", "HEAD"]),
                         ("status_porcelain", ["git", "status", "--porcelain"])):
        try:
            completed = subprocess.run(command, cwd=ROOT, capture_output=True,
                                       text=True, encoding="utf-8", errors="replace", timeout=15)
            result[key] = completed.stdout.strip() if completed.returncode == 0 else None
            if completed.returncode:
                result[key + "_error"] = completed.stderr.strip()
        except (OSError, subprocess.TimeoutExpired) as exc:
            result[key] = None
            result[key + "_error"] = str(exc)
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


def log_summary(case):
    text = (ROOT / case["log"]).read_text(encoding="utf-8", errors="replace")
    summaries = [line for line in text.splitlines() if "CJTEST summary " in line]
    case["fixture_summaries"] = summaries
    case["failed_metrics"] = sum("CJTEST metric " in line and "result=FAIL" in line
                                  for line in text.splitlines())
    if case["scenario"] in ("handling", "crash-handling"):
        case["fixture_summary_present"] = bool(summaries)
        if case["status"] == "passed" and (not summaries or "result=PASS" not in summaries[-1]):
            case["status"] = "failed"
            case["error"] = "Missing or unsuccessful fixture summary despite zero process exit status."
    shot = ROOT / case["shot"]
    case["final_screenshot_present"] = shot.is_file()
    case["screenshot_count"] = sum(path.is_file() for path in shot.parent.glob(shot.stem + "_*.png"))
    case["screenshot_count"] += int(shot.is_file())
    if case["status"] == "passed" and not shot.is_file():
        case["status"] = "failed"
        case["error"] = "Process exited without its final screenshot."


def run_case(case, args, manifest, manifest_path):
    process = None
    started = time.monotonic()
    case["started_utc"] = utc_now()
    case["status"] = "running"
    interrupted = False
    try:
        case["sha256"] = fingerprints()
        case["git"] = git_state()
        case["executable"] = str(ROOT / "ConcreteJungle.exe")
        save_manifest(manifest_path, manifest)
        if args.replace:
            for path in existing_outputs(case):
                path.unlink()
        with (ROOT / case["log"]).open("wb") as log:
            # Windows resolves a relative executable before applying the child cwd.
            process = subprocess.Popen(case["argv"], executable=case["executable"], cwd=ROOT, stdout=log,
                                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
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
        if (ROOT / case["log"]).is_file():
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
            print("Refusing to overwrite evidence. Use --replace to rerun these cases explicitly.",
                  file=sys.stderr)
            return 2
    if args.dry_run:
        print("Dry run: no files written and no processes launched.")
        return 0
    for name in INPUTS:
        if not (ROOT / name).is_file():
            print("Required file is missing: %s" % name, file=sys.stderr)
            return 2

    output = ROOT / "build" / "shots" / "cj002" / args.phase
    if args.run_name:
        output /= args.run_name
    output.mkdir(parents=True, exist_ok=True)
    lock = ROOT / "build" / "cj002-runner.lock"
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

    manifest_path = output / ("manifest-%s-%s-%d.json" % (
        datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ"), args.suite, os.getpid()))
    manifest = {"schema": 1, "phase": args.phase, "suite": args.suite,
                "started_utc": utc_now(), "cwd": str(ROOT),
                "runner_argv": sys.argv if argv is None else [str(Path(__file__)), *argv],
                "timeout_seconds": args.timeout, "replace": args.replace,
                "status": "running", "cases": cases}
    interrupted = False
    try:
        manifest["git"] = git_state()
        manifest["sha256"] = fingerprints()
        save_manifest(manifest_path, manifest)
        print("Manifest: %s" % manifest_path, flush=True)
        for number, case in enumerate(cases, 1):
            print("Running %d/%d: %s" % (number, len(cases), case["name"]), flush=True)
            interrupted = run_case(case, args, manifest, manifest_path)
            print("  %s; exit=%s; %.1f s; log=%s" % (
                case["status"], case["exit_status"], case["elapsed_seconds"], case["log"]), flush=True)
            if interrupted:
                break
    except KeyboardInterrupt:
        interrupted = True
    except OSError as exc:
        manifest["error"] = str(exc)
        print("Suite error: %s" % exc, file=sys.stderr)
    finally:
        passed = sum(case["status"] == "passed" for case in cases)
        manifest["ended_utc"] = utc_now()
        manifest["passed"] = passed
        manifest["not_passed"] = len(cases) - passed
        manifest["status"] = "interrupted" if interrupted else (
            "passed" if passed == len(cases) else "failed")
        try:
            save_manifest(manifest_path, manifest)
        finally:
            lock.unlink()
    print("Suite %s: %d/%d passed. Manifest: %s" % (
        manifest["status"], passed, len(cases), manifest_path), flush=True)
    print("Process and fixture results are recorded; screenshots still require visual review.")
    return 130 if interrupted else (0 if passed == len(cases) else 1)


if __name__ == "__main__":
    sys.exit(main())
