# Maintaining

Keep semantic authority in the owning core library. This repository may admit,
translate, and realize sealed contracts; it must not silently extend them.

Source acquisition belongs to `libpkgfetch`. Build identity and payload/result
semantics belong to `libpkgbuild`. Execution semantics belong to `libpkgexec`.
Package-image interpretation belongs to `libpkgimage`. Declared source archive
decoding is an adapter-owned realization mechanism behind the private
source-archive backend. Linux mechanism belongs to
`libpkgexec-linux` and remains injected by orchestration.

Changes that infer source extraction from filenames, extend source semantics
beyond sealed `libpkgsource` authority, perform dependency resolution, construct
execution backends, install packages, execute lifecycle programs, or publish
transactions require a different authority boundary.

Every release must preserve buildable commits, qualify shared and static
closures, and replay cleanly with `git am --whitespace=error-all`.

Durable evidence codecs must retain only adapter-owned values. They may embed
canonical records from subordinate owners, but must require semantic request
and backend bodies from the caller rather than decode or infer them from
identity strings. Decoder code must remain free of host paths and effects.


## ABI release gate

`libpkgbuild-exec` 3.x is the source-4/fetch-3 retaining ABI generation. The
shared ELF surface is the reviewed 31-symbol
`abi/libpkgbuild-exec.exports` manifest. GCC and Clang shared builds must match
that manifest exactly. Private constructors, `detail` implementation helpers,
and standard-library implementation symbols are not compatibility surface.

The 3.1 API adds only free-function projections/execution phases; it changes no
public carrier layout or virtual protocol, so the SONAME remains 3. Durable
controllers should persist successful `build_execution_result` evidence after
`execute_sealed()` and before `publish_sealed_artifact()`. Moving POSIX
publication logic into a controller instead of this adapter is an authority
regression.

A dependency SONAME change is not by itself a reason to advance this library's
SONAME. Advance the provider ABI only when a public `libpkgbuild-exec` signature,
layout, virtual protocol, or other binary contract changes.

`admitted_build_session` retains `pkgfetch::source_materialization` by value.
The fetch 2.x to 3.x transition changes that carrier because fetch 3 is rebuilt
against source ABI 4; this therefore advances the adapter to
`libpkgbuild-exec.so.3`. Future `libpkgfetch` ABI generations require the same
explicit carrier-layout and public-ABI review before widening the accepted
dependency range. Shared qualification must prove a `libpkgfetch.so.3` NEEDED
edge and refuse older fetch generations.

Installed qualification must compile and run `tests/installed/consumer.cpp`
through the installed `libpkgbuild-exec.pc`. Static qualification uses
`pkg-config --static` so private libarchive/libcrypto/libpkgimage closure is
proved rather than inferred.

`libpkgbuild-exec` also retains `pkgexec` resource identities, interpreter identity,
execution request/resources/result, and backend capability profile values by value.
The libpkgexec 1.x to 2.x correction does not advance this library's ABI: direct
x86-64 qualification proves those retained carriers and every containing
`libpkgbuild-exec` public carrier are layout-identical. Future libpkgexec ABI
generations require the same explicit layout review before widening the accepted
dependency interval. Shared qualification must prove a direct
`libpkgexec.so.2` NEEDED edge and refuse obsolete exec generations.
