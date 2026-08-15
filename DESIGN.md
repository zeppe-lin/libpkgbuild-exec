# Design

## Authority

`libpkgbuild-exec` owns destination-side realization of one sealed native
build request. It translates existing authorities; it does not extend their
semantics.

```
libpkgsource declaration
        |
libpkgfetch verified raw objects
        |
libpkgbuild sealed build request
        |
libpkgbuild-exec admitted session
        |
libpkgexec execution backend
        |
package-output inspection
        |
package_tar encoding and non-replacing publication
        |
libpkgimage inspection of the published bytes
        |
libpkgbuild result
```

Locators, cache paths, package-input host paths, session directories, and the
artifact destination are effect coordinates. They do not enter native build
identity. The caller supplies one exact root view, interpreter identity,
numeric credential policy, and one explicit call-scoped resource identity and
host path for every build-scoped logical package input retained by the sealed
request. Check-scoped inputs remain logical authority only and are realized by
the independent check execution boundary.

The adapter depends only on the backend-neutral `pkgexec::execution_backend`.
Backend construction and `libpkgexec-linux` selection belong to orchestration.

## Admission

A session is admitted only when:

- the fetch materialization belongs to the request's exact source snapshot;
- every sealed source occurs exactly once with the required SHA-256;
- every logical build-scoped input has exactly one matching
  `package_input_resource` bound by its `pkgbuild::build_input_identity`;
- check-scoped inputs remain part of the sealed `pkgbuild::build_request` but
  have no concrete construction resource and cannot be supplied to this
  adapter;
- no resource is duplicated, aliased to another logical input, or supplied for
  an input absent from the request;
- root, session, package-output, artifact, and package-input coordinates are
  absolute and do not overlap in ways that would mix authority with effects;
- the output layout is `package_root`;
- artifact compression is explicitly `none`.

A package-input resource deliberately keeps three facts separate: the logical
input identity owned by `libpkgbuild`, the semantic resource identity consumed
by `libpkgexec`, and the host path at which that resource is available for this
call. The adapter matches those facts and asks the execution backend to expose
the resource read-only. Recipe addressing is a fourth, execution-local concern:
build inputs are projected beneath `PKG_BUILD_INPUT_ROOT=/build/inputs` at
`/build/inputs/<canonical-package-name>`. The historical extra `build/` child is
not semantic authority and is not retained after check inputs leave construction.
Session admission rejects distinct logical inputs that share one semantic
resource identity. Pure request projection additionally
rejects a package-input resource that aliases an adapter-owned source, workspace,
package-output, or temporary resource, before effectful preparation mutates any
host path. The adapter does not issue or verify a content-addressed package-tree
identity, inspect package provenance, or invent a second dependency validator.

## Pure execution-request projection

`seal_execution_request()` derives the canonical `pkgexec::execution_request`
from one admitted session without opening, creating, deleting, staging, chmodding,
or otherwise touching host resources. The projection binds the same logical
source, package-input, workspace, package-output, temporary, root-view,
interpreter, environment, credential, limit, and cancellation authorities later
used by execution.

`prepare()` is the effect boundary. It asks `libpkgsource-exec` to recreate the
phase-neutral raw source-object resource, realizes writable adapter-owned
directories, calls the pure projection, and admits host materializations for its
exact resource slots. A restart path may therefore reproduce request authority
without invoking destructive workspace preparation.

## Source realization

Raw source-object realization is delegated to `libpkgsource-exec`. That provider
reopens and rehashes the exact `pkgfetch::source_materialization`, exposes each
object under its declared `local_name`, and seals the tree read-only. It does not
mint `pkgexec::resource_identity`. This adapter owns the concrete build execution
resource instance and derives its identity from the exact build request plus
materialization authority. Another execution phase may therefore recreate the
same raw-source semantics without borrowing either this adapter's private staging
layout or its execution-resource identity.

A source whose sealed `unpack_kind()` is `archive` is additionally decoded by
this adapter's private source-archive backend into the writable build workspace
before execution. That second transformation remains build-specific: it creates
mutable construction work from immutable raw source authority. Archive format
recognition is never inferred from extension, MIME type, locator, or filename.
The libarchive provider decodes entries; this adapter owns build-workspace path
containment, collision refusal, object-type admission, ownership disregard,
admitted-umask mode realization, and filesystem effects.

## Execution translation

The adapter translates the request's build program into a build-purpose
execution request with:

- a closed `C.UTF-8`, UTC environment;
- denied networking;
- deterministic EOF on standard input through the null device and complete
  stdout/stderr capture;
