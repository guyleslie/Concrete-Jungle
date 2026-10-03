#!/usr/bin/env python3
"""Summarize complete CJ-002 evidence without treating failed targets as missing runs."""

import argparse
import hashlib
import json
from pathlib import Path
import re

from run_cj002 import ROOT, VEHICLES


METRIC = re.compile(r"CJTEST metric fixture=\S+ phase=(\S+) name=(\S+) value=(\S+)")
SUMMARY = re.compile(r"CJTEST summary .*scheduled=(\d+) completed=(\d+).*failures=(\d+) incomplete=(\d+) invalid=(\d+)")


def load_case(case):
    log = ROOT / case["log"]
    text = log.read_text(encoding="utf-8", errors="replace")
    summaries = list(SUMMARY.finditer(text))
    if not summaries:
        raise ValueError(f"Missing fixture summary: {log}")
    scheduled, completed, failures, incomplete, invalid = map(int, summaries[-1].groups())
    if scheduled != completed or incomplete or invalid:
        raise ValueError(f"Incomplete or invalid fixture: {log}")
    if case["exit_status"] not in (0, 1) or case["status"] not in ("passed", "failed"):
        raise ValueError(f"Unsuccessful process execution: {log}")
    if not (ROOT / case["shot"]).is_file():
        raise ValueError(f"Missing final screenshot: {case['shot']}")
    metrics = {(m[1], m[2]): float(m[3]) for m in METRIC.finditer(text)}
    return text, metrics, completed, failures


