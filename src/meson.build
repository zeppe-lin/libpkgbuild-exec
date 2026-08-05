# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later

public_inc = include_directories('../include')
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
private_deps = [libpkgimage_dep, libarchive_dep, libcrypto_dep]

libpkgbuild_exec = library(
  'pkgbuild-exec',
  ['error.cpp', 'model.cpp', 'result_identity.cpp', 'result_codec.cpp', 'executor.cpp'],
  cpp_args: ['-D_FILE_OFFSET_BITS=64'],
  include_directories: public_inc,
  dependencies: public_deps + private_deps,
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
  requires: [
    'libpkgbuild >= 3.0.0',
    'libpkgbuild < 4.0.0',
    'libpkgbuild-image >= 1.0.0',
    'libpkgbuild-image < 2.0.0',
    'libpkgfetch >= 1.0.0',
    'libpkgfetch < 2.0.0',
    'libpkgexec >= 1.4.0',
    'libpkgexec < 2.0.0',
  ],
  requires_private: [
    'libpkgimage >= 0.4.0',
    'libpkgimage < 1.0.0',
    'libarchive',
    'libcrypto',
  ],
)
