# C++17 Compiler Parity Contract (Active Targets)

This checklist defines behavioral parity required before the C++17 compiler path is considered release-ready for this fork's active targets: `cpp_stl`, `lua`, `wireshark_lua`, `python`, `ruby`.

## Normative baseline

- Reference implementation: `compiler/jvm/src/main/scala/io/kaitai/struct/JavaMain.scala` and its transitive CLI/frontend/semantic behavior.
- Parity means: for the same inputs, options, and target, C++17 path must match Scala path in success/failure class, diagnostics semantics, and generated output/runtime contract (allowing only documented, intentional formatting differences).

## Parity checklist

### 1) CLI UX and argument semantics

- [ ] Help/version text surfaces equivalent flags, defaults, and examples used by repository scripts.
- [ ] Argument validation and exit status classes match Scala behavior for:
  - missing/unknown options
  - missing option values
  - incompatible option combinations
  - missing input files
- [ ] Multi-input / multi-target invocation behavior is identical (or explicitly documented as compatible policy with all scripts updated).
- [ ] `--ksc-json-output` machine output schema/exit semantics match Scala for success and failures.

### 2) Diagnostic and error parity

- [ ] Error categories align with Scala (CLI error, parse error, semantic/type error, import resolution error, backend error).
- [ ] Message content used by tests is compatible (same actionable cause and path context).
- [ ] Import cycle and symbol collision diagnostics are equivalent across nested import graphs.

### 3) Full `.ksy` language frontend semantics

- [ ] Parser + semantic model covers all constructs exercised by active-target fixtures:
  - meta / params / seq / instances / enums / types
  - `if`, `repeat`, `switch-on`, size/terminator/process forms
  - bit-endian and byte-endian rules
  - stream/substream positioning and IO helpers
  - validation clauses and casts
- [ ] No subset-only parse/semantic gates remain in normal `.ksy` compilation path.

### 4) Expression + type-checking parity

- [ ] Operator coverage, precedence, associativity, and short-circuit behavior match Scala.
- [ ] Name resolution scopes match Scala for attrs, instances, params, parent/root, enum refs, IO/system refs.
- [ ] Type inference/compatibility/casts match Scala expectations used by tests.
- [ ] Malformed expression and type diagnostics match Scala test expectations.

### 5) IR semantics parity

- [ ] C++17-owned IR faithfully encodes semantics currently represented by Scala pipeline for active targets.
- [ ] IR lowering from `.ksy` does not require Scala compiler for normal build/test flows.
- [ ] `--from-ir` remains optional debug/internal entry point, not primary flow.

### 6) Backend output parity (active targets)

- [ ] `cpp_stl` generation matches Scala runtime contract semantics for read/write/debug behaviors used by tests.
- [ ] `lua`, `wireshark_lua`, `python`, `ruby` output matches Scala package/path/module behavior expected by harness.
- [ ] Flags parity validated (`--read-write`, `--no-auto-read`, `--read-pos`, target namespace/package options where supported).
- [ ] No artificial backend restrictions remain once implemented.

### 7) Repository workflow compatibility

- [ ] `tests/build-compiler` succeeds with C++17 path as default and without Scala requirement in normal active-target CI.
- [ ] `tests/build-formats` succeeds for all active targets in cpp17 mode with zero active exclusions.
- [ ] `tests/run-cpp_stl_17`, `tests/run-lua`, and `tests/run-python` pass using cpp17-generated outputs.

## Stop conditions (release readiness)

Promotion from migration to default-ready is blocked until all are true:

1. Zero active exclusions in `tests/migration_golden/build_formats_exclusions.tsv` for active targets.
2. Zero required mismatches in `tests/migration_golden/cpp17_differential_fixtures.tsv` differential checks.
3. Green `tests/run-cpp_stl_17`, `tests/run-lua`, and `tests/run-python` with cpp17 outputs.
4. CLI and diagnostics parity validated against Scala baseline fixture matrix.
5. Default engine in repository workflows switched to cpp17, with Scala no longer required for normal active-target build/test flows.
