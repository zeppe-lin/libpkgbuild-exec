# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later

executor_test = executable(
  'executor-test',
  'executor_test.cpp',
  dependencies: libpkgbuild_exec_dep,
)
test('build execution authority', executor_test)

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
