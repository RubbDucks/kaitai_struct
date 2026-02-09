# C++17 compiler default-flip readiness checklist

This checklist is the release gate that was used to flip
`KAITAI_COMPILER_ENGINE` default from `scala` to `cpp17`.

## Scope and non-goals

- Default engine is now `cpp17` for test/build entrypoints in this repository.
- Scala build/test wiring is retained as explicit fallback (`KAITAI_COMPILER_ENGINE=scala`).
- This checklist remains the required evidence set for post-flip monitoring.
- `tests/migration_golden/cpp17_differential_fixtures.tsv` is the canonical gate source
  for required vs visibility fixture status.

## Signal catalog (automated)

Canonical threshold set (release-grade and CI-enforced): latency ratio max **2.0**, memory ratio max **2.0**, stability CV max **0.20**.


| Signal ID | Command | Pass criteria |
| --- | --- | --- |
| `SIG-PARITY-DIFF` | `tests/ci-cpp17-differential` | Exit code 0 with `--enforce-gate required`; all required fixtures report parity or approved known deviations. |
| `SIG-PERF-BENCH` | `tests/migration_golden/run_cpp17_benchmarks.py --fixtures tests/migration_golden/benchmark_fixtures.tsv --output-dir tests/test_out/migration_benchmarks --latency-ratio-max 2.0 --memory-ratio-max 2.0 --stability-cv-max 0.20` | Exit code 0; latency, memory, and stability ratios satisfy the canonical release thresholds in `report.json` (latency ≤ 2.0, memory ≤ 2.0, stability CV ≤ 0.20). |
| `SIG-PERF-SCHEMA` | `tests/migration_golden/run_cpp17_benchmarks.py --check-schema tests/test_out/migration_benchmarks/report.json` | Exit code 0; benchmark report schema validates. |
| `SIG-CHECKLIST-LINT` | `tests/migration_golden/validate_cpp17_readiness_checklist.py` | Exit code 0; every checklist item maps to at least one automated signal ID. |
| `SIG-GATE-METADATA-LINT` | `tests/migration_golden/validate_cpp17_gate_examples.py` | Exit code 0; documented required/visibility fixture examples match `cpp17_differential_fixtures.tsv`. |

## Default flip checklist

All checklist items must be `PASS` before default flip is proposed.

| Area | Checklist item | Automated signal(s) | Pass/fail rubric |
| --- | --- | --- | --- |
| Feature parity | Required migration fixtures produce parity-equivalent output vs Scala oracle on active targets (`cpp_stl`, `lua`, `wireshark_lua`, `python`, `ruby`). | `SIG-PARITY-DIFF` | **PASS** when command exits 0 and report has zero blocking fixture failures. |
| Stability | Differential checks are reproducible and do not introduce flaky required failures in CI. | `SIG-PARITY-DIFF` | **PASS** when CI remains green for required gate and no new flaky required fixtures are added. |
| Performance | C++17 path compile latency and memory remain within configured benchmark thresholds versus Scala baseline. | `SIG-PERF-BENCH`, `SIG-PERF-SCHEMA` | **PASS** when benchmark run exits 0 and schema validation succeeds. |
| Diagnostics | Migration diagnostics remain actionable (import-graph errors, advanced semantics subset) and regressions surface in differential inventory. | `SIG-PARITY-DIFF` | **PASS** when required diagnostics fixtures pass and any known mismatches stay explicitly documented in fixture metadata. |
| Documentation | Readiness and migration docs are present and internally consistent (checklist ↔ signals linkage and fixture gate examples). | `SIG-CHECKLIST-LINT`, `SIG-GATE-METADATA-LINT` | **PASS** when both lint commands exit 0. |

Documented gate examples (must match `tests/migration_golden/cpp17_differential_fixtures.tsv`):

<!-- gate-examples:start -->
- required: `cpp17_empty_parity`, `cpp17_hello_world`, `python_hello_world`
- visibility: _(none)_
<!-- gate-examples:end -->

## Review output format (for release readiness review)

Record a readiness summary with:

- Commit SHA under review
- Signal execution timestamps
- Raw command lines used
- PASS/FAIL per checklist row
- Deferred items (if any) with owner and due date
