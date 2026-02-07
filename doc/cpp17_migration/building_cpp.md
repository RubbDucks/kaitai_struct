# Building the experimental C++17 compiler skeleton

This document describes how to build the **opt-in** C++17 compiler scaffold located in `compiler-cpp/`.

> Scala remains the default and authoritative compiler path. The C++17 binary is experimental and is **not** wired into default build or test flows.

## Prerequisites

- CMake 3.16+
- A C++17-capable compiler (GCC, Clang, or MSVC)

## Configure and build

From repository root:

```sh
cmake -S compiler-cpp -B compiler-cpp/build
cmake --build compiler-cpp/build
```

## Run

```sh
./compiler-cpp/build/kscpp --help
./compiler-cpp/build/kscpp --version
```

Current CLI migration status:

- argument parsing mirrors the Scala CLI surface for key options
- `--help` and `--version` return success
- successful parse of compile-mode arguments currently returns a `Not implemented` execution path
- `--from-ir <file>` validates Scala-exported `KSIR1` payloads and exits successfully on valid IR
- `--from-ir <file> -t cpp_stl --cpp-standard 17 -d <outdir>` enables the first experimental IR→C++17 codegen slice (minimal hello-world subset)

## Run C++ skeleton checks

```sh
ctest --test-dir compiler-cpp/build --output-on-failure
```

This verifies parser unit coverage, `--help` / `--version`, and parse error exit-code behavior.

## Non-integration guarantee for this phase

- No Scala compiler wiring is changed.
- No default compiler selection behavior is changed.
- This build is standalone and must be invoked explicitly.

## Validate migration IR

```sh
./compiler-cpp/build/kscpp --from-ir compiler-cpp/tests/data/valid_sample.ksir
```

Expected behavior in this phase:

- valid IR: prints `IR validation succeeded: <spec>` and exits with status `0`
- invalid IR: prints `Error: IR validation failed: ...` and exits with non-zero status

## First IR→C++17 codegen slice (experimental, opt-in)

This commit keeps Scala as the default path and adds an **opt-in** C++17 backend slice for a minimal subset:

- supported: root `seq` attrs with primitive integer fields (`u1/u2/u4/u8/s1/s2/s4/s8`)
- supported: expression subset A for `instances` (`int`/`bool` literals, arithmetic ops, boolean ops, and basic references to attrs/earlier instances)
- unsupported (explicit diagnostic): `types`, `validations`, user types, sized attrs, and non-integer primitives

Example:

```sh
./compiler-cpp/build/kscpp --from-ir compiler-cpp/tests/data/hello_world_minimal.ksir -t cpp_stl --cpp-standard 17 -d /tmp/kscpp_out
```

Expected success output:

```
IR codegen succeeded: hello_world (target=cpp_stl, cpp_standard=17)
```

Unsupported example:

```sh
./compiler-cpp/build/kscpp --from-ir compiler-cpp/tests/data/unsupported_instance.ksir -t cpp_stl --cpp-standard 17
```

Expected failure output includes:

```
Error: C++17 IR codegen failed: not yet supported: ...
```


Parity check for expression subset A (Scala vs C++17 IR backend):

```sh
tests/migration_golden/compare_cpp17_expr_subset_a.sh
```
