#!/usr/bin/env python3
import json
import tempfile
import unittest
from pathlib import Path

from run_cpp17_benchmarks import validate_report_schema


class BenchmarkSchemaTest(unittest.TestCase):
    def test_validate_report_schema_accepts_expected_shape(self):
        report = {
            "schema_version": 1,
            "tool": "run_cpp17_benchmarks.py",
            "generated_at_utc": "2026-01-01T00:00:00Z",
            "thresholds": {},
            "fixtures": [
                {
                    "id": "bench_fixture",
                    "paths": {
                        "scala_full": {"runs": [], "summary": {}},
                        "cpp_from_ir": {"runs": [], "summary": {}},
                    },
                }
            ],
            "summary": {},
        }
        self.assertEqual(validate_report_schema(report), [])

    def test_validate_report_schema_rejects_missing_paths(self):
        report = {
            "schema_version": 1,
            "tool": "run_cpp17_benchmarks.py",
            "generated_at_utc": "2026-01-01T00:00:00Z",
            "thresholds": {},
            "fixtures": [{"id": "bad_fixture"}],
            "summary": {},
        }
        errors = validate_report_schema(report)
        self.assertTrue(any("missing paths" in e for e in errors))


if __name__ == "__main__":
    unittest.main()
