# Contributing

Submit focused patches that preserve the authority boundaries in `DESIGN.md`.
Include tests for admission failure, execution translation, effect cleanup, and
result-sealing behavior. Keep commits independently buildable and diagnostics
outside semantic identities.

Codec changes must include canonical round-trip, corruption, truncation, and
foreign-authority tests. Do not duplicate `libpkgexec` execution fields or add
session paths, source locators, backend calls, or artifact effects to decoding.

Public API changes must review `abi/libpkgbuild-exec.exports` and the SONAME
explicitly. Do not satisfy white-box tests by exporting private implementation
symbols. Run both shared and static installed-consumer qualification before
proposing a release commit.
