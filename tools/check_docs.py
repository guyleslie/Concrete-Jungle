#!/usr/bin/env python3
"""Check the project's Markdown documentation for broken links.

Scans every *.md file in the repository (outside build folders and .git) and
verifies that each relative link points at an existing file or folder and that
each #anchor matches a heading in the target document, using GitHub's heading
slug rules. External links (http, https, mailto) are not fetched.

Usage:  python tools/check_docs.py
Exit code 0 when everything resolves, 1 otherwise.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIP_DIRS = {".git", "build", "build_cmake", "node_modules"}
LINK_RE = re.compile(r"!?\[([^\]]*)\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
FENCE_RE = re.compile(r"^\s*(```|~~~)")


def markdown_files():
    for base, dirs, files in os.walk(ROOT):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for name in files:
            if name.lower().endswith(".md"):
                yield os.path.join(base, name)


def prose_lines(path):
    """Yields (line number, text) for lines outside fenced code blocks."""
    in_fence = False
    with open(path, encoding="utf-8") as f:
        for number, line in enumerate(f, 1):
            if FENCE_RE.match(line):
                in_fence = not in_fence
                continue
            if not in_fence:
                yield number, line


def slug(heading):
    """GitHub-style anchor for a heading text."""
    text = re.sub(r"`([^`]*)`", r"\1", heading)            # inline code
    text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)   # links
    text = text.replace("*", "").strip().lower()
    text = re.sub(r"[^\w\- ]", "", text)                   # punctuation
    return text.replace(" ", "-")


_anchor_cache = {}


def anchors(path):
    if path not in _anchor_cache:
        found, seen = set(), {}
        for _, line in prose_lines(path):
            m = re.match(r"^(#{1,6})\s+(.*?)\s*#*\s*$", line)
            if not m:
                continue
            base = slug(m.group(2))
            count = seen.get(base, 0)
            seen[base] = count + 1
            found.add(base if count == 0 else f"{base}-{count}")
        _anchor_cache[path] = found
    return _anchor_cache[path]


def main():
    errors, checked, files = [], 0, 0
    for md in sorted(markdown_files()):
        files += 1
        rel_md = os.path.relpath(md, ROOT)
        for number, line in prose_lines(md):
            line = re.sub(r"`[^`]*`", "", line)                # ignore inline code
            for m in LINK_RE.finditer(line):
                target = m.group(2)
                if re.match(r"^(https?:|mailto:)", target):
                    continue
                checked += 1
                path_part, _, anchor = target.partition("#")
                dest = os.path.normpath(os.path.join(os.path.dirname(md), path_part)) if path_part else md
                if not os.path.exists(dest):
                    errors.append(f"{rel_md}:{number}: missing file '{target}'")
                    continue
                if anchor and dest.lower().endswith(".md") and anchor not in anchors(dest):
                    errors.append(f"{rel_md}:{number}: missing anchor '#{anchor}' in {os.path.relpath(dest, ROOT)}")
    for e in errors:
        print(e)
    print(f"{files} files, {checked} relative links checked, {len(errors)} problem(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
