# ValidateSupportedSubset `not yet supported` audit

This audit classifies every `not yet supported:` branch in `compiler-cpp/src/codegen.cpp` by feature area for migration tracking.

## Types

- `attr type must resolve to primitive type`
  - Triggered when an `attr` type (or alias chain) does not resolve to a primitive leaf.
- `primitive attr type in this migration slice`
  - Defensive guard for any primitive outside the currently modeled primitive set.
- `switch-on case type must resolve to primitive type`
  - `switch` case target type alias did not resolve to primitive.
- `switch-on cases must share one primitive type`
  - Mixed primitive storage types across switch cases are currently rejected.

## Expressions

- `expression name reference outside attrs/instances: <name>`
  - Expression name references that do not resolve to known attrs/instances (except `_` repeat item).
- `unary operator "<op>"`
  - Unary operator outside current migrated subset (`-`, `!`, `not`, `~`).
- `binary operator "<op>"`
  - Binary operator outside current migrated subset (`+ - * / % == != > >= < <= && || and or & | ^ xor << >>`).
- `unknown expression kind`
  - Defensive branch for future/new IR expression node kinds.

## Switches / control flow

- `malformed switch cases (duplicate else)`
  - More than one fallback (`else`) case in the same switch case list.

## Validations

- `validation target outside attrs/instances: <name>`
  - Validation target is not a declared attr or instance.

## String / enum semantics

- `encoding outside str attrs`
  - `encoding` present for non-string attributes.
- `empty enum name`
  - Enum declaration contains empty name.
- `attr.enum_name references unknown enum`
  - Attr references enum that is not declared in scope.
- `enum attrs must be integer-backed`
  - Enum-typed attrs must use integer primitive storage.

## Processing

- No `not yet supported` diagnostics currently exist specifically for processing in this validator.
  - Existing process support in this migration path remains limited at emission time (for example, xor const handling), but current validation does not gate with a dedicated `not yet supported` branch.
