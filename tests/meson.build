# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later

unit_tests = {
  'error-value': 'unit/error_value_test.cpp',
}

integration_tests = {
  'session-admission': 'integration/session_admission_test.cpp',
  'request-projection': 'integration/request_projection_test.cpp',
  'preparation': 'integration/preparation_test.cpp',
  'backend-contract': 'integration/backend_contract_test.cpp',
  'execution-failure': 'integration/execution_failure_test.cpp',
  'execution-success': 'integration/execution_success_test.cpp',
  'result-sealing': 'integration/result_sealing_test.cpp',
}

protocol_tests = {
  'result-codec-roundtrip': 'protocol/result_codec_roundtrip_test.cpp',
  'result-codec-refusal': 'protocol/result_codec_refusal_test.cpp',
}

foreach name, source : unit_tests
  executable_test = executable(
    name + '-test',
    source,
    dependencies: libpkgbuild_exec_dep,
  )
  test(name, executable_test, suite: 'unit')
endforeach

foreach name, source : integration_tests
  executable_test = executable(
    name + '-test',
    source,
    dependencies: libpkgbuild_exec_dep,
  )
  test(name, executable_test, suite: 'integration')
endforeach

foreach name, source : protocol_tests
  executable_test = executable(
    name + '-test',
    source,
    dependencies: libpkgbuild_exec_dep,
  )
  test(name, executable_test, suite: 'protocol')
endforeach

public_headers = [
  'error.h',
  'model.h',
  'executor.h',
  'result_codec.h',
  'libpkgbuild-exec.h',
]
foreach header : public_headers
  header_test = executable(
    'header-' + header.underscorify() + '-test',
    'header/public_header_test.cpp',
    cpp_args: [
      '-DPKGBUILD_EXEC_TEST_HEADER=<libpkgbuild-exec/' + header + '>',
    ],
    dependencies: libpkgbuild_exec_dep,
  )
  test('header-' + header.underscorify(), header_test, suite: 'header')
endforeach

contract_tests = {
  'authority-contract': 'contracts/check_authority_contract.sh',
  'codec-contract': 'contracts/check_codec_contract.sh',
  'release-metadata': 'contracts/check_release_metadata.sh',
  'test-layout': 'contracts/check_test_layout.sh',
  'abi-contract': 'contracts/check_abi_contract.sh',
  'ci-contract': 'contracts/check_ci_contract.sh',
  'source-resource-boundary': 'contracts/check_source_resource_boundary.sh',
}
foreach name, program : contract_tests
  test(
    name,
    find_program(program),
    args: [meson.project_source_root()],
    suite: 'contract',
  )
endforeach

pkgconfig_metadata = find_program('contracts/check_pkgconfig_metadata.sh')
test(
  'pkgconfig-metadata',
  pkgconfig_metadata,
  args: [meson.project_build_root(), meson.project_version()],
  suite: 'contract',
)

abi_layout = executable(
  'abi-layout-test',
  'contracts/abi_layout_test.cpp',
  dependencies: libpkgbuild_exec_dep,
)
test('abi-layout', abi_layout, suite: 'contract')

if library_kind == 'shared'
  abi_surface = find_program('contracts/check_abi_surface.sh')
  test(
    'abi-surface',
    abi_surface,
    args: [libpkgbuild_exec, meson.project_source_root() / 'abi/libpkgbuild-exec.exports'],
    suite: 'contract',
  )

  fetch_abi_generation = find_program('contracts/check_fetch_abi_generation.sh')
  test(
    'fetch-abi-generation',
    fetch_abi_generation,
    args: [libpkgbuild_exec],
    suite: 'contract',
  )

  exec_abi_generation = find_program('contracts/check_exec_abi_generation.sh')
  test(
    'exec-abi-generation',
    exec_abi_generation,
    args: [libpkgbuild_exec],
    suite: 'contract',
  )
endif
