# Migration

There is no compatibility migration for 0.1.0. This repository establishes a
new native adapter. It does not accept Pkgfile, pkgmk, fakeroot, the legacy
package database, filename-derived artifact identity, or implicit source
extraction contracts.

## 0.1.0 to 1.0.0

Rebuild every consumer. `admitted_build_session` and
`build_execution_result` retain generation-2 fetch/build authority values by
value. No execution evidence or artifact format is converted.

## 2.3.0 to 3.0.0

Rebuild every consumer. `admitted_build_session` retains
`pkgfetch::source_materialization` by value. `libpkgfetch 3.0.0` rebuilds that
carrier for source ABI 4, so `libpkgbuild-exec.so.2` and
`libpkgbuild-exec.so.3` are different binary generations even though the
adapter-owned fields and exported C++ symbol names are unchanged.

Use `libpkgbuild-exec.so.3` with `libpkgfetch >= 3.0.0, < 4.0.0`. Do not mix
fetch ABI 2/source ABI 3 authority with the new execution generation. Durable
build-execution evidence and adapter identity domains are unchanged; private
incompatible retained bytes fail closed rather than being reinterpreted.

## Durable evidence records

Version 1 records introduced after 1.0.0 are new controller evidence, not a
conversion of legacy build logs or package artifacts. There is no importer for
older ad-hoc records. Callers must retain the exact build request, execution
request, and backend profile authorities needed for decoding.