- fixed numeric credentials and `no_new_privileges`;
- exact read-only source and package-input resources;
- exact writable workspace, package-output, and private temporary resources;
- no invented resource limits or cancellation policy.

The supplied root view must satisfy the selected backend's own root and mount
point contract. This adapter does not mutate or manufacture that root view.

Before effectful preparation, execution snapshots the backend capability profile.
A backend exception is translated into `backend_contract_violation`, and returned
execution evidence must name both the exact projected request and that advertised
capability profile. Backend-owned evidence therefore cannot silently switch
request or backend authority underneath one admitted build.

## Payload inspection

A successful process is not a successful build. The package output root is
walked through directory descriptors without following symlinks. The adapter
seals canonical paths, object type, permission bits, numeric ownership,
whole-second modification time, regular-file size and SHA-256, hard-link
topology, symbolic-link targets, FIFO identity, and device numbers.

Regular-file descriptors are not retained in proportion to payload size.
Inspection seals each file's inode/metadata, size, and digest, then releases its
descriptor. Archive encoding retains the exact package-output root, reopens each
regular path component-by-component with no symlink following, requires the
original inode/metadata, and rehashes the bytes while writing them. The complete
payload is inspected again after encoding and must reproduce the same root stamp
and manifest. Descriptor use is therefore bounded by path depth while pathname,
metadata, content, hard-link, and directory replacement remain fail-closed.
Sockets and all unsupported object types fail result sealing.

`package_tar` uses restricted pax and therefore represents modification
time at whole-second precision. The payload manifest is normalized to that
representable precision before artifact encoding.

## Artifact sealing, evidence, and publication

The artifact contract writes only uncompressed `package_tar`. Successful
execution first encodes the archive beneath the admitted private session root
under an unpublished temporary name, makes it read-only, synchronizes it,
hashes it, and gives it a deterministic private sealed name. `libpkgimage`
opens those exact private bytes with the expected complete-archive digest. The
adapter compares the normalized image to the intended payload entry by entry
before successful build evidence exists.

Caller-visible publication is a separate projection. A durable controller can
therefore persist the terminal `build_execution_result` before exposing the
artifact path. `publish_sealed_artifact()` reopens the private sealed bytes,
proves they still match the retained byte count and SHA-256, copies them into
an unpublished temporary object in the destination directory, synchronizes it,
and publishes without replacement. If the final path already exists, present
observation may satisfy publication only when it proves the exact bytes named
by terminal evidence. A pathname by itself is never treated as historical
build evidence.

After successful projection the private artifact name is removed. Recovery
after publication but before controller completion can validate the existing
final path directly against already-durable terminal evidence; recovery before
terminal evidence must replay the admitted build rather than infer success
from filesystem residue.

Diagnostics are retained operational evidence. Stable result-sealing failure
identity contains only the typed failure class, not diagnostic prose or host
paths.

## Durable build-execution evidence

The durable record belongs to this adapter because it binds two independent
owner results: exact `libpkgexec` process evidence and the corresponding
`libpkgbuild` outcome, including post-execution artifact sealing evidence.
The record embeds the canonical `libpkgexec 2.x` execution-result encoding and
adds only adapter-owned fields.

The record does not serialize a build request, execution request, backend
profile, admitted session, source materialization, package-input resource, host
path, credential policy, or execution resource. Decode requires the exact
`pkgbuild::build_request`, `pkgexec::execution_request`, and
`pkgexec::backend_capability_profile` bodies from their owning authorities.
Identity strings alone are not rehydration authority. Public artifact
publication is likewise not decoding authority: decoded terminal evidence can
authorize a later exact-byte projection without rerunning construction.

Successful records retain the complete payload manifest, sealed artifact, and
archive-inspection receipt used to restore the exact
`pkgbuild::image_adapter::build_image_authority`. Decode reconstructs payload
entries and artifacts through public factories, requires the receipt to name
the exact artifact bytes and payload entry count, rebuilds the
`pkgbuild::build_result`, restores the admitted build/image pair, and checks
both subordinate result identities. Failed execution and failed result
sealing remain distinct shapes and reproduce their existing evidence domains.

Diagnostic prose remains outside semantic build identity but is covered by the
whole-record checksum. Every accepted record is re-encoded and must reproduce
its original bytes. The codec performs no source staging, execution, payload
inspection, artifact publication, archive reopening, cleanup, or mutation.

The pure `project_prepared_paths()` projection owns the retained source-tree,
build-workspace, and temporary coordinates beneath an admitted session root.
Downstream composition must consume that projection rather than duplicate the
adapter's path vocabulary.
