# C++17 Migration Risk Register

This register tracks top migration risks, severity, indicators, and mitigations.

| ID | Risk | Likelihood | Impact | Early indicators | Mitigation | Owner | Status |
|---|---|---|---|---|---|---|---|
| R-001 | Behavioral drift between Scala and C++17 outputs | Medium | High | Golden test diffs increase; target-specific regressions | Keep Scala as authoritative oracle; add parity checks before promotion; gate by phased rollout | Compiler maintainers | Open |
| R-002 | Premature default flip causing user disruption | Low | High | Increase in migration-related bug reports; CI instability | Enforce explicit flip commit policy; require parity/perf sign-off and rollback drill | Release + compiler maintainers | Open |
| R-003 | Incomplete architectural boundaries cause scope creep | Medium | Medium | Unclear ownership; mixed concerns in PRs | Maintain boundary docs in `README.md`; require ADR for boundary changes | Compiler maintainers | Open |
| R-004 | CI cost/time growth from dual-path validation | Medium | Medium | Longer queue times; flaky non-deterministic checks | Start C++17 in non-blocking shadow mode; optimize/check split by phase | CI maintainers | Open |
| R-005 | Insufficient rollback readiness | Low | High | No documented disable switch; incident response delays | Define rollback per phase; validate disable procedures during milestones | Compiler + release maintainers | Open |

## Review cadence

- Review at least once per migration phase.
- Update risk status whenever a new ADR is accepted.
- Escalate any High impact risk lacking a tested mitigation plan.
