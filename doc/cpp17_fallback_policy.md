# C++17 migration fallback policy (time-bounded)

## Policy intent

With the default now flipped to the C++17 compiler path, users must have a
clear, low-risk path to revert to the Scala compiler engine.

## Fallback mechanism

- Engine selection remains controlled by `KAITAI_COMPILER_ENGINE`.
- Stable fallback command:

```sh
KAITAI_COMPILER_ENGINE=scala ./build-compiler
```

## Time-bounded support window

After the default flip to `cpp17`, maintainers commit to:

1. **Dual-path support for 2 minor releases or 6 months (whichever is longer)**
   after the flip.
2. During this window:
   - Scala fallback remains available and documented.
   - Regressions found in C++17 default flow may be mitigated by immediate
     fallback guidance.
3. After the window:
   - Scala fallback deprecation/removal requires a separate proposal, explicit
     release-note callout, and migration status update.

## Escalation and rollback triggers

Recommend temporary rollback to Scala default in a hotfix/release branch when
any of the following occurs:

- Required parity gate failures in migration differential checks.
- Benchmark threshold breaches (latency/memory/stability) on release hardware.
- User-facing diagnostic regressions that block common workflows.

## Communication requirements

Any fallback recommendation must include:

- exact command to force Scala engine,
- known affected versions/ranges,
- issue tracker link and expected resolution timeline.

