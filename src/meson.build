# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later

public_inc = include_directories('../include')

elf_export_generator = find_program(
  meson.project_source_root() / 'tools/generate-elf-export-script.sh',
)
elf_export_script_name = 'libpkgbuild-exec.exports.map'
elf_export_script_path = meson.current_build_dir() / elf_export_script_name
elf_export_script = configure_file(
  input: '../abi/libpkgbuild-exec.exports',
  output: elf_export_script_name,
  command: [elf_export_generator, '@INPUT@'],
  capture: true,
)

library_link_args = ['-Wl,--version-script=' + elf_export_script_path]
library_link_depends = [elf_export_script]
public_headers = files(
  '../include/libpkgbuild-exec/error.h',
  '../include/libpkgbuild-exec/model.h',
  '../include/libpkgbuild-exec/executor.h',
  '../include/libpkgbuild-exec/result_codec.h',
  '../include/libpkgbuild-exec/libpkgbuild-exec.h',
)
public_deps = [
  libpkgbuild_dep,
  libpkgbuild_image_dep,
  libpkgfetch_dep,
  libpkgexec_dep,
]
private_deps = [
  libpkgimage_dep,
  libarchive_dep,
  libcrypto_dep,
]

libpkgbuild_exec = library(
  'pkgbuild-exec',
  ['error.cpp', 'model.cpp', 'result_identity.cpp', 'result_codec.cpp', 'executor.cpp'],
  cpp_args: ['-D_FILE_OFFSET_BITS=64'],
  include_directories: public_inc,
  dependencies: public_deps + private_deps,
  link_args: library_link_args,
  link_depends: library_link_depends,
  install: true,
  version: meson.project_version(),
  soversion: '2',
)

libpkgbuild_exec_dep = declare_dependency(
  link_with: libpkgbuild_exec,
  include_directories: public_inc,
  dependencies: public_deps + private_deps,
  compile_args: ['-D_FILE_OFFSET_BITS=64'],
)
meson.override_dependency('libpkgbuild-exec', libpkgbuild_exec_dep)

install_headers(public_headers, subdir: 'libpkgbuild-exec')

pkgconf.generate(
  libpkgbuild_exec,
  name: 'libpkgbuild-exec',
  filebase: 'libpkgbuild-exec',
  description: 'Native sealed build execution adapter',
  subdirs: '.',
  requires: public_deps,
  requires_private: private_deps,
)
