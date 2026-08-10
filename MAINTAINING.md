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

Durable evidence codecs must retain only adapter-owned values. They may embed
canonical records from subordinate owners, but must require semantic request
and backend bodies from the caller rather than decode or infer them from
identity strings. Decoder code must remain free of host paths and effects.


## ABI release gate

`libpkgbuild-exec` 2.x is the corrected logical-input ABI generation. Before
2.2.0 is tagged, the shared ELF surface is frozen to the reviewed 28-symbol
`abi/libpkgbuild-exec.exports` manifest. GCC and Clang shared builds must match
that manifest exactly. Private constructors, `detail` implementation helpers,
and standard-library implementation symbols are not compatibility surface.

A dependency SONAME change is not by itself a reason to advance this library's
SONAME. Advance the provider ABI only when a public `libpkgbuild-exec` signature,
layout, virtual protocol, or other binary contract changes.

`admitted_build_session` retains `pkgfetch::source_materialization` by value. A
future `libpkgfetch` ABI generation therefore requires an explicit layout and
public-ABI review before widening the accepted dependency range. The 1.x to 2.x
fetch correction does not advance `libpkgbuild-exec` ABI generation: the r57
2.2.0 ABI was already compiled against the source-3-shaped fetch headers, and
`libpkgfetch 2.0.0` changes their SONAME/owner metadata without changing those
public headers. Shared qualification must prove a `libpkgfetch.so.2` NEEDED edge.

Installed qualification must compile and run `tests/installed/consumer.cpp`
through the installed `libpkgbuild-exec.pc`. Static qualification uses
`pkg-config --static` so private libarchive/libcrypto/libpkgimage closure is
proved rather than inferred.
