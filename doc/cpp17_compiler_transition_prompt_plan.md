# Scala-to-C++17 Compiler Transition Prompt Plan (Multi-Commit, Zero-Downtime)

This plan is a **prompt pack** you can hand to an implementation agent one commit at a time.

Design goals:
- Keep existing Scala compiler fully functional at every step.
- Introduce a C++17 compiler implementation gradually behind explicit flags.
- Require verifiable checks in each commit before moving forward.
- Avoid big-bang rewrites.

---

## Global constraints to prepend to every prompt

Use this block at the top of each implementation prompt:

```text
You are implementing one migration commit in Kaitai Struct.
Hard constraints:
1) Do not delete Scala compiler code or Scala build/test wiring.
2) Preserve existing behavior by default.
3) New C++17 path must be opt-in until explicitly flipped in a later commit.
4) Keep changes scoped to this commit objective only.
5) Add/update tests and documentation for this commit.
6) Ensure checks pass before commit:
   - existing Scala path checks
   - new C++ path checks relevant to the commit
7) If blocked, add a clear TODO section in docs and a failing test marked/isolated to document the gap.
```

---

## Commit 01 — Baseline + architecture record

**Prompt:**
```text
Create migration scaffolding docs only.

Tasks:
- Add doc/cpp17_migration/README.md with:
  - goals/non-goals
  - architectural boundaries (frontend IR, backend emitters, runtime assumptions)
  - compatibility matrix (Scala path authoritative; C++ path experimental)
  - phased rollout policy and rollback strategy
- Add doc/cpp17_migration/decision_log.md template with first ADR-like entry.
- Add doc/cpp17_migration/risk_register.md with top risks and mitigations.

No production code changes in this commit.
```

**Verification:**
- `git diff --name-only` contains docs only.
- Review for explicit statement: Scala remains default and required.

---

## Commit 02 — Shared golden test contract

**Prompt:**
```text
Create a language-agnostic compiler output contract for migration.

Tasks:
- Define canonical normalization rules for generated output comparisons.
- Add test fixtures list representing minimum viable coverage:
  primitives, enums, instances, switch-on, repeats, imports, process, encoding, validation, errors.
- Add a script (or test harness doc if scripting is not feasible yet) describing how to run:
  Scala compiler -> normalize -> snapshot.
- Store first baseline snapshots under tests/migration_golden/scala/.

Keep Scala build untouched.
```

**Verification:**
- Contract document is explicit about stable/unstable output fields.
- Snapshot inventory exists and is reproducible.

---

## Commit 03 — C++17 workspace skeleton

**Prompt:**
```text
Introduce C++17 project skeleton without integrating into default build.

Tasks:
- Add compiler-cpp/ with CMake project skeleton.
- Add minimal executable (e.g., kscpp) printing version + "experimental".
- Add formatting/lint config for C++ (clang-format file and style doc).
- Add build instructions in doc/cpp17_migration/building_cpp.md.

Do not wire this executable as default compiler.
```

**Verification:**
- CMake configure/build for compiler-cpp succeeds.
- Existing Scala compiler commands remain unchanged.

---

## Commit 04 — CLI compatibility shell

**Prompt:**
```text
Implement a CLI parser shell in C++17 matching current Scala CLI surface syntax.

Tasks:
- Mirror key flags/options/help text structure in C++ executable.
- Parse args into a structured options object.
- Return "not implemented" for compilation execution path while preserving exit-code semantics for parse errors/help/version.
- Add unit tests for option parsing and invalid combinations.

No code generation yet.
```

**Verification:**
- `--help` and `--version` available in C++ binary.
- Parsing tests pass.
- Scala CLI behavior remains baseline.

---

## Commit 05 — Intermediate Representation (IR) schema definition

**Prompt:**
```text
Define a migration IR boundary shared conceptually by Scala and C++.

Tasks:
- Document IR schema in doc/cpp17_migration/ir_spec.md.
- Implement C++ IR data structures (types, attrs, endian, expressions, instances, validations).
- Add round-trip serialization tests for IR (JSON or equivalent textual form).

No generator backend in C++ yet.
```

