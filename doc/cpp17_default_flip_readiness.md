# C++17 compiler default-flip readiness checklist

This checklist is the release gate for flipping `KAITAI_COMPILER_ENGINE` default
from `scala` to `cpp17` in a later commit.

## Scope and non-goals

- This document does **not** flip the default engine.
- Scala build/test wiring remains the default reference path.
- C++17 compiler flow remains opt-in (`KAITAI_COMPILER_ENGINE=cpp17`).

## Signal catalog (automated)

| Signal ID | Command | Pass criteria |
| --- | --- | --- |
| `SIG-PARITY-DIFF` | `tests/ci-cpp17-differential` | Exit code 0 with `--enforce-gate required`; all required fixtures report parity or approved known deviations. |
| `SIG-PERF-BENCH` | `tests/migration_golden/run_cpp17_benchmarks.py --fixtures tests/migration_golden/benchmark_fixtures.tsv --output-dir tests/test_out/migration_benchmarks --stability-cv-max 1.0` | Exit code 0; latency, memory, and stability ratios satisfy configured thresholds in `report.json`. |
| `SIG-PERF-SCHEMA` | `tests/migration_golden/run_cpp17_benchmarks.py --check-schema tests/test_out/migration_benchmarks/report.json` | Exit code 0; benchmark report schema validates. |
| `SIG-CHECKLIST-LINT` | `tests/migration_golden/validate_cpp17_readiness_checklist.py` | Exit code 0; every checklist item maps to at least one automated signal ID. |

## Default flip checklist

All checklist items must be `PASS` before default flip is proposed.

| Area | Checklist item | Automated signal(s) | Pass/fail rubric |
| --- | --- | --- | --- |
| Feature parity | Required migration fixtures produce parity-equivalent output vs Scala oracle on active targets (`cpp_stl`, `lua`, `wireshark_lua`, `python`, `ruby`). | `SIG-PARITY-DIFF` | **PASS** when command exits 0 and report has zero blocking fixture failures. |
| Stability | Differential checks are reproducible and do not introduce flaky required failures in CI. | `SIG-PARITY-DIFF` | **PASS** when CI remains green for required gate and no new flaky required fixtures are added. |
| Performance | C++17 path compile latency and memory remain within configured benchmark thresholds versus Scala baseline. | `SIG-PERF-BENCH`, `SIG-PERF-SCHEMA` | **PASS** when benchmark run exits 0 and schema validation succeeds. |
| Diagnostics | Migration diagnostics remain actionable (import-graph errors, advanced semantics subset) and regressions surface in differential inventory. | `SIG-PARITY-DIFF` | **PASS** when required diagnostics fixtures pass and any known mismatches stay explicitly documented in fixture metadata. |
| Documentation | Readiness and migration docs are present and internally consistent (checklist ↔ signals linkage). | `SIG-CHECKLIST-LINT` | **PASS** when lint command exits 0. |

## Review output format (for release readiness review)

Record a readiness summary with:

- Commit SHA under review
- Signal execution timestamps
- Raw command lines used
- PASS/FAIL per checklist row
- Deferred items (if any) with owner and due date

