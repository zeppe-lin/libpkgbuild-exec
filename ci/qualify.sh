#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

mode=${1:-shared}
build_dir=${2:-build-$mode}

case $mode in
  shared|static) ;;
  *) echo "usage: $0 [shared|static] [build-directory]" >&2; exit 2 ;;
esac

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
case $build_dir in /*) build=$build_dir ;; *) build=$(pwd)/$build_dir ;; esac
install=$build/install
rm -rf "$build"

"$root/tests/contracts/check_authority_contract.sh" "$root"
"$root/tests/contracts/check_codec_contract.sh" "$root"
"$root/tests/contracts/check_release_metadata.sh" "$root"
"$root/tests/contracts/check_test_layout.sh" "$root"
"$root/tests/contracts/check_abi_contract.sh" "$root"
"$root/tests/contracts/check_ci_contract.sh" "$root"

meson setup "$build" "$root" \
  --wrap-mode=nofallback \
  --fatal-meson-warnings \
  --prefix="$install" \
  --libdir=lib \
  --buildtype="${MESON_BUILDTYPE:-debug}" \
  -Ddefault_library="$mode" \
  -Dlink_mode="$mode" \
  -Dman_pages=enabled \
  -Dtests=enabled \
  -Dwerror=true \
  ${MESON_SANITIZE:+-Db_sanitize="$MESON_SANITIZE"} \
  ${MESON_SANITIZE:+-Db_lundef=false}
meson compile -C "$build"
meson test -C "$build" --no-rebuild --print-errorlogs
meson install -C "$build"

consumer="$root/tests/installed/consumer.cpp"
consumer_bin="$build/installed-consumer"
consumer_pkgconfig="$install/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
consumer_flags=$(PKG_CONFIG_PATH="$consumer_pkgconfig" pkg-config --cflags libpkgbuild-exec)
if [ "$mode" = static ]; then
  consumer_libs=$(PKG_CONFIG_PATH="$consumer_pkgconfig" pkg-config --static --libs libpkgbuild-exec)
else
  consumer_libs=$(PKG_CONFIG_PATH="$consumer_pkgconfig" pkg-config --libs libpkgbuild-exec)
fi
# shellcheck disable=SC2086
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Wpedantic -Werror \
  $consumer_flags "$consumer" -o "$consumer_bin" $consumer_libs
LD_LIBRARY_PATH="$install/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  "$consumer_bin"
