#!/usr/bin/env python3
"""Validate documented gate examples against cpp17 differential fixture metadata."""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
FIXTURES = REPO_ROOT / "tests" / "migration_golden" / "cpp17_differential_fixtures.tsv"
DOCS = [
    REPO_ROOT / "doc" / "cpp17_migration" / "README.md",
    REPO_ROOT / "doc" / "cpp17_default_flip_readiness.md",
]

START_MARKER = "<!-- gate-examples:start -->"
END_MARKER = "<!-- gate-examples:end -->"
LINE_RE = re.compile(r"^-\s*(required|visibility):\s*(.+)$")
FIXTURE_RE = re.compile(r"`([a-zA-Z0-9_\-]+)`")


def load_fixture_ids_by_gate() -> dict[str, set[str]]:
    if not FIXTURES.exists():
        raise FileNotFoundError(f"missing fixture metadata: {FIXTURES}")

    rows = [line for line in FIXTURES.read_text(encoding="utf-8").splitlines() if line.strip()]
    header = rows[0].split("\t")
    header[0] = header[0].lstrip("# ").strip()
    idx = {name: i for i, name in enumerate(header)}

    result: dict[str, set[str]] = {"required": set(), "visibility": set()}
    for row in rows[1:]:
        cols = row.split("\t")
        fixture_id = cols[idx["id"]].strip()
        gate = cols[idx["gate"]].strip()
        if gate in result:
            result[gate].add(fixture_id)
    return result


def parse_doc_gate_examples(doc_path: Path) -> dict[str, list[str]]:
    text = doc_path.read_text(encoding="utf-8")
    if START_MARKER not in text or END_MARKER not in text:
        raise ValueError(f"{doc_path}: missing gate example markers")

    block = text.split(START_MARKER, 1)[1].split(END_MARKER, 1)[0]
    parsed: dict[str, list[str]] = {"required": [], "visibility": []}

    for raw_line in block.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        m = LINE_RE.match(line)
        if not m:
            raise ValueError(f"{doc_path}: malformed gate example line: {raw_line}")
        gate, values = m.groups()
        if values.strip() == "_(none)_":
            parsed[gate] = []
            continue
        parsed[gate] = FIXTURE_RE.findall(values)
        if not parsed[gate]:
            raise ValueError(f"{doc_path}: no fixture ids found for gate '{gate}'")

    return parsed


def main() -> int:
    try:
        fixture_ids = load_fixture_ids_by_gate()
    except Exception as exc:
        print(f"[gate-examples] {exc}", file=sys.stderr)
        return 2

    failures: list[str] = []
    checked_docs = 0

    for doc in DOCS:
        if not doc.exists():
            failures.append(f"missing doc: {doc}")
            continue
        checked_docs += 1
        try:
            parsed = parse_doc_gate_examples(doc)
        except Exception as exc:
            failures.append(str(exc))
            continue

        for gate in ("required", "visibility"):
            wanted = parsed[gate]
            available = fixture_ids[gate]
            if wanted:
                unknown = [fixture for fixture in wanted if fixture not in available]
                if unknown:
                    failures.append(
                        f"{doc}: gate '{gate}' contains fixture(s) not present with same gate in TSV: {', '.join(unknown)}"
                    )
            else:
                if available:
                    failures.append(
                        f"{doc}: gate '{gate}' is documented as _(none)_ but TSV contains {len(available)} fixture(s)"
                    )

    if failures:
        print("[gate-examples] documentation/fixture gate drift detected:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print(
        "[gate-examples] OK "
        f"(docs={checked_docs}, required_fixtures={len(fixture_ids['required'])}, visibility_fixtures={len(fixture_ids['visibility'])})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
