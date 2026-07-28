#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
root=$1

grep -q "version: '0.1.0'" "$root/meson.build"
grep -q "soversion: '0'" "$root/src/meson.build"
grep -q '^Version: 0.1.0$' "$root/HISTORY.md"
grep -q '^Date: 2026-07-28$' "$root/HISTORY.md"
grep -q '^# libpkgbuild-exec 0.1.0$' "$root/README.md"
grep -q "libpkgbuild >= 1.0.0" "$root/src/meson.build"
grep -q "libpkgfetch >= 0.1.0" "$root/src/meson.build"
grep -q "libpkgexec >= 1.0.0" "$root/src/meson.build"
grep -q "libpkgimage >= 0.3.0" "$root/src/meson.build"
