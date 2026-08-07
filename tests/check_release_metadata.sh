#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
root=$1

grep -q "version: '2.2.0'" "$root/meson.build"
grep -q "soversion: '2'" "$root/src/meson.build"
grep -q '^Version: 2.2.0$' "$root/HISTORY.md"
grep -q '^Date: 2026-08-07$' "$root/HISTORY.md"
grep -q '^# libpkgbuild-exec 2.2.0$' "$root/README.md"
grep -q "libpkgbuild >= 3.0.0" "$root/src/meson.build"
grep -q "libpkgbuild < 4.0.0" "$root/src/meson.build"
grep -q "libpkgbuild-image >= 1.0.0" "$root/src/meson.build"
grep -q "libpkgbuild-image < 2.0.0" "$root/src/meson.build"
grep -q "libpkgfetch < 2.0.0" "$root/src/meson.build"
grep -q "libpkgexec < 2.0.0" "$root/src/meson.build"
grep -q "libpkgimage < 1.0.0" "$root/src/meson.build"
