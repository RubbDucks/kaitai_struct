# Draft release notes: Scala → C++17 compiler migration (pre-default-flip)

## Summary

This release line continues the compiler migration by expanding readiness and
validation for the experimental C++17 path while keeping Scala as default.

- Default engine remains Scala.
- C++17 path remains opt-in using `KAITAI_COMPILER_ENGINE=cpp17`.
- Readiness gates now include explicit parity/performance/checklist signals.

## Operator caveats

- **No default behavior change yet**: users who do not set
  `KAITAI_COMPILER_ENGINE` continue on the Scala compiler path.
- **Target scope**: C++17 migration checks in this fork are focused on active
  targets (`cpp_stl`, `lua`, `wireshark_lua`, `python`, `ruby`).
- **Known deviations are explicit**: migration differential fixtures may include
  `known_mismatch_allowed` entries that are visible in reports and treated as
  non-blocking unless promoted to required.
- **Benchmark thresholds are policy-driven**: benchmark gates compare C++17
  against Scala baseline using thresholded latency/memory/stability ratios.

## Planned default-flip prerequisites

The default flip will be considered only after the readiness checklist in
`doc/cpp17_default_flip_readiness.md` reaches all-pass status on CI and release
qualification runs.

## Rollback and fallback

If migration regressions are detected during rollout, operators should force the
stable path with:

```sh
unset KAITAI_COMPILER_ENGINE
# or force explicitly
KAITAI_COMPILER_ENGINE=scala ./build-compiler
```

Fallback support window and policy are documented in
`doc/cpp17_fallback_policy.md`.

