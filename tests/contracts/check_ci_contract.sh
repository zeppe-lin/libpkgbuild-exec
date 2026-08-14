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
  'zeppe-lin/libpkgsource, ref: v4.1.0' \
  'zeppe-lin/libpkgstate, ref: 94e59e64b842a396bf5bb9eacc0f262c1e266c5f' \
  'zeppe-lin/libpkgimage, ref: 179f14759b48f006dc922579314d56637f0522a8' \
  'zeppe-lin/libpkgcatalog, ref: v4.0.0' \
  'zeppe-lin/libpkgresolve, ref: v4.0.0' \
  'zeppe-lin/libpkgbuild, ref: v3.0.1' \
  'zeppe-lin/libpkgfetch, ref: v3.0.0' \
  'zeppe-lin/libpkgexec, ref: v2.1.1' \
  'zeppe-lin/libpkgbuild-image, ref: v1.0.1'
do
  count=$(grep -F "$expected" "$workflow" | wc -l | tr -d ' ')
  [ "$count" -eq 2 ] || fail "current authority checkout count is $count, expected 2: $expected"
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