**Verification:**
- IR tests validate required fields and invariants.
- Spec clearly maps to existing KS concepts.

---

## Commit 06 — Scala exporter to IR (sidecar)

**Prompt:**
```text
Add sidecar IR export from Scala pipeline without changing normal outputs.

Tasks:
- Add opt-in flag in Scala compiler (example: --emit-ir <path>).
- Export parsed/validated model into IR format.
- Add tests comparing exported IR to expected fixtures for representative formats.
- Ensure standard compile path is unchanged when flag is absent.
```

**Verification:**
- Existing tests pass without --emit-ir.
- New IR export tests pass with deterministic output.

---

## Commit 07 — C++ IR loader + validator

**Prompt:**
```text
Implement C++ IR ingestion.

Tasks:
- Add parser/loader for IR produced by Scala exporter.
- Add semantic validator (required fields, type references, cycle checks where applicable).
- Add unit tests for valid and invalid IR payloads.
- CLI supports: kscpp --from-ir <file> (no codegen yet, just validate).
```

**Verification:**
- Valid IR returns success.
- Invalid IR returns clear diagnostics and non-zero status.

---

## Commit 08 — First backend slice in C++ (hello_world)

**Prompt:**
```text
Implement first C++ backend codegen slice from IR for smallest subset.

Tasks:
- Support minimal subset sufficient for hello_world-like specs.
- Emit deterministic output for one target (start with cpp_stl to reduce cross-language variables).
- Add golden tests: Scala-generated output vs C++-generated output after normalization.

Keep feature-gated with explicit experimental flag.
```

**Verification:**
- Golden comparison for supported subset passes.
- Unsupported constructs produce explicit "not yet supported" diagnostics.

---

## Commit 09 — Expression engine parity slice A

**Prompt:**
```text
Expand C++ backend support for expression subset A.

Tasks:
- Implement literals, arithmetic, boolean ops, and basic field references.
- Add focused tests for precedence and parenthesization in generated code.
- Add parity tests against Scala outputs for affected fixtures.
```

**Verification:**
- New expression fixtures pass parity checks.
- No regressions in previously supported subset.

---

## Commit 10 — Type system parity slice A

**Prompt:**
```text
Expand type handling in C++ path.

Tasks:
- Add integers (signed/unsigned widths), floats, byte arrays, strings (with encodings where already supported by Scala path).
- Add enum emission for subset fixtures.
- Update migration matrix in docs with implemented vs missing.
```

**Verification:**
- Type-focused fixture parity green.
- Docs accurately reflect current implementation.

---

## Commit 11 — Control-flow constructs slice

**Prompt:**
```text
Implement repeat/if/switch core behavior in C++ backend.

Tasks:
- Add repeat-eos/repeat-expr/repeat-until handling.
- Add conditional fields and switch-on types for selected fixture set.
- Add negative tests for malformed switch cases and unsupported dynamic behavior.
```

**Verification:**
- Control-flow fixtures parity green.
- Failures are diagnosable and deterministic.

---

## Commit 12 — Imports and namespace resolution

**Prompt:**
```text
Implement import graph handling in C++ path.

Tasks:
- Resolve relative/absolute imports in IR/codegen flow.
- Add cycle detection and duplicate-symbol diagnostics.
- Add fixtures with nested imports and namespace collisions.
```

**Verification:**
- Import fixtures parity green.
- Clear error messages for cycles/collisions.

---

## Commit 13 — Instances, validations, and process clauses

**Prompt:**
```text
Implement advanced semantic features in C++ path.

Tasks:
- Add instance generation parity for representative patterns.
- Add validation predicates and error text compatibility checks.
- Add process clauses subset parity (as feasible with current runtime assumptions).
```

**Verification:**
- Advanced fixtures pass parity or are explicitly listed as known gaps.

---

## Commit 14 — Differential test runner (Scala vs C++)

