# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later

executor_test = executable(
  'executor-test',
  'executor_test.cpp',
  dependencies: libpkgbuild_exec_dep,
)
test('build execution authority', executor_test)
test(
  'durable build evidence codec',
  executor_test,
  args: ['--codec'],
)

headers_test = executable(
  'public-headers-test',
  'public_headers.cpp',
  dependencies: libpkgbuild_exec_dep,
)
test('public headers', headers_test)

test(
  'authority contract',
  find_program('check_authority_contract.sh'),
  args: [meson.project_source_root()],
)
test(
  'release metadata',
  find_program('check_release_metadata.sh'),
  args: [meson.project_source_root()],
)

test(
  'codec contract',
  find_program('check_codec_contract.sh'),
  args: [meson.project_source_root()],
)
