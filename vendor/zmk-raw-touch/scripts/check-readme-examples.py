#!/usr/bin/env python3
# Copyright (c) 2026 Mrinal Kalakrishnan
# SPDX-License-Identifier: MIT
"""Keep the README's configuration blocks identical to the files under examples/.

The files are the source of truth: CI compiles them (see ci/), so a block
that matches its file is a block that builds. The README marks each block
with an HTML comment on the line before its code fence:

    <!-- example: examples/trackpad/raw-touch.overlay -->
    ```dts
    ...the file, verbatim...
    ```

    <!-- example-diff: trackpad.overlay examples/trackpad/before.overlay examples/trackpad/raw-touch.overlay -->
    ```diff
    ...unified diff of the two files, generated...
    ```

The diff form names the file as the reader should think of it, then the
before and after files. Its block is regenerated, never hand-edited.

    scripts/check-readme-examples.py          # exit 1 on any drift (CI)
    scripts/check-readme-examples.py --write  # rewrite the README's blocks from the files

Every file under examples/ (other than its README) must be referenced by
at least one marker, so an example cannot quietly stop being documented.
"""

import difflib
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
README = ROOT / "README.md"
EXAMPLES = ROOT / "examples"

MARKER = re.compile(r"^<!-- example(-diff)?: (.+?) -->$")
FENCE = re.compile(r"^```(\S*)$")


def read_example(rel: str) -> list[str]:
    path = ROOT / rel
    if not path.is_file():
        sys.exit(f"{README.name}: marker names a missing file: {rel}")
    return path.read_text().split("\n")[:-1]  # drop the trailing newline's empty element


def expected_lines(is_diff: bool, args: str) -> tuple[list[str], set[str]]:
    """The block a marker calls for, and the example files it references."""
    if not is_diff:
        rel = args.strip()
        return read_example(rel), {rel}
    parts = args.split()
    if len(parts) != 3:
        sys.exit(f"{README.name}: example-diff needs '<name> <before> <after>', got: {args}")
    name, before, after = parts
    diff = difflib.unified_diff(
        read_example(before), read_example(after),
        fromfile=f"{name} (before)", tofile=f"{name} (with raw touch)", lineterm="",
    )
    return list(diff), {before, after}


def main() -> int:
    write = "--write" in sys.argv[1:]
    lines = README.read_text().split("\n")
    out: list[str] = []
    problems: list[str] = []
    referenced: set[str] = set()
    markers = 0
    i = 0
    while i < len(lines):
        line = lines[i]
        m = MARKER.match(line)
        if not m:
            out.append(line)
            i += 1
            continue
        markers += 1
        is_diff = m.group(1) is not None
        fence = FENCE.match(lines[i + 1]) if i + 1 < len(lines) else None
        if not fence:
            sys.exit(f"{README.name}:{i + 1}: marker is not followed by a code fence")
        start = i + 2
        try:
            end = next(j for j in range(start, len(lines)) if lines[j] == "```")
        except StopIteration:
            sys.exit(f"{README.name}:{i + 2}: unterminated code fence")
        actual = lines[start:end]
        expected, files = expected_lines(is_diff, m.group(2))
        referenced |= files
        if actual != expected:
            where = f"{README.name}:{i + 1} ({m.group(2)})"
            if write:
                print(f"rewrote {where}")
            else:
                delta = "\n".join(difflib.unified_diff(actual, expected, "README block", "from examples/", lineterm=""))
                problems.append(f"{where} differs from its example file(s):\n{delta}")
        out.extend([line, lines[i + 1], *expected, "```"])
        i = end + 1

    if markers == 0:
        sys.exit(f"{README.name}: no example markers found")

    orphans = sorted(
        str(p.relative_to(ROOT)) for p in EXAMPLES.rglob("*")
        if p.is_file() and p.name != "README.md" and str(p.relative_to(ROOT)) not in referenced
    )
    for rel in orphans:
        problems.append(f"{rel} is not referenced by any README marker")

    if write:
        README.write_text("\n".join(out))
    if problems:
        print("\n\n".join(problems))
        print(f"\n{len(problems)} problem(s). Run scripts/check-readme-examples.py --write to refresh the README's blocks from examples/.")
        return 1
    print(f"README: {markers} example blocks match examples/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
