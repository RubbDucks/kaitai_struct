# Kaitai Struct: specs and tests

This repository contains specifications and tests for 
[Kaitai Struct](https://github.com/kaitai-io/kaitai_struct) project.

## What's inside

The repository is laid out like that:

* `src/` - binary input files that would be parsed during the tests
* `formats/` - file formats description is Kaitai Struct YAML format
  for the files in `src/`
* `spec/` - specifications (i.e. test code) that uses format
  descriptions to parse binary input files and ensures that they're
  parsed properly.
  * `$LANGUAGE/` - one subdirectory per every supported target language

During the testing the following is expected to be created:

* `compiled/` - formats (described in `formats/`), compiled into specific programming languages modules
  * `$LANGUAGE/` - one subdirectory per every supported target language
* `test_out/` - test running output, in a language-specific format
  * `$LANGUAGE/` - one subdirectory per every supported target language

## How to test

The overall procedure of testing works as follows:

* Make sure that KS compiler (ksc) is built and ready to be used
* Compile format descriptions in `formats/` into source files in
  relevant programming languages (C++, Lua, Lua_Wireshark, Python, Ruby), which
  should be placed in `compiled/$LANGUAGE`.
* Compile and run test code for particular language (located in
  `spec/$LANGUAGE`), which will use files in `src/` for input.
* Aggregate and view results

## Automated test tools

There are a few scripts that automate steps specified above:

* `build-compiler` builds compiler using special "stage" mode,
  i.e. without system-wide deployment, ready to be run from a build
  directory
* `build-formats` compiles all format descriptions in `formats/` with
  this compiler for every supported language, placing results in
  `compiled/$LANGUAGE`
  * Engine selection is controlled by `KAITAI_COMPILER_ENGINE` (default: `scala`).
    Set `KAITAI_COMPILER_ENGINE=cpp17` for the opt-in C++17 migration flow
    (currently limited to `./build-formats cpp_stl`).
* `run-$LANGUAGE` executes all tests for a particular `$LANGUAGE`
  using preferred language-specific testing tool. The output is
  generally dumped on screen for quick assessment during development.
* `ci-$LANGUAGE` also runs all tests for a particular `$LANGUAGE`, but
  logs all output into designated log file instead (mostly useful for
  aggregation within a CI system afterwards).
* `ci-cpp17-readiness-review` runs migration default-flip readiness gates
  (checklist mapping validation, differential parity checks, and benchmark
  threshold checks).

These scripts require Kaitai Struct compiler and language-specific
runtime modules. In this repository, default locations are configured in
`tests/config` (for example `../compiler`, `../runtime/python`, and
`../runtime/lua`).

This repository currently has no git submodules. If your local checkout
uses non-default locations, edit `tests/config`.

To roll back to the stable compiler path at any time, unset the engine override:

```sh
unset KAITAI_COMPILER_ENGINE
# or force explicitly
KAITAI_COMPILER_ENGINE=scala ./build-compiler
```

For the canonical contributor happy-path and release workflow, see
`doc/workflows.adoc`.

## Continuous integration

[Main Kaitai Struct project](https://github.com/kaitai-io/kaitai_struct)
uses CI workflows (currently GitHub Actions and related integrations) to
build compiler artifacts and execute language checks. The results are
published at [Kaitai Struct CI results page](https://ci.kaitai.io).

Please refer to [CI documentation](https://doc.kaitai.io/ci.html) for
a throughout overview of how this all is tied together in a bigger
picture.
