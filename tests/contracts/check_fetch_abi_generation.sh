#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
library=${1:?}
fail() { echo "fetch-abi-generation: $*" >&2; exit 1; }
command -v readelf >/dev/null 2>&1 || fail 'readelf is required'
needed=$(readelf -d "$library" | sed -n 's/^.*Shared library: \[\(.*\)\].*$/\1/p')
printf '%s\n' "$needed" | grep -Fx 'libpkgfetch.so.3' >/dev/null ||
  fail 'shared library is not bound to libpkgfetch.so.3'
if printf '%s\n' "$needed" | grep -E '^libpkgfetch\.so\.[012]$' >/dev/null; then
  fail 'obsolete libpkgfetch ABI generation remains in the shared closure'
fi
