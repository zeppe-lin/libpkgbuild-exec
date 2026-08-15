#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
root=${1:?}
fail() { echo "release-metadata-test: $*" >&2; exit 1; }
require()
{
  file=$1
  text=$2
  grep -F -- "$text" "$file" >/dev/null ||
    fail "missing in ${file#$root/}: $text"
}
require_dependency_range()
{
  variable=$1
  package=$2
  range=$3
  block=$(sed -n "/^${variable} = dependency(/,/^)/p" "$root/meson.build")
  printf '%s\n' "$block" | grep -F "  '$package'," >/dev/null ||
    fail "$variable does not name $package"
  printf '%s\n' "$block" | grep -F "  version: $range," >/dev/null ||
    fail "$variable does not require $range"
}
require "$root/meson.build" "  version: '3.3.0',"
require_dependency_range libpkgsource_dep libpkgsource "['>=4.0.0', '<5.0.0']"
require_dependency_range libpkgsource_exec_dep libpkgsource-exec "['>=0.1.0', '<1.0.0']"
require_dependency_range libpkgbuild_dep libpkgbuild "['>=3.0.1', '<4.0.0']"
require_dependency_range libpkgbuild_image_dep libpkgbuild-image "['>=1.0.1', '<2.0.0']"
require_dependency_range libpkgfetch_dep libpkgfetch "['>=3.0.0', '<4.0.0']"
require_dependency_range libpkgexec_dep libpkgexec "['>=2.1.1', '<3.0.0']"
require_dependency_range libpkgimage_dep libpkgimage "['>=0.4.0', '<1.0.0']"
check_dependency_list()
{
  variable=$1
  shift
  block=$(sed -n "/^${variable} = \[/,/^\]/p" "$root/src/meson.build")
  expected_count=$#
  actual_count=$(printf '%s\n' "$block" |
    grep -Ec '^[[:space:]]+[A-Za-z0-9_]+_dep,$' || true)
  test "$actual_count" -eq "$expected_count" ||
    fail "$variable contains $actual_count dependency objects, expected $expected_count"
  for dependency in "$@"; do
    count=$(printf '%s\n' "$block" | grep -Fxc "  $dependency," || true)
    test "$count" -eq 1 ||
      fail "$variable contains $count copies of $dependency, expected exactly one"
  done
}
check_dependency_list public_deps \
  libpkgbuild_dep libpkgbuild_image_dep libpkgfetch_dep libpkgexec_dep
check_dependency_list private_deps \
  libpkgsource_dep libpkgsource_exec_dep libpkgimage_dep libarchive_dep libcrypto_dep
require "$root/src/meson.build" "  soversion: '3',"
require "$root/src/meson.build" '  requires: public_deps,'
require "$root/src/meson.build" '  requires_private: private_deps,'
require "$root/HISTORY.md" 'Version: 3.3.0'
require "$root/HISTORY.md" 'Version: 3.2.0'
require "$root/HISTORY.md" 'Version: 3.0.1'
require "$root/HISTORY.md" 'Version: 2.3.0'
require "$root/HISTORY.md" 'Version: 2.2.0'
require "$root/HISTORY.md" 'libpkgexec 2.x'
require "$root/README.md" '# libpkgbuild-exec 3.3.0'
