# C++17 Compiler Release-Readiness (Active Targets)

This directory now tracks **release-readiness and promotion gates**, not scaffolding.
For this fork's active targets (`cpp_stl`, `lua`, `wireshark_lua`, `python`, `ruby`),
`compiler-cpp/` must be behaviorally equivalent to the Scala compiler path for normal build/test workflows.

## Normative behavior contract

- Canonical CLI semantics: `compiler/jvm/src/main/scala/io/kaitai/struct/JavaMain.scala`.
- C++17 path is considered parity-complete only when it matches Scala behavior for:
  - CLI UX and argument validation
  - diagnostics class/message expectations used by tests
  - import resolution semantics (including cycles/collisions)
  - full `.ksy` language + expression/type-checking semantics
  - backend outputs/runtime contracts for active targets
  - repository test harness integration

Detailed checklist and stop conditions are in `parity_checklist.md`.

## Done gates (blocking)

All gates below must be green simultaneously before promotion:

1. **Zero active build-format exclusions** for active targets in
   `tests/migration_golden/build_formats_exclusions.tsv`.
2. **Zero required differential mismatches** in
   `tests/migration_golden/cpp17_differential_fixtures.tsv`.
3. **Green runtime test runners using cpp17 outputs**:
   - `tests/run-cpp_stl_17`
   - `tests/run-lua`
   - `tests/run-python`
4. **CLI/diagnostics parity pass** vs Scala baseline on representative fixture matrix,
   including JSON output mode (`--ksc-json-output`).
5. **Default workflow engine flipped to cpp17** for repository build/test flows,
   with Scala no longer required for normal active-target CI.

## Promotion criteria (rollback-free)

Promotion is rollback-free only when:

- Normal contributor commands (`tests/build-compiler`, `tests/build-formats`, language runners)
  work end-to-end without Scala bootstrap requirements for active targets.
- `--from-ir` is optional/internal and not required in standard compilation paths.
- Differential and fixture coverage is broad enough to represent the full active feature surface,
  not a smoke subset.
- No "known mismatch allowed" policy remains for required fixtures.

## Current migration assets and their role

- `parity_checklist.md`: contract + stop conditions.
- `decision_log.md`: records decisions made while closing parity gaps.
- `risk_register.md`: residual risks (must trend to zero for promotion).
- `output_contract.md`: normalized output comparison rules.
- `ir_spec.md`: IR invariants during transition.
- `benchmark_trends.md`: stability/perf tracking while converging.

Documented gate examples (must match `tests/migration_golden/cpp17_differential_fixtures.tsv`):

<!-- gate-examples:start -->
- required: `cpp17_empty_parity`, `cpp17_hello_world`, `python_hello_world`
- visibility: _(none)_
<!-- gate-examples:end -->

## Policy

Until all done gates are met, cpp17 remains in migration/readiness mode.
Once all gates are met and validated in CI, migration docs move to historical context and
repo workflows/docs become cpp17-first.
