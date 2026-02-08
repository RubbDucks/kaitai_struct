# Migration golden snapshots

- Fixture inventory: `fixtures.tsv`
- Differential fixture inventory for C++17 migration checks: `cpp17_differential_fixtures.tsv`
- Snapshot generator: `generate_scala_snapshots.sh`
- Text normalizer: `normalize_compiler_output.py`
- Differential runner (Scala vs C++17 IR backend): `run_cpp17_differential.py`
- Baseline snapshots (Scala): `scala/*.snapshot`

Regenerate Scala baselines:

```bash
tests/migration_golden/generate_scala_snapshots.sh
```


Compare Scala vs experimental C++17 (IR-driven) normalized output for minimal supported subset:

```bash
tests/migration_golden/compare_cpp17_from_ir.sh
```

Compare Scala vs experimental C++17 (IR-driven) for expression subset A fixture:

```bash
tests/migration_golden/compare_cpp17_expr_subset_a.sh
```

Type-focused parity checks for integer/float/bytes/string/enum subset:

```bash
tests/migration_golden/compare_cpp17_type_subset.sh
```

Import graph migration checks (nested imports + cycle/collision diagnostics):

```bash
tests/migration_golden/compare_cpp17_imports.sh
```


Advanced semantics migration checks (instances + validations + process xor const subset):

```bash
tests/migration_golden/compare_cpp17_advanced_semantics.sh
```

Run an automated differential sweep and emit both JSON + human-readable reports:

```bash
tests/migration_golden/run_cpp17_differential.py \
  --fixtures tests/migration_golden/cpp17_differential_fixtures.tsv \
  --output-dir tests/test_out/migration_differential
```

Artifacts are emitted under the output directory:

- `report.json`: machine-readable report for CI tooling.
- `summary.txt`: concise human summary.
- `<fixture-id>/...`: raw/normalized outputs, tool logs and optional diff snippet.
