# libpkgbuild-exec 2.2.0

`libpkgbuild-exec` is the native build-execution adapter for Zeppe-Lin.
It realizes one sealed `libpkgbuild` request through a caller-supplied,
backend-neutral `libpkgexec` execution authority.

The adapter combines:

- a sealed build request from `libpkgbuild`;
- verified raw source objects from `libpkgfetch`;
- explicit call-scoped host resources for every resolver-issued build and check input;
- an explicit root view, interpreter identity, credentials, and session paths;
- an injected `pkgexec::execution_backend`.

Before any host effect, `seal_execution_request()` can reproduce the exact
backend-neutral execution request from an admitted session and
`project_prepared_paths()` can reproduce the adapter-owned source, workspace,
and temporary paths. `prepare()` uses those same projections, then stages and
binds host resources. This separation lets restart recovery and downstream
check composition recover exact request and resource coordinates without
deleting or recreating a workspace.

The effectful path stages each verified source again under its exact declared
`local_name`, requests denied-network execution, retains the complete execution result,
inspects the package output root, writes an uncompressed `package_tar`,
publishes without replacing an existing artifact, asks `libpkgimage` to reopen and inspect the published bytes, and delegates
build/image equality to `libpkgbuild-image` before build success is retained.

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
