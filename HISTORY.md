Unreleased

- Treat an admitted `SOURCE_DATE_EPOCH` as package-image timestamp authority, not
  only as an execution-environment variable. Successful result sealing now
  canonicalizes every payload/archive entry mtime to the admitted epoch while
  retaining the transient package tree only as guarded encoding material.
- Strengthen deterministic artifact qualification by making two otherwise
  identical backend executions emit deliberately different wall-clock output
  mtimes and requiring identical payload, build, and `package_tar` authority.

Version: 3.3.0
Date: 2026-08-15

- Normalize the recipe-facing build-input namespace to the phase-local
  `PKG_BUILD_INPUT_ROOT=/build/inputs`; each admitted build dependency is
  projected at `/build/inputs/<canonical-package-name>`.
- Remove the obsolete `/build/inputs/build` nesting left behind when concrete
  check inputs moved out of construction. Exact admission remains keyed by
  `pkgbuild::build_input_identity`; only the execution coordinate changes.
- Preserve `libpkgbuild-exec.so.3`: no public C++ carrier, signature, layout, or
  virtual protocol changes. The canonical execution request identity changes
  naturally because its environment and logical mount points change.

Version: 3.2.0
Date: 2026-08-15

- Delegate raw verified source-object tree realization to `libpkgsource-exec`;
  keep archive expansion into the writable build workspace in this adapter.
- Keep raw-source realization phase-neutral while letting each execution
  composer mint its own concrete resource-instance identity. Build resource
  identity is therefore owned here rather than exported by libpkgsource-exec.
- Translate source-resource provider failures back into build-execution error
  vocabulary instead of leaking provider exceptions across this boundary.
- Restrict concrete construction package inputs to build scope. Check-scoped
  requirements remain sealed in the build request but are not mounted or
  exported through the build execution environment; independent check
  execution owns their concrete realization.

Version: 3.1.0
Date: 2026-08-15

- Split successful artifact sealing from caller-visible publication so a
  durable controller can persist terminal build evidence before exposing the
  final artifact name.
- Retain the verified package archive beneath the admitted session root until
  `publish_sealed_artifact()` projects those exact bytes to the public
  artifact path.
- Make evidence-backed publication idempotent without re-executing the build:
  an existing final path is accepted only when present observation proves the
  exact byte count and SHA-256 retained by terminal build evidence.
- Preserve one-shot `execute()` as execute/seal/publish convenience and keep
  the generation-3 SONAME; this is an additive API release with unchanged
  public carrier layouts.

Version: 3.0.1

- Treat an already-published artifact as an idempotent publication only when a
  fresh authorized execution seals exactly the same read-only bytes; differing
  pre-existing bytes still fail without replacement.
Date: 2026-08-14

- Rebuild `admitted_build_session` against `libpkgfetch.so.3` and advance the
  adapter SONAME to `libpkgbuild-exec.so.3`. The public session retains
  `pkgfetch::source_materialization` by value, so fetch's source-4 carrier ABI
  change is part of this adapter's binary contract even though its exported
  symbol names remain stable.
- Require `libpkgfetch >= 3.0.0, < 4.0.0`; refuse every source-3 retaining
  fetch generation at build, pkg-config, CI, and shared-object qualification.
- Preserve build-execution identity domains, durable evidence schema, source
  archive realization semantics, and the reviewed 28-symbol export inventory.
  This is an ABI-carrier rebuild, not a semantic reconstruction.

Version: 2.3.0
Date: 2026-08-14

- Realize only explicitly declared source archives before isolated execution.
- Keep verified raw source objects present under their sealed local names.
- Decode archives through a private libarchive backend while owning path, type,
  collision, ownership, and permission policy inside this adapter.
- Refuse escaping links, duplicate paths, special objects, and archive-entry
  collisions; never infer archive semantics from filenames.
- Preserve the shared-library SONAME at 2; the public adapter ABI is unchanged.
- Require `libpkgbuild >= 3.0.1` and `libpkgbuild-image >= 1.0.1` so the
  source-realization adapter cannot admit a resolver-3 transitive build closure.
- Require `libpkgfetch >= 2.1.1` and `libpkgexec >= 2.1.1`, excluding
  the tagged generation-2 providers that still admitted source ABI 3.
- Qualify the 2.3 boundary against source 4, catalog 4, resolver 4, and the
  resolver-4-bound build/image admission releases.

Version: 2.2.0
Date: 2026-08-07

- Freeze the generation-2 ELF ABI to an exact 28-symbol compiler-independent manifest.
- Bind execution authority to `libpkgexec 2.x` after verifying that all retained execution carriers remain layout-identical across the 1.x to 2.x owner correction.
- Qualify the shared product against the current resolver-3/build-3/exec-2 dependency closure.
- Bind the source-materialization dependency to `libpkgfetch 2.x`, the truthful
  SONAME generation for the source-3-shaped values already used by this ABI.
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
