#!/bin/sh
set -eu
root=$1
src="$root/src/executor.cpp"
grep -F '#include <libpkgsource-exec/libpkgsource-exec.h>' "$src" >/dev/null
grep -F 'source_object_resource_identity(session)' "$src" >/dev/null
grep -F 'realize_source_object_resource(' "$src" >/dev/null
grep -F 'session.sources(), paths.source_tree' "$src" >/dev/null
grep -F 'libpkgbuild-exec:source-object-resource:v1' "$src" >/dev/null
grep -F 'session.request().identity().hex() + session.sources().identity().hex()' "$src" >/dev/null
grep -F 'pkgsource_exec::realize_source_object_tree(materialization' "$src" >/dev/null
grep -F 'cannot realize source-object resource:' "$src" >/dev/null
! grep -F 'libpkgbuild-exec:source-tree:v1' "$src" >/dev/null
! grep -F 'stage_source_object(' "$src" >/dev/null

! grep -F 'pkgsource_exec::source_object_tree_identity' "$src" >/dev/null
grep -F 'source_tree.materialization != session.sources().identity()' "$src" >/dev/null
