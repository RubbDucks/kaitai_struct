# Compiler Output Contract for Scala-to-C++17 Migration

This contract defines how generated output is compared between compiler implementations.
It is language-agnostic and applies to all backends (`python`, `ruby`, `cpp_stl`, `lua`, ...).

## Scope

- **In scope:** textual compiler artifacts (generated source files and compiler diagnostics for intentional failure fixtures).
- **Out of scope (for this phase):** runtime execution semantics, performance, binary output size.

## Canonical normalization rules

Before diffing snapshots, both Scala and C++17 outputs MUST be normalized with
`tests/migration_golden/normalize_compiler_output.py`.

Normalization steps:

1. Convert line endings to `\n`.
2. Replace absolute repository paths with `<REPO_ROOT>`.
3. Replace ISO-like timestamps with `<TIMESTAMP>`.
4. Replace generated banner lines (`This is a generated file!`) with `<GENERATED_BANNER>`.
5. Collapse 3+ blank lines into 2 blank lines.
6. Ensure a single trailing newline.

## Stable vs unstable fields

### Stable (must match)

- Fixture metadata used for snapshot identity (`id`, `category`, `mode`, target).
- Set of generated file paths for successful fixtures.
- Normalized textual body of generated files.
- Compiler non-zero exit status for error fixtures.
- Normalized diagnostic message text for error fixtures (after path/timestamp cleanup).

### Unstable (ignored by normalization)

- Absolute paths emitted by the host environment.
- Timestamp-like tokens.
- Formatting variations limited to line-ending style and excessive blank-line runs.
- Generated banner wording around `This is a generated file!`.

## Minimum viable fixture coverage

The migration baseline covers these categories:

- primitives
- enums
- instances
- switch-on
- repeats
- imports
- process
- encoding
- validation
- errors

Fixture inventory is declared in `tests/migration_golden/fixtures.tsv`.

## Reproducible Scala baseline workflow

Use:

```bash
tests/migration_golden/generate_scala_snapshots.sh
```

This script performs:

1. Build Scala stage compiler if missing (`tests/build-compiler`).
2. Compile each fixture listed in `fixtures.tsv`.
3. Collect generated files (or compiler diagnostics for error fixtures).
4. Normalize into `tests/migration_golden/scala/*.snapshot`.
5. Recompute `tests/migration_golden/scala/SHA256SUMS`.

## C++17 migration golden comparators (experimental backend slices)

The first backend slice adds a focused comparator for `hello_world`-like specs:

```sh
tests/migration_golden/compare_cpp17_from_ir.sh
```

It compiles `tests/formats/hello_world.ksy` via Scala (`cpp_stl` + `--cpp-standard 17`) and compares normalized output against C++17 IR-driven generation from `compiler-cpp/tests/data/hello_world_minimal.ksir`.

This check is opt-in and migration-scoped; it does not alter default Scala compiler/test wiring.


Advanced semantics subset comparator (instances + validations + process xor const):

```sh
tests/migration_golden/compare_cpp17_advanced_semantics.sh
```
