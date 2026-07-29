# Migration

There is no compatibility migration for 0.1.0. This repository establishes a
new native adapter. It does not accept Pkgfile, pkgmk, fakeroot, the legacy
package database, filename-derived artifact identity, or implicit source
extraction contracts.

## 0.1.0 to 1.0.0

Rebuild every consumer. `admitted_build_session` and
`build_execution_result` retain generation-2 fetch/build authority values by
value. No execution evidence or artifact format is converted.
