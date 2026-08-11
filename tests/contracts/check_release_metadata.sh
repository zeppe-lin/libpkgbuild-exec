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
require "$root/meson.build" "  version: '2.2.0',"
require_dependency_range libpkgbuild_dep libpkgbuild "['>=3.0.0', '<4.0.0']"
require_dependency_range libpkgbuild_image_dep libpkgbuild-image "['>=1.0.0', '<2.0.0']"
require_dependency_range libpkgfetch_dep libpkgfetch "['>=2.0.0', '<3.0.0']"
require_dependency_range libpkgexec_dep libpkgexec "['>=2.0.0', '<3.0.0']"
require_dependency_range libpkgimage_dep libpkgimage "['>=0.4.0', '<1.0.0']"
require "$root/src/meson.build" "  soversion: '2',"
require "$root/src/meson.build" "    'libpkgexec >= 2.0.0',"
require "$root/src/meson.build" "    'libpkgexec < 3.0.0',"
require "$root/HISTORY.md" 'Version: 2.2.0'
require "$root/HISTORY.md" 'libpkgexec 2.x'
require "$root/README.md" '# libpkgbuild-exec 2.2.0'
