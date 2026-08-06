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
host path for every logical package input retained by the sealed request.

The adapter depends only on the backend-neutral `pkgexec::execution_backend`.
Backend construction and `libpkgexec-linux` selection belong to orchestration.

## Admission

A session is admitted only when:

- the fetch materialization belongs to the request's exact source snapshot;
- every sealed source occurs exactly once with the required SHA-256;
- every logical build or check input has exactly one matching
  `package_input_resource` bound by its `pkgbuild::build_input_identity`;
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
the resource read-only. It does not issue or verify a content-addressed
package-tree identity, inspect package provenance, or invent a second dependency
validator.

## Pure execution-request projection

`seal_execution_request()` derives the canonical `pkgexec::execution_request`
from one admitted session without opening, creating, deleting, staging, chmodding,
or otherwise touching host resources. The projection binds the same logical
source, package-input, workspace, package-output, temporary, root-view,
interpreter, environment, credential, limit, and cancellation authorities later
used by execution.

`prepare()` is the effect boundary. It realizes the staged source and writable
directories, calls the pure projection, and admits host materializations for its
exact resource slots. A restart path may therefore reproduce request authority
without invoking destructive workspace preparation.

## Source realization

A digest-shaped store path is not trusted by itself. Before execution, each
verified source object is reopened without following a final symlink, required
to remain a read-only regular file, streamed into an unpublished staged file,
and hashed again. Its byte count and digest must agree with both
`libpkgfetch` evidence and the sealed `libpkgbuild` request.

The staged files retain their declared `local_name` and are exposed in one
read-only source tree. No extension, MIME type, locator, or filename triggers
archive extraction, decompression, patching, or any other transformation.

## Execution translation

The adapter translates the request's build program into a build-purpose
execution request with:

- a closed `C.UTF-8`, UTC environment;
- denied networking;
- closed standard input and complete stdout/stderr capture;
- fixed numeric credentials and `no_new_privileges`;
- exact read-only source and package-input resources;
- exact writable workspace, package-output, and private temporary resources;
- no invented resource limits or cancellation policy.

The supplied root view must satisfy the selected backend's own root and mount
point contract. This adapter does not mutate or manufacture that root view.

## Payload inspection

A successful process is not a successful build. The package output root is
walked through directory descriptors without following symlinks. The adapter
seals canonical paths, object type, permission bits, numeric ownership,
whole-second modification time, regular-file size and SHA-256, hard-link
topology, symbolic-link targets, FIFO identity, and device numbers.

Regular files remain open from inspection through archive encoding. Their
metadata and content digest are checked again while bytes are written. Path
bindings and directories are rechecked for replacement during traversal.
Sockets and all unsupported object types fail result sealing.

`package_tar` uses restricted pax and therefore represents modification
time at whole-second precision. The payload manifest is normalized to that
representable precision before artifact encoding.

## Artifact publication and verification

The artifact contract writes only uncompressed `package_tar`. The archive is
encoded
in the destination directory under an unpublished temporary name, made
read-only, synchronized, and hashed. Publication uses a same-filesystem hard
link and fails if the final name already exists; existing artifact bytes are
never replaced.

After publication, `libpkgimage` opens the final artifact path with the exact
expected complete-archive digest. The adapter compares the normalized image to
the intended payload entry by entry and verifies that the final pathname still
names the retained inode. Any verification failure removes the newly published
artifact and seals a failed build result.

Diagnostics are retained operational evidence. Stable result-sealing failure
identity contains only the typed failure class, not diagnostic prose or host
paths.

## Durable build-execution evidence

The durable record belongs to this adapter because it binds two independent
owner results: exact `libpkgexec` process evidence and the corresponding
`libpkgbuild` outcome, including post-execution artifact sealing evidence.
The record embeds the canonical `libpkgexec 1.4` execution-result encoding and
adds only adapter-owned fields.

The record does not serialize a build request, execution request, backend
profile, admitted session, source materialization, package-input resource, host
path, credential policy, or execution resource. Decode requires the exact
`pkgbuild::build_request`, `pkgexec::execution_request`, and
`pkgexec::backend_capability_profile` bodies from their owning authorities.
Identity strings alone are not rehydration authority.

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
