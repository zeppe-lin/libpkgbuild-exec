#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
root=${1:?}
fail() { echo "ci-contract: $*" >&2; exit 1; }
workflow=$root/.github/workflows/ci.yml
qualify=$root/ci/qualify.sh
consumer=$root/tests/installed/consumer.cpp

for expected in \
  'zeppe-lin/libpkgsource, ref: d5f30663a4e56c2319f301ca762741106dea1bd0' \
  'zeppe-lin/libpkgstate, ref: f74df278b47b48e798c3de01c922c59b58319d13' \
  'zeppe-lin/libpkgimage, ref: 284324996dce673e1a96d73f8adb90b29dbb79f5' \
  'zeppe-lin/libpkgcatalog, ref: 16976cac176f576871e327d5d2f6fe9d9dfa0666' \
  'zeppe-lin/libpkgresolve, ref: 46c19d5d17a3e232597bf4c588733ad92db2dfbf' \
  'zeppe-lin/libpkgbuild, ref: f6c13ed438b5e448f53b0603f060c339018e9f32' \
  'zeppe-lin/libpkgfetch, ref: ab3024f7897f40b2b0302f9ee3d4e53868d8a28e' \
  'zeppe-lin/libpkgexec, ref: 351cd87b86c3b007f4a9789a3f6948a27d9b29c3' \
  'zeppe-lin/libpkgbuild-image, ref: a8077e6d6a5143a5a7c6537d001703615562f4e7'
do
  grep -F "$expected" "$workflow" >/dev/null ||
    fail "current authority checkout is absent: $expected"
done
! grep -F 'yaml_adapter' "$workflow" >/dev/null ||
  fail 'obsolete embedded YAML adapter option remains in CI'
! grep -F 'planner_adapter' "$workflow" >/dev/null ||
  fail 'obsolete embedded planner adapter option remains in CI'
grep -F 'meson install -C "$build"' "$qualify" >/dev/null ||
  fail 'product is not installed before installed-consumer qualification'
grep -F 'pkg-config --static --libs libpkgbuild-exec' "$qualify" >/dev/null ||
  fail 'static installed pkg-config closure is not qualified'
grep -F 'pkgbuild_exec::execute(session, backend)' "$consumer" >/dev/null ||
  fail 'installed consumer does not execute build realization'
grep -F 'pkgbuild_exec::encode_build_execution_result(result)' "$consumer" >/dev/null ||
  fail 'installed consumer does not execute durable encoding'
if grep -E '&[[:space:]]*pkgbuild_exec::|decltype\(&pkgbuild_exec::' "$consumer" >/dev/null; then
  fail 'installed consumer regressed to function-address qualification'
fi
