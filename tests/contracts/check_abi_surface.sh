#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
library=${1:?}
manifest=${2:?}
fail() { echo "abi-surface-test: $*" >&2; exit 1; }
command -v nm >/dev/null 2>&1 || fail 'nm is required'
actual=$(mktemp)
expected=$(mktemp)
trap 'rm -f "$actual" "$expected"' EXIT HUP INT TERM
LC_ALL=C sort -u "$manifest" > "$expected"
nm -D --defined-only "$library" |
  awk '{print $3}' |
  sed 's/@@[^@]*$//' |
  grep '^_Z' |
  LC_ALL=C sort -u > "$actual"
if ! diff -u "$expected" "$actual"; then
  fail 'shared-library exports differ from reviewed manifest'
fi
