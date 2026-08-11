# Testing

The suite is separated by evidence role. A failure should identify whether the
problem belongs to adapter value semantics, caller/callee composition, durable
wire evidence, public-header closure, or a static authority/release contract.

Meson suites:

- `unit`
- `integration`
- `protocol`
- `header`
- `contract`

The shared build also runs an exact ELF ABI-surface contract.
It also verifies that the built DSO names `libpkgfetch.so.2` directly, so release
metadata cannot claim generation 2 while the binary still closes over an obsolete
fetch SONAME.
The same shared-product gate requires `libpkgexec.so.2` and refuses exec 0/1.
An x86-64 ABI-layout contract freezes every retained execution carrier plus the
public `libpkgbuild-exec` values that contain them. Generated pkg-config metadata
is checked as an exact, duplicate-free dependency set.

Run the complete native suite:

```sh
meson test -C build --print-errorlogs
```

Run one evidence role independently:

```sh
meson test -C build --suite unit --print-errorlogs
meson test -C build --suite integration --print-errorlogs
meson test -C build --suite protocol --print-errorlogs
meson test -C build --suite header --print-errorlogs
meson test -C build --suite contract --print-errorlogs
```

Qualify each dependency closure independently:

```sh
./ci/qualify.sh shared
./ci/qualify.sh static
```

## Unit qualification

`error-value` pins the stable adapter error and result-sealing vocabulary without
constructing filesystem or execution authority.

## Integration qualification

Integration fixtures build genuine authorities through `libpkgsource`,
`libpkgcatalog`, `libpkgstate`, `libpkgresolve`, `libpkgbuild`, `libpkgfetch`,
and `libpkgexec`. The execution backend is injected because concrete backend
selection belongs to orchestration; the fixture backend returns real sealed
`libpkgexec::execution_result` values and otherwise owns no build policy.

`session-admission` proves:

- source materialization belongs to the exact sealed build source;
- unordered concrete package inputs canonicalize to build-request order;
- missing, duplicate, and aliased logical package-input resources are refused;
- relative, root, overlapping, and artifact-overlap effect coordinates are
  refused before mutation;
- supplementary groups are canonicalized and duplicate/primary aliases are
  refused;
- native credential bounds are enforced; and
- only uncompressed `package_tar` realization is admitted.

`request-projection` proves:

- pure build-purpose request sealing performs no host mutation;
- exact program, interpreter, root, credentials, environment, input lists,
  mount points, access modes, and working directory are retained;
- the required guarantee set is exactly the policy derived from the sealed
  request;
- no limits or cancellation policy are invented;
- session, artifact, and package-input host paths do not enter semantic request
  identity;
- package-input semantic resource identities do enter request identity; and
- a caller-supplied package-input resource cannot alias an adapter-owned source,
  workspace, package-output, or temporary resource. That refusal occurs before
  effectful preparation begins.

`preparation` proves exact verified source bytes are reopened, rehashed, staged
under their declared local names, sealed read-only, and bound together with the
exact writable workspace/output/temporary resources. Changed bytes and a final
source-object symlink are refused.

`backend-contract` proves capability observation and execution are both
exception-contained. Returned execution evidence must name the exact projected
request and the exact capability profile advertised before preparation. Both
standard and non-standard backend throws are translated into
`backend_contract_violation`; capability failure occurs before build paths are
mutated.

`execution-failure` independently qualifies not-started backend refusal and a
started nonzero program failure. Both retain exact `libpkgexec` evidence and
become failed `libpkgbuild` results without inventing post-execution sealing or
artifact evidence.

`execution-success` proves complete process evidence, payload inspection,
hard-link and symlink topology, FIFO retention, deterministic uncompressed
package-tar bytes, read-only non-replacing publication, independent
`libpkgimage` inspection, and effect-coordinate independence of final build
identity.

`result-sealing` keeps successful process execution distinct from a successful
build. It proves refusal of unsupported package-output objects, empty output,
and an already-existing artifact destination, including non-replacement of the
existing bytes.

## Durable evidence protocol

`result-codec-roundtrip` produces real adapter results for successful artifact
sealing, pre-start execution failure, started program failure, and
post-execution payload-sealing failure. Every shape must round-trip exactly and
re-encode byte-for-byte canonically.

`result-codec-refusal` covers checksum corruption, truncation, trailing bytes,
and substitution of the build request, execution request, or backend profile.
Decode remains evidence-only: it does not admit a session, stage sources,
execute a backend, inspect package output, publish an artifact, or clean up
filesystem state.

## Public headers and contracts

Every installed public header is compiled as a standalone translation unit.
Static contracts pin the build/execution authority boundary, durable codec
shape, release metadata, test-role layout, absence of `libpkgexec-linux` from
this backend-neutral adapter, source-byte revalidation, non-replacing artifact
publication, and request-sealing-before-effects ordering.


## Installed and ABI qualification

`ci/qualify.sh` installs the selected shared or static product into an isolated
prefix and then compiles `tests/installed/consumer.cpp` exclusively through the
installed pkg-config metadata. The consumer constructs genuine source/catalog/
state/resolve/build/fetch authorities, admits a build session, projects its
execution request and prepared paths, executes a refusing backend, and
round-trips the resulting durable evidence. It is deliberately not a function-
address linker probe.

For static builds the consumer uses `pkg-config --static --libs
libpkgbuild-exec`, forcing the private image/archive/crypto closure. For shared
builds the runtime uses only the installed product and qualified dependency
prefixes.

The generation-2 ELF ABI is the exact 28-symbol manifest in
`abi/libpkgbuild-exec.exports`. `abi-surface` compares the built shared object to
that manifest; compiler-specific template/STL debris and private implementation
symbols are release failures.
