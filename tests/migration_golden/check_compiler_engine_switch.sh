#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

cd "$ROOT_DIR"

echo "[migration-golden] verifying compiler engine default"
default_engine="$(sh -c '. ./tests/config; printf "%s" "$KAITAI_COMPILER_ENGINE"')"
if [[ "$default_engine" != "cpp17" ]]; then
  echo "Expected default engine 'cpp17', got '$default_engine'" >&2
  exit 1
fi

echo "[migration-golden] verifying cpp17 opt-in marker and active-fork target support"
set +e
BUILD_FORMATS_SUMMARY_PATH=/tmp/ks_cpp17_switch_summary.json KAITAI_COMPILER_ENGINE=cpp17 ./tests/build-formats python >/tmp/ks_cpp17_switch.out 2>/tmp/ks_cpp17_switch.err
status=$?
set -e
if [[ $status -ne 0 ]]; then
  echo "Expected cpp17 build-formats python to succeed" >&2
  exit 1
fi
if ! grep -q "\[compiler-engine\] selected=cpp17" /tmp/ks_cpp17_switch.err; then
  echo "Expected cpp17 telemetry marker in stderr" >&2
  exit 1
fi
python3 ./tests/migration_golden/require_build_formats_summary.py /tmp/ks_cpp17_switch_summary.json --expect-engine cpp17 --expect-target python

echo "[migration-golden] verifying explicit scala fallback marker"
set +e
BUILD_FORMATS_SUMMARY_PATH=/tmp/ks_scala_switch_summary.json KAITAI_COMPILER_ENGINE=scala ./tests/build-formats python >/tmp/ks_scala_switch.out 2>/tmp/ks_scala_switch.err
status=$?
set -e
if [[ $status -ne 0 ]]; then
  echo "Expected scala build-formats python to succeed" >&2
  exit 1
fi
if ! grep -q "\[compiler-engine\] selected=scala" /tmp/ks_scala_switch.err; then
  echo "Expected scala telemetry marker in stderr" >&2
  exit 1
fi
python3 ./tests/migration_golden/require_build_formats_summary.py /tmp/ks_scala_switch_summary.json --expect-engine scala --expect-target python

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
