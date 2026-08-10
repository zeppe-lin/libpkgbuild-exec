#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
root=$1
meson=$root/tests/meson.build

for directory in contracts fixtures header installed integration protocol support unit; do
  test -d "$root/tests/$directory" || {
    echo "missing test role directory: $directory" >&2
    exit 1
  }
done

grep -F "test('header-' + header.underscorify(), header_test, suite: 'header')" "$meson" >/dev/null
if grep -F "test('header:'" "$meson" >/dev/null; then
  echo 'deprecated colon remains in generated header test name' >&2
  exit 1
fi

for suite in unit integration protocol header contract; do
  grep -F "suite: '$suite'" "$meson" >/dev/null || {
    echo "missing Meson test suite: $suite" >&2
    exit 1
  }
done

if test -e "$root/tests/executor_test.cpp" || test -e "$root/tests/public_headers.cpp"; then
  echo 'flat omnibus test source remains after role split' >&2
  exit 1
fi

grep -F "'integration/session_admission_test.cpp'" "$meson" >/dev/null
grep -F "'integration/request_projection_test.cpp'" "$meson" >/dev/null
grep -F "'integration/preparation_test.cpp'" "$meson" >/dev/null
grep -F "'integration/backend_contract_test.cpp'" "$meson" >/dev/null
grep -F "'integration/execution_failure_test.cpp'" "$meson" >/dev/null
grep -F "'integration/execution_success_test.cpp'" "$meson" >/dev/null
grep -F "'integration/result_sealing_test.cpp'" "$meson" >/dev/null
grep -F "'protocol/result_codec_roundtrip_test.cpp'" "$meson" >/dev/null
grep -F "'protocol/result_codec_refusal_test.cpp'" "$meson" >/dev/null
[ -f "$root/tests/installed/consumer.cpp" ] || {
  echo 'missing installed consumer' >&2
  exit 1
}