**Prompt:**
```text
Add automated differential testing in tests/.

Tasks:
- Build a runner that compiles same fixture set via Scala and C++ paths.
- Normalize outputs and diff with concise summaries.
- Emit machine-readable report (json) + human summary.
- Add CI hook in non-blocking mode first (informational).
```

**Verification:**
- Runner works locally and in CI informational mode.
- Report artifacts are easy to inspect.

---

## Commit 15 — Expand parity coverage to fork’s active targets

**Prompt:**
```text
Broaden coverage for this fork’s active targets: cpp_stl, lua, wireshark_lua, python, ruby.

Tasks:
- For each target, add representative fixture bins and parity criteria.
- Mark target-specific known deviations with explicit rationale.
- Ensure Scala remains source of truth where C++ lacks coverage.
```

**Verification:**
- Differential report shows per-target pass/fail/gap counts.

---

## Commit 16 — Performance/regression harness

**Prompt:**
```text
Add benchmark and stability checks for migration confidence.

Tasks:
- Add compile-time benchmark harness on fixed fixture corpus.
- Capture memory/latency metrics for Scala vs C++ paths.
- Add trend report doc with baseline thresholds.
```

**Verification:**
- Bench scripts run and produce consistent metric schema.

---

## Commit 17 — Dogfood mode (opt-in production trial)

**Prompt:**
```text
Add opt-in mode to use C++ path in regular workflows.

Tasks:
- Add top-level switch (env var or explicit flag) to route through C++ compiler path.
- Keep Scala as default.
- Add telemetry/log marker indicating selected compiler engine.
- Document rollback command and troubleshooting.
```

**Verification:**
- Default path unchanged.
- Opt-in path functional for declared supported matrix.

---

## Commit 18 — CI gate (targeted blocking)

**Prompt:**
```text
Promote differential checks from informational to selective blocking.

Tasks:
- Define minimum required fixture subsets where C++ parity must be exact.
- Keep unsupported areas non-blocking but visible.
- Add CI badge/table docs for migration status.
```

**Verification:**
- CI fails only on agreed blocking subsets.
- Migration dashboard reflects true status.

---

## Commit 19 — Default flip preparation checklist

**Prompt:**
```text
Prepare readiness review for default flip.

Tasks:
- Add formal checklist doc with pass criteria: feature parity, stability, performance, diagnostics, docs.
- Add release-notes draft with migration caveats.
- Add fallback policy with time-bounded support window.
```

**Verification:**
- Checklist is measurable and mapped to automated signals.

---

## Commit 20 — Default flip (reversible)

**Prompt:**
```text
Flip default compiler engine to C++ path while retaining Scala fallback.

Tasks:
- Route default command path to C++ implementation.
- Preserve explicit flag/env to force Scala path.
- Update docs, help text, and CI defaults.
- Add post-flip smoke tests for both paths.
```

**Verification:**
- Default path uses C++.
- Scala fallback still passes regression suite.
- Rollback instruction is tested and documented.

---

## Commit 21+ — Decommission planning (optional, delayed)

**Prompt:**
```text
Do not remove Scala immediately.

Tasks:
- Track real-world issue rate under C++ default for a stabilization window.
- Only after criteria are met, propose separate decommission plan.
- Decommission itself must be separate staged commits with archival strategy.
```

**Verification:**
- Data-backed go/no-go decision logged in decision log.

---

## Reusable "verification template" prompt for each commit

```text
For this commit, add a Verification section to commit message and docs:
- What existing checks were run to confirm no Scala regressions
- What new checks validate C++ path additions
- Which fixture IDs/files were covered
- Exact pass/fail counts
- Known gaps intentionally left for next commit
```

## Reusable "PR description template"

```text
## What this commit does
(brief summary)

## Why now
(link to migration phase)

## Backward compatibility
- Scala default behavior preserved? (yes/no)
- Any user-facing flag changes?

## Verification
- Existing path checks:
- New path checks:
- Differential results:

## Known limitations
(list)

## Next commit
(reference next prompt ID)
```

