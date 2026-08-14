#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
root=$1
api=$root/include/libpkgbuild-exec/model.h
executor_api=$root/include/libpkgbuild-exec/executor.h
executor=$root/src/executor.cpp
meson=$root/meson.build
design=$root/DESIGN.md

# The adapter consumes sealed authorities and receives its backend.
grep -q 'pkgbuild::build_request' "$api"
grep -q 'pkgfetch::source_materialization' "$api"
grep -q 'pkgbuild::build_input_identity' "$api"
grep -q 'pkgexec::resource_identity' "$api"
! grep -R -q 'input_tree_identity\|materialized_package_input' "$root/include" "$root/src"
grep -F 'one explicit call-scoped resource identity and' "$design" >/dev/null
grep -F 'does not issue or verify a content-addressed' "$design" >/dev/null
if grep -E -n 'input-tree identity|Package-input tree identities|materialized tree identity' "$design" >/dev/null 2>&1; then
  echo 'obsolete package-input tree authority remains in design documentation' >&2
  exit 1
fi
grep -q 'pkgexec::execution_backend' "$executor_api"
grep -q 'seal_execution_request' "$executor_api"
grep -q 'distinct logical package inputs share one execution resource identity' "$root/src/model.cpp"
grep -q 'require_unique_execution_resource_identities' "$executor"
grep -q 'const auto advertised_backend = backend_capabilities(backend);' "$executor"
grep -q 'execution.backend() != advertised_backend' "$executor"
grep -q 'execution backend threw non-standard execution evidence' "$executor"
grep -q 'project_prepared_paths' "$executor_api"
grep -q 'without touching host resources' "$executor_api"
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
grep -q '^prepared_paths project_prepared_paths(' "$executor"
grep -q '^pkgexec::execution_request seal_execution_request(' "$executor"
grep -q 'const auto paths = project_prepared_paths(session);' "$executor"
grep -q 'auto request = seal_execution_request(session);' "$executor"
request_line=$(grep -n 'auto request = seal_execution_request(session);' "$executor" | tail -n 1 | cut -d: -f1)
reset_line=$(grep -n 'reset_directory(session.paths().session_root' "$executor" | cut -d: -f1)
test "$request_line" -lt "$reset_line" || {
  echo 'execution request is sealed only after effectful preparation begins' >&2
  exit 1
}

# Artifact production is non-replacing and independently inspected.
grep -q 'archive_write_set_format_pax_restricted' "$executor"
grep -q '::link(path_.c_str(), destination.c_str())' "$executor"
grep -q 'image_digest(artifact_digest.first)' "$executor"
grep -q 'verify_published_binding' "$executor"
grep -q 'build_image_authority::admit' "$executor"
! grep -q 'void verify_image' "$executor"
grep -q 'open_regular_beneath' "$executor"
grep -q 'package payload changed during archive encoding' "$executor"
grep -q 'large_payload_is_descriptor_bounded' \
  "$root/tests/integration/result_sealing_test.cpp"
! grep -q 'unique_fd descriptor;' "$executor" || {
  echo 'payload sealing retains one descriptor per regular file' >&2
  exit 1
}

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
