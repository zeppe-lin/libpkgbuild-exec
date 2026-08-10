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
  'zeppe-lin/libpkgsource, ref: 9a2a85c85c20bbfa77306f3eb14ccc67ac1e800c' \
  'zeppe-lin/libpkgstate, ref: f74df278b47b48e798c3de01c922c59b58319d13' \
  'zeppe-lin/libpkgimage, ref: 284324996dce673e1a96d73f8adb90b29dbb79f5' \
  'zeppe-lin/libpkgcatalog, ref: 16976cac176f576871e327d5d2f6fe9d9dfa0666' \
  'zeppe-lin/libpkgresolve, ref: 4272e066d35a4ea7a13d7e7dc8c6d536a5490785' \
  'zeppe-lin/libpkgbuild, ref: c6dc011eb8a28009e67503a5e7a35c150e966382' \
  'zeppe-lin/libpkgfetch, ref: 18501adc5df0e308ab35ce57f009158185fdb3b3' \
  'zeppe-lin/libpkgexec, ref: 08814baf2dbc037a037f29dd7de218e5a19c7e77' \
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
