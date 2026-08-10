Version: 2.2.0
Date: 2026-08-07

- Freeze the generation-2 ELF ABI to an exact 28-symbol compiler-independent manifest.
- Qualify the current split source/state/catalog/resolve/build/fetch/exec/image closure.
- Add a staged installed consumer that executes session admission, build realization, and durable evidence round-trip.
- Expose a pure projection of adapter-owned prepared source, workspace, and temporary paths.
- Make effectful preparation consume the same path projection used by downstream composition.
- Preserve the shared-library SONAME at 2; this is an additive API release.
- Reject package-input resource aliasing before concrete execution admission.
- Bind returned execution evidence to the backend capability profile advertised
  for the call and translate backend exceptions into adapter-owned failure.

Version: 2.1.0
Date: 2026-08-07

- Add a pure canonical execution-request projection for admitted build sessions.
- Keep source staging and host-resource realization in the existing prepare path.
- Let restart composition reproduce request authority without touching workspaces.
- Preserve the shared-library SONAME at 2; this is an additive API release.

Version: 2.0.0
Date: 2026-08-05

- Admit resolver-issued logical build/check inputs with explicit call-scoped resources.
- Remove the forged package-input tree identity from the execution ABI.
- Consume genuine libpkgfetch materialization directly from the source snapshot.
- Delegate successful build/image admission to libpkgbuild-image.
- Advance the shared-library SONAME to 2 for the corrected session ABI.

Version: 1.1.0
Date: 2026-08-01

- Establish a canonical versioned codec for complete build-execution evidence.
- Embed libpkgexec 1.4 execution records without duplicating execution schema.
- Retain payload, artifact, archive-inspection, sealing, and diagnostic evidence.
- Require exact build request, execution request, and backend authorities at decode.
- Reject corruption, noncanonical bytes, and substituted semantic authorities.
- Keep the libpkgbuild-exec SONAME at 1 and raise libpkgexec to 1.4.0.

Version: 1.0.0
Date: 2026-07-29

ABI migration release for the generation-2 source/build closure.

- Rebuilt admitted build sessions against `libpkgfetch 1.0.0`,
  `libpkgbuild 2.0.0`, and `libpkgexec 1.3.0`.
- Advanced the shared-library SONAME to `libpkgbuild-exec.so.1` because
  admitted sessions and terminal results embed the changed fetch and build
  authority values by value.
- Preserved session admission, execution request derivation, artifact
  inspection, result sealing, and identity domains.
- Excluded source generation 1 from the runtime dependency closure.

Version: 0.1.0
Date: 2026-07-28

- Establish exact admitted build-execution sessions.
- Revalidate and stage verified raw source objects under sealed local names.
- Bind exact build and check input trees as read-only execution resources.
- Translate hermetic build policy into denied-network backend-neutral calls.
- Retain complete execution evidence and map execution failures into builds.
- Inspect complete package output roots without following symlinks.
- Produce deterministic uncompressed package-tar-v1 artifacts.
- Publish artifacts without replacement and verify the published bytes through
  libpkgimage before sealing build success.
