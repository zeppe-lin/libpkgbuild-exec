# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later

scdoc = find_program('scdoc', required: get_option('man_pages'))
if scdoc.found()
  custom_target(
    'libpkgbuild-exec.7',
    input: 'libpkgbuild-exec.7.scdoc',
    output: 'libpkgbuild-exec.7',
    command: [scdoc],
    feed: true,
    capture: true,
    install: true,
    install_dir: join_paths(get_option('mandir'), 'man7'),
  )

  custom_target(
    'libpkgbuild_exec_result_codec.3',
    input: 'libpkgbuild_exec_result_codec.3.scdoc',
    output: 'libpkgbuild_exec_result_codec.3',
    command: [scdoc],
    feed: true,
    capture: true,
    install: true,
    install_dir: join_paths(get_option('mandir'), 'man3'),
  )
endif
