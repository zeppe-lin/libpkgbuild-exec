# libpkgbuild-exec 1.0.0

`libpkgbuild-exec` is the native build-execution adapter for Zeppe-Lin.
It realizes one sealed `libpkgbuild` request through a caller-supplied,
backend-neutral `libpkgexec` execution authority.

The adapter combines:

- a sealed build request from `libpkgbuild`;
- verified raw source objects from `libpkgfetch`;
- exact host trees for every sealed build and check input;
- an explicit root view, interpreter identity, credentials, and session paths;
- an injected `pkgexec::execution_backend`.

It stages each verified source again under its exact declared `local_name`,
requests denied-network execution, retains the complete execution result,
inspects the package output root, writes an uncompressed `package_tar_v1`,
publishes without replacing an existing artifact, and asks `libpkgimage` to
reopen and inspect the published bytes before build success is sealed.

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
retains the adapter-owned build, payload, artifact, inspection, sealing, and
diagnostic evidence. Decode requires the exact build request, execution
request, and backend profile bodies; it never reconstructs those authorities
from identities.
