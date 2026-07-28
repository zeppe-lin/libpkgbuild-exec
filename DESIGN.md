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
package_tar_v1 encoding and non-replacing publication
        |
libpkgimage inspection of the published bytes
        |
libpkgbuild result
```

Locators, cache paths, package-input host paths, session directories, and the
artifact destination are effect coordinates. They do not enter native build
identity. The caller supplies one exact root view, interpreter identity,
numeric credential policy, concrete tree for every sealed package input, and
explicit call-scoped paths.

The adapter depends only on the backend-neutral `pkgexec::execution_backend`.
Backend construction and `libpkgexec-linux` selection belong to orchestration.

## Admission

A session is admitted only when:

- the fetch materialization belongs to the request's exact source snapshot;
- every sealed source occurs exactly once with the required SHA-256;
- every sealed package input has exactly one matching input-tree identity;
- root, session, package-output, artifact, and package-input coordinates are
  absolute and do not overlap in ways that would mix authority with effects;
- the output layout is `package_root_v1`;
- artifact compression is explicitly `none`.

Package-input tree identities are upstream authority. This adapter binds their
exact host coordinates and asks the execution backend to realize them
read-only; it does not invent a second package-image or dependency verifier.

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

Version 0.1 translates the request's build program into a build-purpose
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

`package_tar_v1` uses restricted pax and therefore represents modification
time at whole-second precision. The payload manifest is normalized to that
representable precision before artifact encoding.

## Artifact publication and verification

Version 0.1 writes only uncompressed `package_tar_v1`. The archive is encoded
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
