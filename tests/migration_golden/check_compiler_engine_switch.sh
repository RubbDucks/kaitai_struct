#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

cd "$ROOT_DIR"

echo "[migration-golden] verifying compiler engine default"
default_engine="$(sh -c '. ./tests/config; printf "%s" "$KAITAI_COMPILER_ENGINE"')"
if [[ "$default_engine" != "scala" ]]; then
  echo "Expected default engine 'scala', got '$default_engine'" >&2
  exit 1
fi

echo "[migration-golden] verifying cpp17 opt-in marker and target guard"
set +e
KAITAI_COMPILER_ENGINE=cpp17 ./tests/build-formats python >/tmp/ks_cpp17_switch.out 2>/tmp/ks_cpp17_switch.err
status=$?
set -e
if [[ $status -eq 0 ]]; then
  echo "Expected cpp17 build-formats python to fail (unsupported target)" >&2
  exit 1
fi
if ! grep -q "\[compiler-engine\] selected=cpp17" /tmp/ks_cpp17_switch.err; then
  echo "Expected cpp17 telemetry marker in stderr" >&2
  exit 1
fi
if ! grep -q "currently supports only target=cpp_stl" /tmp/ks_cpp17_switch.err; then
  echo "Expected unsupported-target diagnostic" >&2
  exit 1
fi

echo "[migration-golden] verifying invalid compiler engine is rejected"
if KAITAI_COMPILER_ENGINE=invalid ./tests/build-compiler >/tmp/ks_invalid_engine.out 2>/tmp/ks_invalid_engine.err; then
  echo "Expected invalid engine invocation to fail" >&2
  exit 1
fi
if ! grep -q "Unknown KAITAI_COMPILER_ENGINE" /tmp/ks_invalid_engine.err; then
  echo "Expected unknown-engine diagnostic in stderr" >&2
  exit 1
fi

echo "[migration-golden] compiler engine switch checks passed"
