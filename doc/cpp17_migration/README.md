# Scala-to-C++17 Compiler Migration (Scaffolding)

## Purpose

This directory tracks the staged migration plan for introducing a C++17 implementation path in the Kaitai Struct compiler while keeping the current Scala implementation authoritative.

## Goals

- Define migration boundaries and decision records before production implementation changes.
- Introduce a clear opt-in policy for the C++17 compiler path.
- Preserve current user experience and CI behavior with Scala as the default and required compiler path.
- Provide rollback mechanics for every migration phase.

## Non-goals

- No production compiler logic changes in this scaffolding commit.
- No removal or deprecation of Scala compiler code.
- No change to default compiler selection behavior.
- No runtime library behavior changes.

## Architectural boundaries

### 1) Frontend and IR

- **Current authority:** Scala frontend/parser/type checker and Scala-owned IR semantics.
- **Migration boundary:** C++17 path must consume a stable, explicitly defined IR contract.
- **Constraint:** IR meaning remains source-of-truth compatible with Scala behavior unless explicitly versioned.

### 2) Backend emitters

- **Current authority:** Existing Scala emitters remain production path.
- **Migration boundary:** C++17 emitter path is introduced behind explicit opt-in only.
- **Constraint:** Generated output parity is measured against Scala outputs for supported targets.

### 3) Runtime assumptions

- Runtime libraries (`runtime/*`) are unchanged by this scaffolding.
- Any future C++17 compiler path must honor existing runtime contracts and test harness expectations.
- Compatibility issues are tracked in `risk_register.md` before changing runtime assumptions.

## Compatibility matrix

| Area | Scala path | C++17 path |
|---|---|---|
| Status | **Authoritative, required, default** | Experimental, opt-in only |
| CI gate | Blocking | Non-blocking until promotion criteria are met |
| Feature parity source | Defines expected behavior | Must match Scala-defined behavior |
| User-facing default | Enabled | Disabled unless explicitly requested |

> **Explicit policy:** Scala remains the default and required compiler implementation throughout this migration until a later, explicit flip commit is approved.


## C++17 backend migration matrix (current experimental slice)

| Feature area | Scala path | C++17 path (opt-in) |
|---|---|---|
| Integer attrs (`u1..u8`, `s1..s8`) | Implemented | Implemented |
| Float attrs (`f4`, `f8`) | Implemented | Implemented |
| Bytes attrs (`bytes`, `size`/EOS) | Implemented | Implemented (subset) |
| String attrs (`str`, explicit `encoding`) | Implemented | Implemented (size + encoding subset) |
| Enum emission / enum-typed integer attrs | Implemented | Implemented (subset fixtures) |
| User types / `types` section | Implemented | Missing |
| Validations | Implemented | Missing |
| Advanced control flow (`switch-on`, repeats, process, etc.) | Implemented | Partial (`repeat-*`, `if`, primitive `switch-on` subset) |

## Phased rollout policy

1. **Scaffolding (current):** documentation, risk tracking, ADRs, and opt-in policy definition.
2. **Shadow mode:** C++17 path compiles in CI as informational checks only.
3. **Targeted opt-in:** selected test subsets and contributors exercise C++17 path.
4. **Parity gates:** C++17 path must satisfy documented parity/performance/reliability criteria.
5. **Default flip (future explicit commit):** only after approval and stable rollback plan.

## Rollback strategy

- Keep Scala path intact and continuously healthy in CI at all phases.
- Keep C++17 path isolated behind opt-in switches/flags until explicitly promoted.
- If regressions occur, disable C++17 opt-in path in CI and release workflows without affecting Scala defaults.
- Record rollback trigger criteria and incident notes in `decision_log.md` and `risk_register.md`.

## References

- `decision_log.md` for architecture decisions and reversibility notes.
- `risk_register.md` for migration risk tracking and mitigations.
- `output_contract.md` for language-agnostic normalized golden-output comparison rules and Scala baseline workflow.
- `ir_spec.md` for the shared migration IR boundary and invariants used by Scala/C++ interop work.
- `building_cpp.md` for configuring and validating the standalone experimental C++17 compiler skeleton.
