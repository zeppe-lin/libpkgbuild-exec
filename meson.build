# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
project(
  'libpkgbuild-exec',
  'cpp',
  version: '2.3.0',
  license: 'GPL-3.0-or-later',
  meson_version: '>=1.2.0',
  default_options: [
    'cpp_std=c++17',
    'warning_level=3',
    'default_library=shared',
    'b_ndebug=if-release',
  ],
)

library_kind = get_option('default_library')
if library_kind == 'both'
  error('libpkgbuild-exec requires separate shared and static builds')
endif

want_static = get_option('link_mode') == 'static'
if (library_kind == 'static') != want_static
  error('default_library and link_mode must both select shared or static')
endif

libcrypto_dep = dependency(
  'libcrypto',
  required: true,
  static: want_static,
)
libarchive_dep = dependency(
  'libarchive',
  required: true,
  static: want_static,
)
libpkgsource_dep = dependency(
  'libpkgsource',
  version: ['>=4.0.0', '<5.0.0'],
  required: true,
  static: want_static,
  fallback: ['libpkgsource', 'libpkgsource_dep'],
)
libpkgbuild_dep = dependency(
  'libpkgbuild',
  version: ['>=3.0.0', '<4.0.0'],
  required: true,
  static: want_static,
  fallback: ['libpkgbuild', 'libpkgbuild_dep'],
)
libpkgbuild_image_dep = dependency(
  'libpkgbuild-image',
  version: ['>=1.0.0', '<2.0.0'],
  required: true,
  static: want_static,
  fallback: ['libpkgbuild-image', 'libpkgbuild_image_dep'],
)
libpkgfetch_dep = dependency(
  'libpkgfetch',
  version: ['>=2.0.0', '<3.0.0'],
  required: true,
  static: want_static,
  fallback: ['libpkgfetch', 'libpkgfetch_dep'],
)
libpkgexec_dep = dependency(
  'libpkgexec',
  version: ['>=2.0.0', '<3.0.0'],
  required: true,
  static: want_static,
  fallback: ['libpkgexec', 'libpkgexec_dep'],
)
libpkgimage_dep = dependency(
  'libpkgimage',
  version: ['>=0.4.0', '<1.0.0'],
  required: true,
  static: want_static,
  fallback: ['libpkgimage', 'libpkgimage_dep'],
)

pkgconf = import('pkgconfig')

subdir('src')
if not get_option('tests').disabled()
  subdir('tests')
endif
subdir('man')
