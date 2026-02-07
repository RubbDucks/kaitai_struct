#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
FIXTURES="$ROOT_DIR/tests/migration_golden/fixtures.tsv"
NORMALIZER="$ROOT_DIR/tests/migration_golden/normalize_compiler_output.py"
OUT_DIR="$ROOT_DIR/tests/migration_golden/scala"
STAGE_BIN="$ROOT_DIR/compiler/jvm/target/universal/stage/bin/kaitai-struct-compiler"

if [[ ! -x "$STAGE_BIN" ]]; then
  echo "[migration-golden] Scala compiler stage build not found, running tests/build-compiler"
  (cd "$ROOT_DIR/tests" && ./build-compiler)
fi

mkdir -p "$OUT_DIR"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

while IFS=$'\t' read -r id category mode ksy target; do
  [[ -z "${id}" || "${id:0:1}" == "#" ]] && continue

  work_dir="$TMP_DIR/$id"
  mkdir -p "$work_dir"
  input="$ROOT_DIR/$ksy"

  if [[ "$mode" == "success" ]]; then
    "$STAGE_BIN" --target "$target" --import-path "$ROOT_DIR/tests/formats" --outdir "$work_dir" "$input" >"$work_dir/stdout.txt" 2>"$work_dir/stderr.txt"
    cat "$work_dir/stdout.txt" "$work_dir/stderr.txt" > "$work_dir/compiler.log"

    find "$work_dir" -type f \( -name '*.py' -o -name '*.h' -o -name '*.cpp' -o -name '*.rb' -o -name '*.lua' -o -name '*.java' \) \
      | LC_ALL=C sort > "$work_dir/generated_files.list"

    {
      echo "id=$id"
      echo "category=$category"
      echo "mode=$mode"
      echo "target=$target"
      while IFS= read -r file; do
        rel="${file#$work_dir/}"
        echo "--- FILE:$rel"
        cat "$file"
      done < "$work_dir/generated_files.list"
    } > "$work_dir/raw_snapshot.txt"
  else
    set +e
    "$STAGE_BIN" --import-path "$ROOT_DIR/tests/formats" "$input" >"$work_dir/stdout.txt" 2>"$work_dir/stderr.txt"
    status=$?
    set -e
    if [[ $status -eq 0 ]]; then
      echo "Expected fixture '$id' to fail compilation" >&2
      exit 1
    fi

    {
      echo "id=$id"
      echo "category=$category"
      echo "mode=$mode"
      echo "exit_status=$status"
      cat "$work_dir/stdout.txt"
      cat "$work_dir/stderr.txt"
    } > "$work_dir/raw_snapshot.txt"
  fi

  "$NORMALIZER" "$work_dir/raw_snapshot.txt" "$OUT_DIR/$id.snapshot"
  echo "[migration-golden] wrote $OUT_DIR/$id.snapshot"
done < "$FIXTURES"

( cd "$OUT_DIR" && sha256sum *.snapshot > SHA256SUMS )
echo "[migration-golden] wrote $OUT_DIR/SHA256SUMS"
