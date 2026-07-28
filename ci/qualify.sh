#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

mode=${1:-shared}
build=${2:-build-$mode}

case $mode in
  shared|static) ;;
  *) echo "usage: $0 [shared|static] [build-directory]" >&2; exit 2 ;;
esac

meson setup "$build" --wipe \
  -Ddefault_library="$mode" \
  -Dlink_mode="$mode" \
  -Dman_pages=enabled \
  -Dtests=enabled
meson compile -C "$build"
meson test -C "$build" --print-errorlogs
