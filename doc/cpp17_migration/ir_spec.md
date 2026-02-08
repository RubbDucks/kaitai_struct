# C++17 Migration IR Specification (v1)

This document defines the first shared migration IR boundary between the existing Scala compiler path and the experimental C++17 path.

## Status and scope

- **Status:** experimental migration contract
- **Version marker:** `KSIR1`
- **Default authority:** Scala remains the production compiler path; this IR is an opt-in migration artifact.
- **Out of scope in this commit:** backend code generation in C++.

## Design goals

1. Capture the core Kaitai Struct concepts needed to bridge frontend semantics to future backends.
2. Keep the schema narrow and testable in early migration commits.
3. Make invariants explicit so malformed IR fails fast.

## Schema overview

A `Spec` object contains:

- `name` (**required**): top-level KS type name (`meta/id` conceptually).
- `default_endian` (**required**): either `le` or `be`; maps to `meta/endian` default semantics.
- `imports`: optional IR sidecar import list (relative/absolute file paths).
- `types`: named type aliases / helper types.
- `attrs`: sequence-style attributes (`seq` entries conceptually).
- `instances`: computed/lazy fields (`instances` conceptually).
- `validations`: declarative validation predicates (maps to `valid/*`).

### Type system (`TypeRef`)

`TypeRef` supports:

- `primitive`: one of `u1/u2/u4/u8/s1/s2/s4/s8/f4/f8/str/bytes`
- `user`: reference to another type by name

### Attributes (`Attr`)

Each attribute includes:

- `id` (**required**)
- `type` (**required**)
- `endian_override` (optional, `le`/`be`), overriding spec default when present
- `size_expr` (optional expression), mapping to `size`-style KS constraints
- `enum_name` (optional), for integer attrs that map to named enums
- `encoding` (optional), for `str` attrs when encoding is known

### Expressions (`Expr`)

Minimal expression coverage in v1:

- integer literals
- boolean literals
- name references
- unary operators
- binary operators

Expression text form is prefix S-expression:

- `(int 1)`
- `(bool true)`
- `(name "len")`
- `(un "!" (name "bad"))`
- `(bin "+" (name "len") (int 4))`

### Enums (`EnumDef`)

- `name` (**required**)
- `values`: ordered `(value, name)` entries

### Instances (`Instance`)

- `id` (**required**)
- `value_expr` (**required**) for computed value expression

### Validations (`Validation`)

- `target` (**required**) logical field/instance identifier being constrained
- `condition_expr` (**required**) boolean expression
- `message` (optional text; currently serialized as string, may be empty)

## Textual wire format

Current round-trip format is a deterministic line-based encoding:

- starts with `KSIR1`
- section counts for `types`, `attrs`, `enums`, `instances`, `validations`
- quoted strings for names/messages and expression payloads
- ends with `end`

This format is intentionally simple for migration bring-up and testability; later commits may introduce canonical JSON while preserving field semantics.

## Invariants (validated in tests)

- `spec.name` is required.
- user `TypeRef` requires non-empty user type name.
- user `TypeRef` must resolve to a declared type name (or the top-level spec name).
- type aliases in `types` must not form reference cycles.
- imported IR files are resolved relative to the current file first, then `--import-path` roots.
- import graphs must be acyclic.
- imported `types` and `enums` must not create duplicate symbol names after merge.
- `attr.id` is required.
- `instance.id` is required.
- `validation.target` is required.
- expression parse/structure must be syntactically valid.

## Mapping to existing KS concepts

- `Spec.default_endian` ↔ KS `meta/endian`
- `Spec.attrs` ↔ KS `seq`
- `Attr.size_expr` ↔ KS `size`
- `Spec.instances` ↔ KS `instances`
- `Spec.validations` ↔ KS `valid/*` checks
- `TypeRef.user` ↔ user-defined / imported type references


## Scala sidecar export (opt-in)

The Scala compiler can export this IR as a sidecar artifact using:

```sh
kaitai-struct-compiler -t <lang> --emit-ir <path> <file>.ksy
```

Behavior:

- `--emit-ir` is **opt-in** and does not change normal codegen outputs.
- For single input, `<path>` may be a file path (for example `out/spec.ksir`).
- For multiple inputs, `<path>` is treated as a directory and one `<spec>.ksir` is emitted per input.

## TODO

- Expand expression coverage (`if`, casts, enum refs, IO helpers) to match Scala semantics.
- Repetition and switch metadata are now serialized on `attr` rows in the experimental text wire format (repeat kind, repeat expr, `if`, switch-on/cases).
- Add schema version negotiation rules for forward/backward compatibility.
