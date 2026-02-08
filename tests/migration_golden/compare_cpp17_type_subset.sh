#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
NORMALIZER="$ROOT_DIR/tests/migration_golden/normalize_compiler_output.py"
SCALA_BIN="$ROOT_DIR/compiler/jvm/target/universal/stage/bin/kaitai-struct-compiler"
KSCXX_BIN="$ROOT_DIR/compiler-cpp/build/kscpp"

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

INPUT_KSY="$TMP_DIR/type_subset.ksy"
INPUT_IR="$TMP_DIR/type_subset.ksir"
cat > "$INPUT_KSY" <<'YAML'
meta:
  id: type_subset
  endian: le
enums:
  animal:
    7: cat
    13: dog
seq:
  - id: u8v
    type: u8
  - id: s4v
    type: s4
  - id: f4v
    type: f4
  - id: f8v
    type: f8
  - id: payload
    size: 4
  - id: name
    type: str
    size: 3
    encoding: ASCII
  - id: pet
    type: u1
    enum: animal
YAML

SCALA_OUT="$TMP_DIR/scala"
CPP_OUT="$TMP_DIR/cpp"
mkdir -p "$SCALA_OUT" "$CPP_OUT"

"$SCALA_BIN" -t cpp_stl --cpp-standard 17 --emit-ir "$INPUT_IR" -- -d "$SCALA_OUT" "$INPUT_KSY"
"$KSCXX_BIN" --from-ir "$INPUT_IR" -t cpp_stl --cpp-standard 17 -d "$CPP_OUT"

for side in scala cpp; do
  out_dir="$TMP_DIR/$side"
  {
    echo "id=type_subset"
    echo "mode=success"
    find "$out_dir" -type f \( -name '*.h' -o -name '*.cpp' \) | LC_ALL=C sort | while IFS= read -r f; do
      rel="${f#$out_dir/}"
      echo "--- FILE:$rel"
      cat "$f"
    done
  } > "$TMP_DIR/$side.raw"
  "$NORMALIZER" "$TMP_DIR/$side.raw" "$TMP_DIR/$side.norm"
done

for needle in "read_f4le" "read_f8le" "read_bytes(4)" "bytes_to_str"; do
  grep -q "$needle" "$TMP_DIR/scala.norm"
  grep -q "$needle" "$TMP_DIR/cpp.norm"
done

grep -q "enum" "$TMP_DIR/scala.norm"
grep -q "enum" "$TMP_DIR/cpp.norm"

echo "[migration-golden] Scala and C++17(IR) type subset feature parity checks passed"
