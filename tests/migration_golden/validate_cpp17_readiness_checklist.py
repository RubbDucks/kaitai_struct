#!/usr/bin/env python3
"""Validate that readiness checklist rows map to automated signals."""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
CHECKLIST = REPO_ROOT / "doc" / "cpp17_default_flip_readiness.md"

SIGNAL_RE = re.compile(r"SIG-[A-Z0-9-]+")


def main() -> int:
    if not CHECKLIST.exists():
        print(f"[readiness-checklist] missing file: {CHECKLIST}", file=sys.stderr)
        return 2

    text = CHECKLIST.read_text(encoding="utf-8")
    lines = text.splitlines()

    signal_catalog = set(SIGNAL_RE.findall(text))
    if not signal_catalog:
        print("[readiness-checklist] no signal IDs found", file=sys.stderr)
        return 2

    missing: list[tuple[int, str]] = []
    for idx, line in enumerate(lines, start=1):
        if not line.startswith("| "):
            continue
        if line.startswith("| Area ") or line.startswith("| ---"):
            continue
        if "PASS" not in line:
            continue
        signals = SIGNAL_RE.findall(line)
        if not signals:
            missing.append((idx, line))
            continue
        unknown = [s for s in signals if s not in signal_catalog]
        if unknown:
            missing.append((idx, f"{line} (unknown: {', '.join(unknown)})"))

    if missing:
        print("[readiness-checklist] checklist rows missing valid automated signals:", file=sys.stderr)
        for idx, row in missing:
            print(f"  line {idx}: {row}", file=sys.stderr)
        return 1

    print(
        "[readiness-checklist] OK "
        f"(signals={len(signal_catalog)}, checklist_rows_with_rubrics={sum(1 for l in lines if l.startswith('| ') and 'PASS' in l)})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
