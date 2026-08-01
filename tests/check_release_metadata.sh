#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
root=$1

grep -q "version: '1.1.0'" "$root/meson.build"
grep -q "soversion: '1'" "$root/src/meson.build"
grep -q '^Version: 1.1.0$' "$root/HISTORY.md"
grep -q '^Date: 2026-08-01$' "$root/HISTORY.md"
grep -q '^# libpkgbuild-exec 1.1.0$' "$root/README.md"
grep -q "libpkgbuild >= 2.0.0" "$root/src/meson.build"
grep -q "libpkgfetch >= 1.0.0" "$root/src/meson.build"
grep -q "version: '>=1.4.0'" "$root/meson.build"
grep -q "libpkgexec >= 1.4.0" "$root/src/meson.build"
grep -q "libpkgimage >= 0.3.0" "$root/src/meson.build"
