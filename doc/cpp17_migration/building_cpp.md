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

## Run C++ skeleton checks

```sh
ctest --test-dir compiler-cpp/build --output-on-failure
```

This verifies parser unit coverage, `--help` / `--version`, and parse error exit-code behavior.

## Non-integration guarantee for this phase

- No Scala compiler wiring is changed.
- No default compiler selection behavior is changed.
- This build is standalone and must be invoked explicitly.