def report():
    phase = "before"
    directory = ROOT / "build/shots/cj002" / phase
    cases = {}
    manifests = {}
    city_manifest = None
    for path in sorted(directory.rglob("manifest-*.json"), key=lambda p: p.name):
        manifest = json.loads(path.read_text(encoding="utf-8"))
        if manifest["status"] not in ("passed", "failed"):
            continue
        for case in manifest["cases"]:
            if case["scenario"] in ("handling", "crash-handling"):
                cases[case["name"]] = case
                manifests[case["name"]] = path.relative_to(directory).as_posix()
        if manifest["suite"] == "city" and manifest["status"] == "passed" and len(manifest["cases"]) == 6:
            city_manifest = (path, manifest)
    required = ["handling-" + name for name in VEHICLES] + ["crash-handling"]
    missing = set(required) - cases.keys()
    if missing:
        raise ValueError("Missing cases: " + ", ".join(sorted(missing)))
    hashes = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
              for name in ("ConcreteJungle.exe", "assets/data/vehicles.cfg", "src/vehicle_tests.cpp")}
    reference = cases[required[0]]["sha256"]
    if any(cases[name]["sha256"] != reference for name in required):
        raise ValueError("Isolated cases do not share identical executable/configuration/fixture hashes")
    current = all(reference[name] == digest for name, digest in hashes.items())
    lines = ["# CJ-002: Recorded arcade baseline", "",
             "Measured results from the isolated handling and collision fixtures, before production physics changes. Targets remain in the [approved proposal](vehicle-handling-proposal.md). Failed acceptance checks are baseline findings, not evidence of an interrupted run.", "",
             "## Evidence", "",
             f"All 17 classes and the collision fixture share the same executable, configuration and fixture SHA-256 hashes. Current workspace inputs {'match' if current else 'differ from'} these recorded inputs.", "",
             "| Input | SHA-256 |", "|---|---|"]
    lines.extend(f"| `{name}` | `{digest}` |" for name, digest in reference.items())
    lines += ["", "Manifests (relative to `build/shots/cj002/" + phase + "`):", ""]
    lines.extend("- `" + name + "`" for name in sorted(set(manifests.values())))
    lines += ["", "## Handling", "",
              "All classes completed 21 phases and 14,400 frames with no invalid state. A missing stop is shown explicitly: the car never met the sampled speed threshold of 0.1 m/s during the brake phase. Scooter acceleration and braking start at 50 km/h; all other classes use 100 km/h.", "",
              "| Class | Acceleration (s) | Brake distance (m) | Steady top speed (km/h) | Reverse peak (m/s) | Failed checks |",
              "|---|---|---|---|---|---|"]
    for name in VEHICLES:
        _, metrics, completed, failures = load_case(cases["handling-" + name])
        if completed != 21:
            raise ValueError(f"Unexpected handling phase count for {name}: {completed}")
        acceleration = metrics["acceleration", "zero_to_50_s" if name == "Scooter" else "zero_to_100_s"]
        braking = metrics["service-brake", "fifty_to_zero_m" if name == "Scooter" else "hundred_to_zero_m"]
        top = metrics["top-from-below", "steady_top_kmh"]
        reverse = metrics["reverse-transition", "reverse_speed_m_s"]
        brake_text = f"{braking:.2f}" if braking >= 0 else "No sampled stop"
        acceleration_text = f"{acceleration:.2f}" if acceleration >= 0 else "Target not reached"
        lines.append(f"| {name} | {acceleration_text} | {brake_text} | {top:.2f} | {reverse:.2f} | {failures} |")
    text, _, completed, failures = load_case(cases["crash-handling"])
    lines += ["", "## Collision findings", "",
              f"Completed {completed} phases at 60 Hz and 20 Hz, with {failures} failed checks and no invalid state. These are individual check failures, not a count of distinct defects.", "",
              "| Phase | Failed check | Measured value |", "|---|---|---|"]
    for line in text.splitlines():
        match = METRIC.search(line)
        if match and "result=FAIL" in line:
            lines.append(f"| `{match[1]}` | `{match[2]}` | {match[3]} |")
    lines += ["", "## Interpretation and next steps", "",
              "The arcade model accelerates much faster than the approved targets, has insufficient braking distance and allows excessive reverse speed. Several brake phases enter reverse before a sampled stop is recorded; retain this failure rather than inventing a stopping distance.", "",
              "Each class's failed-check count includes ten missing tyre-slip sample checks. Axle tyre slip does not exist in the arcade model; it must be available in the new model. This diagnostic absence is distinct from handling quality.", "",
              "An all-pass skidpad sweep provides a lower bound, and an all-fail sweep provides no measured maximum. Do not interpret the logged `usable_cornering_g` as a bracketed physical maximum unless `skid_sweep_bracketed` passes. The current narrow target-centred sweep is useful for testing the approved cornering bands, but does not establish the old model's maximum grip.", "",
              "Next: implement the approved axle force model and data-driven class parameters, resolve collision failures against the recorded cases, repeat all measurements and inspect screenshots. CJ-002 remains in progress until the user accepts the driving feel.", ""]
    if city_manifest and all(c.get("sha256") == reference for c in city_manifest[1]["cases"]):
        lines += ["## City evidence", "",
                  f"All six city runs completed on the same inputs in `{city_manifest[0].relative_to(directory).as_posix()}`. Process completion alone does not prove metric acceptance; compare the PHYS/PEDS logs and screenshots before and after.", "",
                  "| Scenario | Position flips | Heading flips | Max penetration (px) | Deep frames |",
                  "|---|---|---|---|---|"]
        for case in city_manifest[1]["cases"]:
            if case["status"] != "passed" or not (ROOT / case["shot"]).is_file():
                raise ValueError(f"Incomplete city evidence: {case['name']}")
            text = (ROOT / case["log"]).read_text(encoding="utf-8", errors="replace")
            jitter = re.search(r"PHYS: .*jitter: pos flips (\d+).*heading flips (\d+)", text)
            penetration = re.search(r"PHYS: penetration max ([\d.]+) px, frames > 3px: (\d+)", text)
            if not jitter or not penetration:
                raise ValueError(f"Missing city physics metrics: {case['name']}")
            lines.append(f"| `{case['name']}` | {jitter[1]} | {jitter[2]} | {penetration[1]} | {penetration[2]} |")
        lines.append("")
        lines += ["The `rampage` reference is not a clean collision pass: its deep-overlap frames remain an explicit baseline defect. Do not replace the zero-deep-frame requirement for isolated fixtures with this city result.", ""]
    else:
        lines += ["## City evidence", "",
                  "The city scenarios recorded on 2026-09-28 used an earlier executable and fixture. Keep them as historical evidence; rerun the city suite on the frozen baseline build before claiming an exact before/after integration comparison.", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        result = report()
    except (OSError, ValueError, KeyError) as exc:
        parser.exit(1, f"Cannot summarize evidence: {exc}\n")
    if args.output:
        args.output.write_text(result, encoding="utf-8")
    else:
        print(result)


if __name__ == "__main__":
    main()
