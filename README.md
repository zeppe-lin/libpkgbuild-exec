# libpkgbuild-exec 3.2.0

`libpkgbuild-exec` is the native build-execution adapter for Zeppe-Lin.
It realizes one sealed `libpkgbuild` request through a caller-supplied,
backend-neutral `libpkgexec` execution authority.

The adapter combines:

- a sealed build request from `libpkgbuild`;
- exact raw source-object resources from `libpkgsource-exec`, derived from a
  completed `libpkgfetch` materialization, with build-workspace expansion only
  when `libpkgsource` explicitly declares archive unpacking;
- explicit call-scoped host resources for resolver-issued build inputs only;
  check-scoped inputs remain sealed logical request authority for the independent
  check phase and are never mounted into construction;
- an explicit root view, interpreter identity, credentials, and session paths;
- an injected `pkgexec::execution_backend`.

Before any host effect, `seal_execution_request()` can reproduce the exact
backend-neutral execution request from an admitted session and
`project_prepared_paths()` can reproduce the adapter-owned source, workspace,
and temporary paths. `prepare()` uses those same projections, asks
`libpkgsource-exec` to recreate the exact phase-neutral raw source-object tree,
then realizes and binds the build-owned writable resources. Restart recovery may
reproduce request authority without destructive preparation, while a later
check can recreate equivalent source authority independently from retained
materialization evidence.

The effectful path requests denied-network execution, retains the complete
execution result, inspects the package output root, and writes an uncompressed
`package_tar`. `execute_sealed()` keeps the exact read-only archive beneath the
admitted private session root, asks `libpkgimage` to inspect those sealed bytes,
and delegates build/image equality to `libpkgbuild-image` before build success
is retained. `publish_sealed_artifact()` later projects only those retained
bytes to the caller artifact path without replacement. The one-shot
`execute()` convenience performs both phases for callers that do not need a
durable boundary between terminal evidence and public publication.

The library does not fetch sources, extract archives, discover collections,
resolve dependencies, construct a Linux backend, install packages, execute
package lifecycle programs, or publish transaction state.

## Build

```sh
meson setup build \
  -Ddefault_library=shared \
  -Dlink_mode=shared \
  -Dman_pages=enabled
meson compile -C build
meson test -C build --print-errorlogs
```

Shared and static dependency closures are separate builds. See `TESTING.md`.

## Durable evidence

`libpkgbuild-exec` provides a versioned canonical codec for
`build_execution_result`. It embeds `libpkgexec`'s execution-result record and
retains the adapter-owned build, payload, artifact, build/image admission,
sealing, and diagnostic evidence. Decode requires the exact build request, execution
request, and backend profile bodies; it never reconstructs those authorities
from identities.
