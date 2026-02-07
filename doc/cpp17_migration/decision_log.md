# C++17 Migration Decision Log

Use this file as an ADR-style append-only log for migration decisions.

## ADR-0001: Migration scaffolding, governance, and safety rails

- **Date:** 2026-02-07
- **Status:** Accepted
- **Owners:** Compiler maintainers

### Context

Kaitai Struct currently relies on a Scala compiler path that is production-proven and deeply integrated with existing build/test workflows. A C++17 migration path is desired, but replacing behavior without guardrails risks regressions and ecosystem disruption.

### Decision

Adopt a documentation-first scaffolding phase with these hard policies:

1. Scala compiler path remains authoritative, default, and required.
2. C++17 compiler path is experimental and opt-in only.
3. No deletion of Scala code/build/test wiring during migration phases.
4. Every phase must define rollback mechanics before broadening rollout.

### Consequences

- Migration work is sequenced and auditable.
- Contributors can reason about boundaries before implementation begins.
- CI and release stability remain tied to Scala path until explicit promotion criteria are met.

### Reversibility

High. This ADR adds governance documentation only and does not alter production execution paths.

---

## ADR Template (copy for new entries)

### ADR-XXXX: <title>

- **Date:** YYYY-MM-DD
- **Status:** Proposed | Accepted | Superseded
- **Owners:** <team/person>

#### Context

<why this decision is needed>

#### Decision

<what is being decided>

#### Consequences

<trade-offs, impact>

#### Reversibility

<how to roll back>

#### Supersedes / Superseded by

<links/ids if applicable>
