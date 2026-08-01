# Contributing

Submit focused patches that preserve the authority boundaries in `DESIGN.md`.
Include tests for admission failure, execution translation, effect cleanup, and
result-sealing behavior. Keep commits independently buildable and diagnostics
outside semantic identities.

Codec changes must include canonical round-trip, corruption, truncation, and
foreign-authority tests. Do not duplicate `libpkgexec` execution fields or add
session paths, source locators, backend calls, or artifact effects to decoding.
