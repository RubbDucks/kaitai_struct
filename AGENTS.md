# Kaitai Struct repository guidance

## Key documentation to know

These are the core references for the language and ecosystem behavior:

- Kaitai Struct user guide: language overview, type system, sequences/instances, expression language, streams/substreams, and advanced techniques.
- Adding a new target language: how to plan code generation mappings, runtime implementation, and the testing pipeline for a new target.
- Developers memo (in `doc/developers.adoc`): build/test expectations across compiler, tests, runtimes, docs, and visualizer.

## Component map (what lives where)

- `compiler/`: Scala-based Kaitai Struct compiler (`ksc`).
- `tests/`: shared format specs and multi-language test harness/scripts.
- `runtime/*`: runtime libraries for each target language (C++ STL, C#, Go, Java, JavaScript, Lua, Nim, Perl, PHP, Python, Ruby, Rust, Zig).
- `formats/`: curated library of `.ksy` format specs.
- `visualizer/`: Ruby-based console visualizer (`ksv`, `ksdump`).
- `doc/`: AsciiDoc sources and build scripts for documentation site.
- `benchmarks/`: benchmarks and data generators.

## Working in this repository

- There are no git submodules.
- Commit changes directly in this repository.

## Clean setup and full build/test order (from scratch)

If you need a completely clean environment and want to build/run _everything_, follow this order.
The goal is to make sure the compiler exists before compiling formats, then run per-language tests.

### 1) Install core prerequisites (compiler + common tooling)

The compiler is required for **all** tests and format builds.
Minimum requirements from the developer memo:

- Java Runtime Environment (JRE)
- sbt (Scala Build Tool)

Example Debian/Ubuntu install for sbt/JRE (from `doc/developers.adoc`):

```sh
echo "deb https://repo.scala-sbt.org/scalasbt/debian all main" | sudo tee /etc/apt/sources.list.d/sbt.list
echo "deb https://repo.scala-sbt.org/scalasbt/debian /" | sudo tee /etc/apt/sources.list.d/sbt_old.list
curl -sL "https://keyserver.ubuntu.com/pks/lookup?op=get&search=0x2EE0EA64E40A89B84B2DF73499E82A75642AC823" | sudo apt-key add
sudo apt-get update
sudo apt-get install sbt
```

### 2) Install per-language toolchains/runtimes (required for full test suite)

To run `tests/run-*` scripts across _all_ languages, install the
language toolchains and build tools for each runtime:

- C++ (gcc/clang, make/cmake, Boost for cpp_stl tests)
- C# (.NET SDK or Mono)
- Go (Go toolchain)
- Java (JDK, Maven/Gradle for tests; TestNG jars are used)
- JavaScript (Node.js + npm/yarn)
- Lua
- Nim
- Perl (plus CPAN modules used by the runtime/tests)
- PHP
- Python (pip + virtualenv recommended)
- Ruby (bundler)
- Rust (rustup/cargo)
- Zig
- Julia (if running `run-julia`)

For C++ tests specifically, make sure you have `cmake`, a C++ compiler,
`make`, `libboost-all-dev`, and `zlib` headers installed. On Debian/Ubuntu:

```sh
sudo apt-get install build-essential cmake libboost-all-dev zlib1g-dev
```

If you prefer not to install all toolchains locally, use `tests/docker-ci`
which runs tests in prebuilt images that include all dependencies.

### 3) Build the compiler (stage build for tests)

From the `tests/` directory, use the standard helper:

```sh
cd tests
./build-compiler
```

This produces a locally runnable compiler (stage build).

### 4) Compile test formats

```sh
cd tests
./build-formats
```

This generates `tests/compiled/$LANGUAGE` outputs from `tests/formats/`.

### 5) Run language tests

Run each language-specific test suite as needed:

```sh
cd tests
./run-python
./run-java
./run-ruby
# ...other run-<lang> scripts
```

Use `ci-<lang>` scripts for CI-style logs/outputs.

### C++ test caveats (cpp_stl_17 vs cpp_stl_98)

- The compiler target is `cpp_stl` with `--cpp-standard` selecting 98 or 11.
  When invoking the compiler manually, remember that `-d` needs to be
  preceded by `--` so the output directory is not parsed as an input file.
- Some test formats (e.g., `expr_calc_array_ops`) require C++11 features such
  as literal arrays. That means the `cpp_stl_98` build can fail due to missing
  generated headers; prefer running `./run-cpp_stl_17` for full coverage.
- If you still need C++98 runs, expect failures on formats that require C++11.

### Python test caveats

- Python tests use `runtime/python` via `PYTHONPATH` (see `tests/run-python`), not
  the PyPI package. If you see `ImportError: cannot import name 'PY2'`, ensure
  the runtime defines `PY2` for compatibility with older specwrite helpers.
- Some read/write tests rely on correct stream positioning after short reads.
  If EOF tests report incorrect positions, verify the runtime rewinds on failed
  `_read_bytes_not_aligned` reads for seekable streams.
- Use `./ci-python` to run `spec` and `specwrite` suites separately when
  debugging write-path failures.

## Build & test entry points (by component)

Use these as starting points; they reflect the standard workflows documented in each component.

### Compiler (`compiler/`)

- Built with sbt (Scala). From `compiler/`:
  ```sh
  sbt test
  ```
- For JVM packaging (optional):
  ```sh
  sbt compilerJVM/universal:packageBin
  ```

### Tests (`tests/`)

- The test harness assumes the compiler and runtimes exist in default component locations.
- Standard flow (from `tests/`):
  ```sh
  ./build-compiler
  ./build-formats
  ./run-python    # or run-java, run-ruby, etc.
  ```
- Per-language runners are `run-<lang>` scripts.

### Documentation (`doc/`)

- Build the HTML docs with Asciidoctor and bundler:
  ```sh
  bundle install
  make
  ```

### Visualizer (`visualizer/`)

- Ruby-based; install dependencies and run locally:
  ```sh
  bundle install
  bin/ksv --help
  ```

### Runtimes (`runtime/*`)

- Each runtime has its own language-specific build/test workflow; check its README for details.

### Formats (`formats/`)

- Primarily `.ksy` specs. Use the compiler or Web IDE to validate changes.

## Language concepts (from the user guide)

When editing `.ksy` specs or generator logic, keep these core concepts in mind:

- Sequence-driven parsing with explicit types and sizes.
- Instances (computed or parsed after the main sequence) for derived values.
- Repetition, conditionals, and switch-on expressions for variant layouts.
- Streams/substreams with size-limits, absolute/relative positioning, and processing (compression/encryption) steps.

## Adding a new target language (high level)

- Map KS types/structures, expression semantics, and stream access to the target language idioms.
- Implement a runtime library in that language.
- Manually compile a sample `.ksy` (hello_world) and build out tests.
- Integrate with the shared test suite in `tests/`.

## Contribution hygiene

- Keep commits scoped to the correct component.
- Prefer running component-local tests before broad changes.
- For user-facing changes, update documentation in `doc/` when appropriate.

## Local environment acceleration notes (important)

- This repository fork intentionally keeps only these targets: `cpp_stl`, `lua`, `wireshark_lua`, `python`, `ruby`.
- For C++ tests, `tests/run-cpp_stl_17` expects generated sources in `tests/compiled/cpp_stl_17/`. Generate them with:
  - `kaitai-struct-compiler -- --verbose file -t cpp_stl --cpp-standard 17 -d tests/compiled/cpp_stl_17 ...`
  - If you only run `tests/build-formats cpp_stl`, you'll populate `compiled/cpp_stl` (not `compiled/cpp_stl_17`) and C++ tests can degrade to empty/partial trees.
- Lua runtime/tests need Lua >= 5.3 and < 5.5 (5.3/5.4). Some environments default to Lua 5.1; switch interpreter before running tests.
- Lua test dependencies used by `tests/run-lua`: `luaunit`, `luafilesystem`; for broad encoding coverage install `lua-iconv` too.
- If package installation suddenly fails with 404 from Ubuntu mirrors, run `apt-get update` and retry.
- When debugging Python test regressions, remember the known pitfalls:
  - missing `PY2` compatibility export in `runtime/python/kaitaistruct.py`
  - failed short reads on seekable streams must rewind before raising EOF

## Maintenance reminder for future agents

- If you discover any environment quirk, test prerequisite, command nuance, or recurring failure mode while working here, **add a short note to this AGENTS.md immediately** so future runs are faster and less error-prone.


## Minimum execution bar for change requests

- Do not ship “single-line” config-only edits for migration requests (like language standard bumps) unless the request explicitly asks for that only.
- For version/standard upgrades, update **all** relevant naming and entry points (scripts, directories, docs, aliases/targets), then run at least one real build + one real test command for the affected component before committing.
- In final reports, include the exact commands run and whether they passed/failed so reviewers can reproduce quickly.
