#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${TMPDIR:-/tmp}/linux30-modernization-verify"

command -v cmake >/dev/null
command -v python3 >/dev/null

rm -rf "$BUILD"
echo '[1/4] Configure portable C verification'
cmake -S "$HERE" -B "$BUILD"

echo '[2/4] Build portable C verification with warnings-as-errors'
cmake --build "$BUILD" -j2

echo '[3/4] Run allocation policy tests'
ctest --test-dir "$BUILD" --output-on-failure

echo '[4/4] Run kernel-source architecture/regression contracts'
python3 "$HERE/source_contract_test.py"

echo 'PASS: local verification completed'
