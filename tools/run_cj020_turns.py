#!/usr/bin/env python3
"""Retain before/after evidence for CJ-020 (turning kinematics of traffic).

Examples:
    python tools/run_cj020_turns.py --phase before --run-name baseline
    python tools/run_cj020_turns.py --phase after --run-name turns
    python tools/run_cj020_turns.py --phase after --suite fixture --dry-run

Build separately; "before" and "after" are builds of different revisions, recorded with
their git state and input hashes. The suite runs the isolated traffic-turns fixture
(every traffic class turning right, left and going straight through an empty junction
at 60 Hz and 20 Hz) and then the six city scenarios with their TRAFFIC slip, rail
overlap, wait and decision CPU lines. One game window at a time.
Exit codes: 0 all runs completed and the fixture accepted, 1 failed or incomplete,
2 setup error, 130 interrupted.
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

FIXTURE = "cj020-turns-v1"
FIXTURE_FRAMES = 400000
CITY = (("day", 1800), ("chase", 1800), ("drive", 1800), ("overview", 1800), ("crash", 1500), ("rampage", 3600))
CITY_LINES = ("TRAFFIC slip", "TRAFFIC rail overlaps", "TRAFFIC:", "TRAFFIC stopped because", "TRAFFIC wait cycles",
              "TRAFFIC yielding", "DRIVER DECISION CPU", "RECOVERY WORK", "PHYS: traffic knocked", "SHOT:", "RECOVERY CPU")
INPUTS = (
    "ConcreteJungle.exe", "assets/data/vehicles.cfg", "assets/data/traffic.cfg",
    "src/traffic.cpp", "src/traffic.h", "src/traffic_recovery.cpp", "src/traffic_recovery.h",
    "src/traffic_turn_tests.cpp", "src/traffic_turn_tests.h", "src/vehicle_types.cpp", "src/vehicle_types.h",
    "src/game.cpp", "src/game.h", "src/main.cpp", "tools/run_cj016.py", "tools/run_cj020_turns.py",
)


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--phase", required=True, choices=("before", "after"))
    parser.add_argument("--run-name", required=True, help="evidence set below the phase directory")
    parser.add_argument("--suite", choices=("all", "fixture", "city"), default="all")
    parser.add_argument("--replace", action="store_true", help="explicitly replace existing outputs")
    parser.add_argument("--timeout", type=positive_seconds, default=1800.0, metavar="SECONDS",
                        help="wall-clock timeout per run (default: 1800 s)")
    parser.add_argument("--dry-run", action="store_true", help="print the runs without launching or writing")
    args = parser.parse_args(argv)
    if not re.fullmatch(r"[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}", args.run_name):
        parser.error("--run-name must be 1-64 letters, digits, underscores or hyphens")
    return args


def make_runs(args):
    directory = Path("build") / "shots" / "cj020" / args.phase / args.run_name
    runs = []
    if args.suite in ("all", "fixture"):
        shot = (directory / "traffic-turns.png").as_posix()
        runs.append({"name": "traffic-turns", "argv": ["./ConcreteJungle.exe", "--scenario", "traffic-turns", "--frames",
                                                       str(FIXTURE_FRAMES), "--uncapped", "--shot", shot],
                     "log": (directory / "traffic-turns.log").as_posix(), "shot": shot, "fixture": True})
    if args.suite in ("all", "city"):
        for name, frames in CITY:
            shot = (directory / (name + ".png")).as_posix()
            runs.append({"name": name, "argv": ["./ConcreteJungle.exe", "--scenario", name, "--frames", str(frames),
                                                "--every", "300", "--uncapped", "--shot", shot],
                         "log": (directory / (name + ".log")).as_posix(), "shot": shot, "fixture": False})
    for run in runs:
        run.update({"status": "pending", "exit_status": None})
    return runs


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def summarize(run):
    text = (ROOT / run["log"]).read_text(encoding="utf-8", errors="replace").splitlines()
    if not run["fixture"]:
        run["lines"] = [line.split("INFO: ", 1)[-1] for line in text if any(key in line for key in CITY_LINES)]
        run["evidence_complete"] = run["exit_status"] == 0 and any("TRAFFIC slip" in l for l in run["lines"])
        return
    records = []
    for line in text:
        match = re.search(r"\bCJ020T ([a-z_]+)\b", line)
        if match:
            records.append({"kind": match.group(1), "fields": record_fields(line[match.start():]), "line": line})
    runs = [r["fields"] for r in records if r["kind"] == "run"]
    summaries = [r["fields"] for r in records if r["kind"] == "summary"]
    metrics = [r["fields"] for r in records if r["kind"] == "metric"]
    results = [r["fields"] for r in records if r["kind"] == "result"]
    scheduled = [r["fields"].get("case") for r in records if r["kind"] == "scheduled"]
    summary = summaries[-1] if summaries else {}
    run["failed_metrics"] = sum(m.get("result") == "FAIL" for m in metrics)
    run["passed_cases"] = sum(r.get("result") == "PASS" for r in results)
    run["cases"] = len(results)
    run["failed_by_metric"] = {}
    for m in metrics:
        if m.get("result") == "FAIL":
            run["failed_by_metric"][m.get("name")] = run["failed_by_metric"].get(m.get("name"), 0) + 1
    run["fixture_completed"] = (
        len(runs) == 1 and runs[0].get("fixture") == FIXTURE and len(summaries) == 1
        and [r.get("case") for r in results] == scheduled and integer_field(summary, "incomplete") == 0
        and integer_field(summary, "invalid") == 0 and integer_field(summary, "checks") == len(metrics))
    shot = ROOT / run["shot"]
    run["case_captures_complete"] = all(shot.with_name(shot.stem + "_" + c + ".png").is_file() for c in scheduled)
    run["fixture_accepted"] = run["fixture_completed"] and summary.get("result") == "PASS"
    run["evidence_complete"] = (run["exit_status"] in (0, 1) and run["fixture_completed"]
                                and run["case_captures_complete"] and shot.is_file())


def execute(run, args, manifest, manifest_path):
    process = None
    started = time.monotonic()
    run["status"] = "running"
    run["started_utc"] = utc_now()
    try:
        if args.replace:
            root = (ROOT / "build" / "shots" / "cj020").resolve()
            for path in existing_outputs(run):
                if root not in path.resolve().parents:
                    raise OSError("Refusing to remove evidence outside %s: %s" % (root, path))
                path.unlink()
        save_manifest(manifest_path, manifest)
        env = dict(os.environ, CJ_TEST_REVISION=(manifest["git"].get("head") or "UNKNOWN")[:12])
        with (ROOT / run["log"]).open("wb") as log:
            process = subprocess.Popen(run["argv"], executable=str(ROOT / "ConcreteJungle.exe"), cwd=ROOT, env=env,
                                       stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
            try:
                run["exit_status"] = process.wait(timeout=args.timeout)
                run["status"] = "completed"
            except subprocess.TimeoutExpired:
                run["status"] = "timed_out"
                stop_process(process)
                run["exit_status"] = process.returncode
    except KeyboardInterrupt:
        run["status"] = "interrupted"
        if process is not None:
            stop_process(process)
        raise
    except OSError as exc:
        run["status"] = "error"
        run["error"] = str(exc)
    finally:
        if process is not None and process.poll() is None:
            stop_process(process)
        run["ended_utc"] = utc_now()
        run["elapsed_seconds"] = round(time.monotonic() - started, 3)
        if (ROOT / run["log"]).is_file() and run["status"] == "completed":
            summarize(run)
        save_manifest(manifest_path, manifest)


def main(argv=None):
    args = parse_args(argv)
    runs = make_runs(args)
    for run in runs:
        print(shlex.join(run["argv"]))
    conflicts = [p for run in runs for p in existing_outputs(run)]
    if conflicts and not args.replace:
        print("Existing outputs: %d files. Use a new --run-name or --replace." % len(conflicts), file=sys.stderr)
        return 2
    if args.dry_run:
        print("Dry run: no files written and no processes launched.")
        return 0
    for name in INPUTS:
        if not (ROOT / name).is_file():
            print("Required file is missing: %s" % name, file=sys.stderr)
            return 2
    output = (ROOT / runs[0]["shot"]).parent
    output.mkdir(parents=True, exist_ok=True)
    manifest_path = output / ("manifest-%s-%d.json" % (
        datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ"), os.getpid()))
    manifest = {"schema": 1, "phase": args.phase, "fixture": FIXTURE, "suite": args.suite, "started_utc": utc_now(),
                "runner_argv": sys.argv if argv is None else [str(Path(__file__)), *argv],
                "inputs": {name: sha256(ROOT / name) for name in INPUTS}, "git": git_state(),
                "status": "running", "runs": runs}
    save_manifest(manifest_path, manifest)
    print("Manifest: %s" % manifest_path, flush=True)
    interrupted = False
    try:
        for run in runs:
            execute(run, args, manifest, manifest_path)
            if run["fixture"]:
                print("%s: exit=%s passed_cases=%s/%s failed_metrics=%s %s" % (
                    run["name"], run["exit_status"], run.get("passed_cases"), run.get("cases"),
                    run.get("failed_metrics"), run.get("failed_by_metric")), flush=True)
            else:
                for line in run.get("lines", []):
                    if line.startswith("TRAFFIC slip") or line.startswith("TRAFFIC rail overlaps"):
                        print("%s: %s" % (run["name"], line), flush=True)
    except KeyboardInterrupt:
        interrupted = True
    finally:
        manifest["ended_utc"] = utc_now()
        complete = all(r.get("evidence_complete") for r in runs)
        accepted = all(r.get("fixture_accepted", True) for r in runs)
        manifest["evidence_complete"] = complete
        manifest["status"] = "interrupted" if interrupted else ("passed" if complete and accepted else "failed")
        save_manifest(manifest_path, manifest)
    print("Suite %s; evidence_complete=%s. Manifest: %s" % (manifest["status"], manifest["evidence_complete"],
                                                           manifest_path), flush=True)
    return 130 if interrupted else (0 if manifest["status"] == "passed" else 1)


if __name__ == "__main__":
    sys.exit(main())
