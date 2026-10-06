#!/usr/bin/env python3
"""Package a playable Windows release of Concrete Jungle as a zip file.

Collects the executable, every tracked file under assets/ and the licence,
credits and readme into build/release/ConcreteJungle-<version>-windows-x64.zip,
with a top-level ConcreteJungle-<version>/ folder. The executable must be a
release build from build.bat: it runs without a console window, and build.sh
builds a console executable for test runs.

Usage:  python tools/package_release.py --version 0.3.0
"""
import argparse
import hashlib
import re
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCUMENTS = ["LICENSE", "CREDITS.md", "README.md", "CHANGELOG.md"]
PE_SUBSYSTEM_GUI = 2


def pe_subsystem(exe):
    """Returns the Windows subsystem field of a PE executable (2 = GUI, 3 = console)."""
    data = exe.read_bytes()[:4096]
    if data[:2] != b"MZ":
        raise ValueError(f"{exe} is not a Windows executable")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError(f"{exe} has no PE header")
    # The subsystem is at offset 68 of the optional header, which follows the
    # 4-byte signature and the 20-byte file header (same offset for PE32 and PE32+).
    return struct.unpack_from("<H", data, pe + 24 + 68)[0]


def tracked_assets():
    out = subprocess.run(["git", "ls-files", "-z", "assets"], cwd=ROOT, check=True, capture_output=True).stdout
    return [Path(p) for p in out.decode("utf-8").split("\0") if p]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--version", required=True, help="release version, e.g. 0.3.0")
    parser.add_argument("--exe", default="ConcreteJungle.exe", help="executable to package")
    parser.add_argument("--output", default="build/release", help="output folder")
    args = parser.parse_args()

    if not re.fullmatch(r"\d+\.\d+\.\d+", args.version):
        sys.exit(f"Version must look like 0.3.0, not {args.version}")
    exe = ROOT / args.exe
    if not exe.is_file():
        sys.exit(f"{exe} not found: build it with build.bat first")
    if pe_subsystem(exe) != PE_SUBSYSTEM_GUI:
        sys.exit(f"{exe} is a console build: build the release with build.bat")
    if not (ROOT / "CHANGELOG.md").read_text(encoding="utf-8").count(f"## [{args.version}]"):
        sys.exit(f"CHANGELOG.md has no section for {args.version}")

    name = f"ConcreteJungle-{args.version}"
    target = ROOT / args.output / f"{name}-windows-x64.zip"
    target.parent.mkdir(parents=True, exist_ok=True)
    files = [(exe, "ConcreteJungle.exe")]
    files += [(ROOT / doc, doc) for doc in DOCUMENTS]
    files += [(ROOT / asset, asset.as_posix()) for asset in tracked_assets()]
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for source, arcname in files:
            archive.write(source, f"{name}/{arcname}")

    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    print(f"{target.relative_to(ROOT).as_posix()}: {len(files)} files, {target.stat().st_size / 1e6:.1f} MB")
    print(f"SHA-256 {digest}")


if __name__ == "__main__":
    main()
