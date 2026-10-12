"""Sets the gain of every radio clip in assets/data/radio.cfg (CJ-040).

Measures each clip's integrated loudness (EBU R128, ffmpeg's ebur128 filter) and writes
the gain that brings it to the LOUDNESS target into its SONG, DJ or AD record, keeping
everything else in the file as it is. Run it after adding or changing a clip:

    python tools/radio_gain.py            # update the gains
    python tools/radio_gain.py --check    # only report; exit 1 if a gain is off by 0.1 dB or more

Needs ffmpeg on the PATH.
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONFIG = ROOT / "assets" / "data" / "radio.cfg"
CLIP = re.compile(r"^(\s*)(SONG|DJ|AD)(\s+)(\S+)(\s+)(\S+)(.*)$", re.IGNORECASE)


def loudness(path):
    """Integrated loudness in LUFS."""
    result = subprocess.run(["ffmpeg", "-hide_banner", "-nostats", "-i", str(path), "-map", "0:a",
                             "-af", "ebur128", "-f", "null", "-"], capture_output=True, text=True)
    summary = result.stderr[result.stderr.rfind("Summary:"):]
    for line in summary.splitlines():
        line = line.strip()
        if line.startswith("I:"):
            return float(line.split()[1])
    raise RuntimeError(f"{path}: no loudness in ffmpeg's output")


def main():
    check = "--check" in sys.argv
    lines = CONFIG.read_text(encoding="utf-8").splitlines(keepends=True)
    target, folder = None, None
    for line in lines:
        tok = line.split("#", 1)[0].split()
        if tok and tok[0].upper() == "LOUDNESS":
            target = float(tok[1])
    if target is None:
        sys.exit(f"{CONFIG}: no LOUDNESS record")
    off = 0
    for i, line in enumerate(lines):
        tok = line.split("#", 1)[0].split()
        if tok and tok[0].upper() == "STATION":
            folder = ROOT / "assets" / "radio" / tok[1]
            continue
        m = CLIP.match(line.rstrip("\r\n"))
        if not m or folder is None:
            continue
        path = folder / m.group(4)
        level = loudness(path)
        gain = round(target - level, 1)
        old = float(m.group(6))
        mark = "" if abs(old - gain) < 0.1 else "  <- changed"
        off += mark != ""
        print(f"{m.group(2).upper():4} {path.relative_to(ROOT).as_posix():40} {level:6.1f} LUFS  gain {gain:+5.1f} dB{mark}")
        if not check:
            ending = line[len(line.rstrip("\r\n")):]
            lines[i] = f"{m.group(1)}{m.group(2)}{m.group(3)}{m.group(4)}{m.group(5)}{gain:+.1f}{m.group(7)}{ending}"
    if check:
        sys.exit(1 if off else 0)
    CONFIG.write_text("".join(lines), encoding="utf-8", newline="")
    print(f"{CONFIG.relative_to(ROOT).as_posix()}: {off} gains changed (target {target:.1f} LUFS)")


if __name__ == "__main__":
    main()
