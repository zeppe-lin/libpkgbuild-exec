# Migration

There is no compatibility migration for 0.1.0. This repository establishes a
new native adapter. It does not accept Pkgfile, pkgmk, fakeroot, the legacy
package database, filename-derived artifact identity, or implicit source
extraction contracts.

## 0.1.0 to 1.0.0

Rebuild every consumer. `admitted_build_session` and
`build_execution_result` retain generation-2 fetch/build authority values by
value. No execution evidence or artifact format is converted.

## Durable evidence records

Version 1 records introduced after 1.0.0 are new controller evidence, not a
conversion of legacy build logs or package artifacts. There is no importer for
older ad-hoc records. Callers must retain the exact build request, execution
request, and backend profile authorities needed for decoding.
