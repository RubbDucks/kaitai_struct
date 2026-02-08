#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KSCXX_BIN="$ROOT_DIR/compiler-cpp/build/kscpp"
FIXTURE_DIR="$ROOT_DIR/compiler-cpp/tests/data/imports"

if [[ ! -x "$KSCXX_BIN" ]]; then
  echo "[migration-golden] C++ compiler binary missing; run cmake -S compiler-cpp -B compiler-cpp/build && cmake --build compiler-cpp/build" >&2
  exit 1
fi

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

"$KSCXX_BIN" --from-ir "$ROOT_DIR/compiler-cpp/tests/data/imports_nested_root.ksir" -I "$ROOT_DIR/compiler-cpp/tests/data" >"$TMP_DIR/import_ok.log"
grep -q "IR validation succeeded" "$TMP_DIR/import_ok.log"

if "$KSCXX_BIN" --from-ir "$FIXTURE_DIR/cycle/a.ksir" >"$TMP_DIR/cycle.log" 2>&1; then
  echo "[migration-golden] expected cycle fixture to fail" >&2
  exit 1
fi
grep -q "import cycle detected" "$TMP_DIR/cycle.log"

if "$KSCXX_BIN" --from-ir "$FIXTURE_DIR/collision/root.ksir" >"$TMP_DIR/collision.log" 2>&1; then
  echo "[migration-golden] expected collision fixture to fail" >&2
  exit 1
fi
grep -q "duplicate symbol across imports" "$TMP_DIR/collision.log"

echo "[migration-golden] import fixtures parity checks passed (nested imports + cycle/collision diagnostics)"
