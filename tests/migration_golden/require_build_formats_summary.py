#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


def main() -> int:
    p = argparse.ArgumentParser(description="Validate tests/build-formats summary JSON")
    p.add_argument("summary", type=Path)
    p.add_argument("--expect-engine")
    p.add_argument("--expect-target")
    p.add_argument("--allow-failures", type=int, default=0)
    args = p.parse_args()

    data = json.loads(args.summary.read_text(encoding="utf-8"))
    required = ["engine", "target", "generated", "failed", "skipped", "status"]
    missing = [k for k in required if k not in data]
    if missing:
        raise SystemExit(f"missing fields in summary: {', '.join(missing)}")

    if args.expect_engine and data["engine"] != args.expect_engine:
        raise SystemExit(f"engine mismatch: expected {args.expect_engine}, got {data['engine']}")
    if args.expect_target and data["target"] != args.expect_target:
        raise SystemExit(f"target mismatch: expected {args.expect_target}, got {data['target']}")

    failed = int(data["failed"])
    if failed > args.allow_failures:
        raise SystemExit(
            f"unexpected build-formats failures: failed={failed} allow={args.allow_failures}; summary={args.summary}"
        )

    expected_status = "success" if failed == 0 else "failed"
    if data["status"] != expected_status:
        raise SystemExit(f"status mismatch: expected {expected_status}, got {data['status']}")

    print(
        f"[build-formats-summary] validated engine={data['engine']} target={data['target']} "
        f"generated={data['generated']} failed={data['failed']} skipped={data['skipped']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
