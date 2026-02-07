# Migration golden snapshots

- Fixture inventory: `fixtures.tsv`
- Snapshot generator: `generate_scala_snapshots.sh`
- Text normalizer: `normalize_compiler_output.py`
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
