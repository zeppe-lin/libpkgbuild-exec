#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
root=${1:?}
fail() { echo "abi-contract: $*" >&2; exit 1; }
manifest=$root/abi/libpkgbuild-exec.exports
[ -s "$manifest" ] || fail 'reviewed ELF ABI manifest is absent'
[ "$(sed -n '/^_Z[A-Za-z0-9_]*$/p' "$manifest" | wc -l)" -eq 28 ] ||
  fail 'reviewed ELF ABI manifest must contain exactly 28 symbols'
[ "$(LC_ALL=C sort -u "$manifest" | wc -l)" -eq 28 ] ||
  fail 'reviewed ELF ABI manifest contains duplicate symbols'
! grep -F '_ZN13pkgbuild_exec6detail' "$manifest" >/dev/null ||
  fail 'private detail namespace entered public ABI manifest'
! grep -E '^_ZNSt|^_ZN9__gnu_cxx' "$manifest" >/dev/null ||
  fail 'standard-library implementation symbol entered public ABI manifest'
! grep -E '_ZN13pkgbuild_exec22admitted_build_sessionC[12]' "$manifest" >/dev/null ||
  fail 'private admitted-session constructor entered public ABI manifest'
! grep -E '_ZN13pkgbuild_exec22build_execution_resultC[12]' "$manifest" >/dev/null ||
  fail 'private build-result constructor entered public ABI manifest'
grep -F '_ZN13pkgbuild_exec22admitted_build_session5admit' "$manifest" >/dev/null ||
  fail 'session admission is absent from reviewed ABI'
grep -F '_ZN13pkgbuild_exec22seal_execution_request' "$manifest" >/dev/null ||
  fail 'execution-request projection is absent from reviewed ABI'
grep -F '_ZN13pkgbuild_exec7execute' "$manifest" >/dev/null ||
  fail 'execute() is absent from reviewed ABI'
grep -F '_ZN13pkgbuild_exec29decode_build_execution_result' "$manifest" >/dev/null ||
  fail 'durable decoder is absent from reviewed ABI'
grep -F '_ZTIN13pkgbuild_exec5errorE' "$manifest" >/dev/null ||
  fail 'public error RTTI is absent from reviewed ABI'
grep -F "soversion: '2'" "$root/src/meson.build" >/dev/null ||
  fail 'SONAME generation changed unexpectedly'
grep -F -- '--version-script=' "$root/src/meson.build" >/dev/null ||
  fail 'reviewed ELF export manifest is not linked'
grep -F "../abi/libpkgbuild-exec.exports" "$root/src/meson.build" >/dev/null ||
  fail 'Meson does not consume reviewed ABI manifest'
grep -F '28-symbol' "$root/MAINTAINING.md" >/dev/null ||
  fail 'reviewed ABI inventory is undocumented'
