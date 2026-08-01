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

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
"$root/tests/check_authority_contract.sh" "$root"
"$root/tests/check_codec_contract.sh" "$root"
"$root/tests/check_release_metadata.sh" "$root"

meson setup "$build" --wipe \
  -Ddefault_library="$mode" \
  -Dlink_mode="$mode" \
  -Dman_pages=enabled \
  -Dtests=enabled
meson compile -C "$build"
meson test -C "$build" --print-errorlogs
