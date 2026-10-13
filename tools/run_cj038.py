#!/usr/bin/env python3
"""Retain CJ-038 attention/evasion evidence, one visible game window at a time.

Build separately. Use --phase before|after --run-name NAME. The default suite runs
evasion and city; other suites are explicit. --scenario narrows a city run.
Existing evidence is never overwritten. Baseline failures remain visible;
exit 1 means failed acceptance or incomplete evidence in either phase.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

sys.dont_write_bytecode = True
from run_cj016 import ROOT, git_state, positive_seconds, record_fields, utc_now

INPUTS = (
    "assets/data/civilians.cfg", "assets/data/impacts.cfg", "assets/data/vehicles.cfg",
    "assets/data/traffic.cfg", "src/pedestrian.cpp", "src/pedestrian.h",
    "src/assets.cpp", "src/assets.h", "src/person_hit_tests.cpp",
    "tests/person-evasion.cfg", "tests/person-hits.cfg", "tools/run_cj038.py",
    "src/traffic.cpp", "src/traffic_turn_tests.cpp", "src/traffic_turn_tests.h",
    "src/game.cpp", "src/main.cpp", "tests/traffic-turn-pedestrians.cfg",
)
CITY = (("foot", 1500), ("day", 1500), ("rampage", 3600))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def summarize(run, phase):
    lines = (ROOT / run["log"]).read_text(encoding="utf-8", errors="replace").splitlines()
    run["lines"] = [l.split("INFO: ", 1)[-1] for l in lines if any(k in l for k in
                    ("PEDS:", "TIMING:", "SHOT:", "PERSONHIT summary", "PERSONHIT result"))]
    shot_exists = (ROOT / run["shot"]).is_file()
    if run["suite"] in ("turns", "turn-peds"):
        run["lines"] += [l.split("INFO: ", 1)[-1] for l in lines if any(k in l for k in
                         ("CJ020T summary", "CJ038T result"))]
        summaries = [record_fields(l) for l in lines if "CJ020T summary" in l]
        summary = summaries[-1] if len(summaries) == 1 else {}
        run["summary"] = summary
        run["complete"] = (run["exit"] in (0, 1) and shot_exists and summary.get("incomplete") == "0"
                           and summary.get("invalid") == "0" and summary.get("scheduled") == summary.get("completed"))
        run["accepted"] = run["complete"] and summary.get("failures") == "0"
        return
    if run["suite"] != "city":
        summaries = [record_fields(l) for l in lines if "PERSONHIT summary" in l]
        summary = summaries[-1] if len(summaries) == 1 else {}
        run["summary"] = summary
        run["complete"] = (run["exit"] in (0, 1) and shot_exists and summary.get("finished") == "1"
                           and summary.get("invalid") == "0" and summary.get("runs") == summary.get("completed"))
        run["accepted"] = run["complete"] and summary.get("failed") == "0"
        return
    hits = [re.search(r"hit by traffic (\d+), by the player (\d+).*path (\d+), of them hit (\d+)", l)
            for l in lines if "PEDS: hit by traffic" in l]
    timing = [re.search(r"pedestrians ([0-9.]+) ms", l) for l in lines if "TIMING:" in l]
    run["complete"] = (run["exit"] == 0 and shot_exists and len(hits) == len(timing) == 1
                       and hits[0] is not None and timing[0] is not None
                       and any("SHOT:" in l and "frames" in l for l in lines))
    if not run["complete"]:
        run["accepted"] = False
        return
    traffic, player, threatened, hit = map(int, hits[0].groups())
    run.update(traffic_hits=traffic, player_hits=player, threatened=threatened, threat_hits=hit,
               escaped_percent=100 * (threatened - hit) / threatened if threatened else None,
               pedestrian_cpu_ms=float(timing[0].group(1)))
    criteria = run["pedestrian_cpu_ms"] <= 0.5
    if run["scenario"] == "rampage":
        # Old acceptance is retained for the baseline; CJ-038 intentionally replaces it.
        criteria &= threatened > 0 and (50 <= run["escaped_percent"] <= 65 if phase == "after"
                                        else run["escaped_percent"] >= 70)
    else:
        criteria &= traffic == 0
    run["accepted"] = criteria


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--phase", required=True, choices=("before", "after"))
    parser.add_argument("--run-name", required=True)
    parser.add_argument("--suite", choices=("all", "city", "evasion", "impacts", "turns", "turn-peds"), default="all")
    parser.add_argument("--scenario", choices=tuple(name for name, _ in CITY),
                        help="run only this city scenario (requires --suite city)")
    parser.add_argument("--exe", type=Path, default=Path("ConcreteJungle.exe"))
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--timeout", type=positive_seconds, default=1800)
    args = parser.parse_args()
    if args.scenario and args.suite != "city":
        parser.error("--scenario requires --suite city")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,63}", args.run_name) or not 1 <= args.repeat <= 10:
        parser.error("run-name must be 1-64 letters/digits/_/-; repeat must be 1-10")
    executable = (ROOT / args.exe).resolve()
    output = ROOT / "build" / "shots" / "cj038" / args.phase / args.run_name
    if output.exists():
        parser.error("evidence directory exists; use a new run-name")
    if not executable.is_file() or not all((ROOT / n).is_file() for n in INPUTS):
        parser.error("build the executable and prepare both scene files first")
    output.mkdir(parents=True)
    manifest = {"phase": args.phase, "started": utc_now(), "git": git_state(),
                "executable": str(executable), "executable_sha256": digest(executable),
                "inputs": {n: digest(ROOT / n) for n in INPUTS}, "runs": []}
    path = output / "manifest.json"

    def save():
        path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    schedule = []
    if args.suite in ("all", "evasion"):
        schedule.append(("evasion", "person-hits", 1000000, "tests/person-evasion.cfg"))
    if args.suite == "impacts":
        schedule.append(("impacts", "person-hits", 1000000, "tests/person-hits.cfg"))
    if args.suite == "turn-peds":
        schedule.append(("turn-peds", "traffic-turn-peds", 12000, "tests/traffic-turn-pedestrians.cfg"))
    if args.suite == "turns":
        schedule.append(("turns", "traffic-turns", 200000, None))
    if args.suite in ("all", "city"):
        schedule.extend(("city", name, frames, None) for name, frames in CITY
                        if not args.scenario or name == args.scenario)
    save()
    try:
        for repetition in range(args.repeat):
            for suite, scenario, frames, test in schedule:
                name = f"{suite}-{scenario}-{repetition + 1}"
                shot = (output / (name + ".png")).relative_to(ROOT).as_posix()
                log = (output / (name + ".log")).relative_to(ROOT).as_posix()
                argv = [str(executable), "--scenario", scenario, "--shot", shot,
                        "--frames", str(frames), "--uncapped"]
                if suite == "city":
                    argv += ["--every", "300"]
                if test:
                    argv += ["--test", test]
                run = dict(name=name, suite=suite, scenario=scenario, shot=shot, log=log, argv=argv)
                manifest["runs"].append(run)
                save()
                print(f"Running {name}", flush=True)
                start = time.monotonic()
                with (ROOT / log).open("wb") as stream:
                    process = subprocess.Popen(argv, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT,
                                               stdin=subprocess.DEVNULL,
                                               env=dict(os.environ, CJ_TEST_REVISION=manifest["git"].get("head", "UNKNOWN")[:12]))
                    try:
                        run["exit"] = process.wait(timeout=args.timeout)
                    except (subprocess.TimeoutExpired, KeyboardInterrupt):
                        process.terminate()
                        process.wait(timeout=15)
                        raise
                run["seconds"] = round(time.monotonic() - start, 3)
                summarize(run, args.phase)
                save()
                print(json.dumps({k: v for k, v in run.items() if k not in ("argv", "lines")}), flush=True)
    except (OSError, subprocess.TimeoutExpired, KeyboardInterrupt) as exc:
        manifest["error"] = str(exc) or "Interrupted"
    manifest["ended"] = utc_now()
    manifest["complete"] = (len(manifest["runs"]) == len(schedule) * args.repeat
                            and all(r.get("complete", False) for r in manifest["runs"]))
    manifest["accepted"] = manifest["complete"] and all(r.get("accepted", False) for r in manifest["runs"])
    save()
    print(f"Evidence: {path}", flush=True)
    return 0 if manifest["accepted"] else 1


if __name__ == "__main__":
    sys.exit(main())
