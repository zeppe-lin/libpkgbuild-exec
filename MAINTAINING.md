# Maintaining

Keep semantic authority in the owning core library. This repository may admit,
translate, and realize sealed contracts; it must not silently extend them.

Source acquisition belongs to `libpkgfetch`. Build identity and payload/result
semantics belong to `libpkgbuild`. Execution semantics belong to `libpkgexec`.
Archive interpretation belongs to `libpkgimage`. Linux mechanism belongs to
`libpkgexec-linux` and remains injected by orchestration.

Changes that introduce archive extraction, dependency resolution, backend
construction, installation, lifecycle execution, or transaction publication
require a different authority boundary.

Every release must preserve buildable commits, qualify shared and static
closures, and replay cleanly with `git am --whitespace=error-all`.
