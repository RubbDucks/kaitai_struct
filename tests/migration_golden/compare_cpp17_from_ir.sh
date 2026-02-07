#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
NORMALIZER="$ROOT_DIR/tests/migration_golden/normalize_compiler_output.py"
SCALA_BIN="$ROOT_DIR/compiler/jvm/target/universal/stage/bin/kaitai-struct-compiler"
KSCXX_BIN="$ROOT_DIR/compiler-cpp/build/kscpp"
INPUT_KSY="$ROOT_DIR/tests/formats/hello_world.ksy"
INPUT_IR="$ROOT_DIR/compiler-cpp/tests/data/hello_world_minimal.ksir"

if [[ ! -x "$SCALA_BIN" ]]; then
  echo "[migration-golden] Scala stage compiler missing; run tests/build-compiler first" >&2
  exit 1
fi

if [[ ! -x "$KSCXX_BIN" ]]; then
  echo "[migration-golden] C++ compiler binary missing; run cmake -S compiler-cpp -B compiler-cpp/build && cmake --build compiler-cpp/build" >&2
  exit 1
fi

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

SCALA_OUT="$TMP_DIR/scala"
CPP_OUT="$TMP_DIR/cpp"
mkdir -p "$SCALA_OUT" "$CPP_OUT"

"$SCALA_BIN" -t cpp_stl --cpp-standard 17 -- -d "$SCALA_OUT" "$INPUT_KSY"
"$KSCXX_BIN" --from-ir "$INPUT_IR" -t cpp_stl --cpp-standard 17 -d "$CPP_OUT"

for side in scala cpp; do
  out_dir="$TMP_DIR/$side"
  {
    echo "id=hello_world_minimal"
    echo "mode=success"
    find "$out_dir" -type f \( -name '*.h' -o -name '*.cpp' \) | LC_ALL=C sort | while IFS= read -r f; do
      rel="${f#$out_dir/}"
      echo "--- FILE:$rel"
      cat "$f"
    done
  } > "$TMP_DIR/$side.raw"
  "$NORMALIZER" "$TMP_DIR/$side.raw" "$TMP_DIR/$side.norm"
done

diff -u "$TMP_DIR/scala.norm" "$TMP_DIR/cpp.norm"
echo "[migration-golden] Scala and C++17(IR) normalized outputs match for hello_world_minimal"
