#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
library=${1:?}
fail() { echo "exec-abi-generation: $*" >&2; exit 1; }
command -v readelf >/dev/null 2>&1 || fail 'readelf is required'
needed=$(readelf -d "$library" | sed -n 's/^.*Shared library: \[\(.*\)\].*$/\1/p')
printf '%s\n' "$needed" | grep -Fx 'libpkgexec.so.2' >/dev/null ||
  fail 'shared library is not bound to libpkgexec.so.2'
if printf '%s\n' "$needed" | grep -E '^libpkgexec\.so\.[01]$' >/dev/null; then
  fail 'obsolete libpkgexec ABI generation remains in the shared closure'
fi
