# C++17 Migration Benchmark Trend Report

## Scope

This report defines the migration benchmark harness used to build confidence in the opt-in C++17 path without changing the default Scala compiler behavior.

- Harness: `tests/migration_golden/run_cpp17_benchmarks.py`
- Fixture corpus: `tests/migration_golden/benchmark_fixtures.tsv`
- Output schema: `tests/test_out/migration_benchmarks/report.json`

The corpus is intentionally fixed per migration phase to make trend comparisons meaningful across commits.

## Metric schema

Per fixture, the report captures metrics for both paths:

- `scala_full`: Scala compiler path compiling `.ksy` directly.
- `cpp_from_ir`: C++17 path compiling from Scala-emitted migration IR.

Each path includes:

- Iteration-level metrics: `elapsed_sec`, `user_sec`, `sys_sec`, `max_rss_kb`, `iteration`, `phase`.
- Summary metrics:
  - latency: median/mean/min/max/cv
  - max RSS: median/mean/min/max/cv

Cross-path ratios:

- `latency_median_ratio_cpp_over_scala`
- `max_rss_median_ratio_cpp_over_scala`

## Baseline thresholds

Thresholds are recorded in `report.json` and used to mark trend warnings:

- `latency_ratio_max = 2.0`
- `memory_ratio_max = 2.0`
- `stability_cv_max = 0.20`

Interpretation:

- **Pass**: No fixture breaches thresholds.
- **Warn**: At least one fixture breaches thresholds.

`warn` is migration-informational and does **not** flip compiler defaults.

## Usage

```bash
tests/migration_golden/run_cpp17_benchmarks.py \
  --fixtures tests/migration_golden/benchmark_fixtures.tsv \
  --output-dir tests/test_out/migration_benchmarks
```

To validate schema consistency for automation:

```bash
tests/migration_golden/run_cpp17_benchmarks.py \
  --check-schema tests/test_out/migration_benchmarks/report.json
```

## TODO (future migration phases)

- Add commit-to-commit trend delta checks (baseline file + regression budget).
- Split C++ path timing into IR load/parse and backend emission sub-metrics.
- Integrate benchmark trend artifacts into CI retention for longitudinal analysis.
