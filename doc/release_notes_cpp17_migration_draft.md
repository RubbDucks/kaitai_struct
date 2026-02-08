# Draft release notes: Scala → C++17 compiler migration (post-default-flip)

## Summary

This release line continues the compiler migration by expanding readiness and
validation while making the C++17 path the default compiler engine.

- Default engine is now C++17 for tests/build entrypoints.
- Scala path remains available via `KAITAI_COMPILER_ENGINE=scala`.
- Readiness gates now include explicit parity/performance/checklist signals.

## Operator caveats

- **Default behavior changed**: users who do not set
  `KAITAI_COMPILER_ENGINE` use the C++17 compiler path.
- **Target scope**: C++17 migration checks in this fork are focused on active
  targets (`cpp_stl`, `lua`, `wireshark_lua`, `python`, `ruby`).
- **Known deviations are explicit**: migration differential fixtures may include
  `known_mismatch_allowed` entries that are visible in reports and treated as
  non-blocking unless promoted to required.
- **Benchmark thresholds are policy-driven**: benchmark gates compare C++17
  against Scala baseline using thresholded latency/memory/stability ratios.

## Planned default-flip prerequisites

The default flip is now in effect, gated by the readiness checklist in
`doc/cpp17_default_flip_readiness.md`. Continue running those gates for
post-flip qualification runs.

## Rollback and fallback

If migration regressions are detected during rollout, operators should force the
stable path with:

```sh
KAITAI_COMPILER_ENGINE=scala ./build-compiler
# restore default after rollback testing
unset KAITAI_COMPILER_ENGINE
```

Fallback support window and policy are documented in
`doc/cpp17_fallback_policy.md`.

