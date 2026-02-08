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
- Java (JDK, Maven/Gradle for tests; TestNG jars are used)
- JavaScript (Node.js + npm/yarn)
- Lua
- Python (pip + virtualenv recommended)
- Ruby (bundler)
- Rust (rustup/cargo)

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
- On fresh Ubuntu images used here, `sbt` is often missing even when Java is present; install sbt repo keys/lists first, then `apt-get install sbt` before running `tests/build-compiler`.
- When debugging Python test regressions, remember the known pitfalls:
  - missing `PY2` compatibility export in `runtime/python/kaitaistruct.py`
  - failed short reads on seekable streams must rewind before raising EOF
- `tests/build-formats` does not accept `cpp_stl_17` as a target; generate C++17 artifacts with `tests/../compiler/jvm/target/universal/stage/bin/kaitai-struct-compiler -- --verbose file -t cpp_stl --cpp-standard 17 -d tests/compiled/cpp_stl_17 ...` before `tests/run-cpp_stl_17`.


## Maintenance reminder for future agents

- If you discover any environment quirk, test prerequisite, command nuance, or recurring failure mode while working here, **add a short note to this AGENTS.md immediately** so future runs are faster and less error-prone.


## Minimum execution bar for change requests

- Don't be a quiter! keep trying things until you figure it out.
- The last thing you should always do before submitting your changes back to the uses is run the c++, lua and python Tests and if they dont pass all of there tests then you need to keep going until you have fix it and they all pass!

- `tests/run-lua` requires generated modules under `tests/compiled/lua`; run `tests/build-formats lua` first if modules are missing.
- On Ubuntu, `lua-iconv` apt package may only satisfy Lua 5.1; for Lua 5.4 tests you may need `liblua5.4-dev` + `luarocks --lua-version=5.4 install lua-iconv`.
- If `tests/run-lua` can't find `luaunit` in `/usr/local/share/lua/5.4`, your `lua` alternative may still point to 5.1; set both alternatives to 5.4:
  - `update-alternatives --set lua-interpreter /usr/bin/lua5.4`
  - `update-alternatives --set lua-compiler /usr/bin/luac5.4`
- Installing `lua5.1` (often pulled in by distro `luarocks` packages) can silently switch the `lua`/`luac` alternatives away from 5.4; re-run both `update-alternatives --set ...5.4` commands before `tests/run-lua`.
- `tests/run-python` depends on generated `tests/compiled/python/testformats` and `tests/compiled/python/testwrite`; run `tests/build-formats python` before executing it.
- `tests/build-formats` accepts one language target per invocation in this environment; run it separately (for example `./build-formats python` then `./build-formats lua`) to ensure both output trees are generated.
- If `tests/run-cpp_stl_17` fails with missing generated headers (for example `imports_rel_1.h`), rebuild the compiler (`tests/build-compiler`) and regenerate C++17 outputs with `../compiler/jvm/target/universal/stage/bin/kaitai-struct-compiler -- --verbose file -t cpp_stl --cpp-standard 17 -d tests/compiled/cpp_stl_17 --import-path ../formats --import-path formats/ks_path formats/*.ksy`.
- `tests/run-cpp_stl_17` can take a long time and produce very verbose compiler output; when debugging CI-like runs locally, redirect stdout/stderr to a log file and tail `tests/test_out/cpp_stl_17/build-*.log` for progress.
- `tests/run-cpp_stl_17` can finish with exit code 0 even after excluding many specs during auto-recovery; check the `success on run attempt` line for non-zero excluded test counts when judging coverage quality.
- For Scala stage compiler invocations that pass short `-d` outdir while also compiling `.ksy` inputs (including migration golden scripts), insert `--` before `-d` to prevent outdir from being parsed as an input file.
- `tests/migration_golden/run_cpp17_benchmarks.py` depends on GNU `time` at `/usr/bin/time`; install package `time` if memory metrics preflight fails.

- `tests/run-cpp_stl_17` may spend a long time in the initial CMake build phase (compiling hundreds of generated specs); expect progress percentages to advance slowly and monitor `tests/test_out/cpp_stl_17/build-*.log` rather than assuming a hang.
