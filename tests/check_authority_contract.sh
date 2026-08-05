#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
root=$1
api=$root/include/libpkgbuild-exec/model.h
executor=$root/src/executor.cpp
meson=$root/meson.build

# The adapter consumes sealed authorities and receives its backend.
grep -q 'pkgbuild::build_request' "$api"
grep -q 'pkgfetch::source_materialization' "$api"
grep -q 'pkgbuild::build_input_identity' "$api"
grep -q 'pkgexec::resource_identity' "$api"
! grep -R -q 'input_tree_identity\|materialized_package_input' "$root/include" "$root/src"
grep -q 'pkgexec::execution_backend' \
  "$root/include/libpkgbuild-exec/executor.h"
grep -q 'pkgbuild::image_adapter::build_image_authority' "$api"
! grep -R -q 'libpkgexec-linux' "$root/include" "$root/src" "$meson"

# Source admission is byte-based, not pathname-shaped authority.
grep -q 'O_NOFOLLOW' "$executor"
grep -q 'object.byte_count()' "$executor"
grep -q 'object.observed_digest()' "$executor"
grep -q 'object.declaration().content_digest()' "$executor"
grep -q 'fchmod(destination.get(), 0444)' "$executor"

# Builds receive denied networking and explicit resources.
grep -q 'network_policy::denied' "$executor"
grep -q 'resource_role::source_tree' "$executor"
grep -q 'resource_role::build_input_tree' "$executor"
grep -q 'resource_role::check_input_tree' "$executor"
grep -q 'resource_role::package_output_root' "$executor"

# Artifact production is non-replacing and independently inspected.
grep -q 'archive_write_set_format_pax_restricted' "$executor"
grep -q '::link(path_.c_str(), destination.c_str())' "$executor"
grep -q 'image_digest(artifact_digest.first)' "$executor"
grep -q 'verify_published_binding' "$executor"
grep -q 'build_image_authority::admit' "$executor"
! grep -q 'void verify_image' "$executor"

# No shell utility becomes an accidental execution or archive authority.
if grep -R -E 'system\(|popen\(|execl?p?\(|/usr/bin/(tar|cp|install|ip)' \
    "$root/src" >/dev/null; then
  echo 'external utility entered the adapter authority' >&2
  exit 1
fi

# Source transformation is deliberately absent from implementation.
if grep -R -E 'extract|unpack|decompress' "$root/include" "$root/src" \
    >/dev/null; then
  echo 'source transformation entered the build-execution adapter' >&2
  exit 1
fi
