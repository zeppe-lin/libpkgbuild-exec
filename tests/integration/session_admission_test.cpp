// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../fixtures/session.h"
#include "../support/test.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <sys/types.h>

namespace {

using namespace pkgbuild_exec_test;

pkgbuild_exec::admitted_build_session admit(
    const fixture_owner::state& state,
    std::vector<pkgbuild_exec::package_input_resource> inputs,
    pkgbuild_exec::session_paths paths,
    pkgbuild_exec::execution_identity identity,
    pkgfetch::source_materialization materialization,
    pkgbuild::artifact_compression compression =
        pkgbuild::artifact_compression::none)
{
  return pkgbuild_exec::admitted_build_session::admit(
      state.request, std::move(materialization), std::move(inputs),
      std::move(paths), std::move(identity), compression);
}

void canonicalizes_input_order()
{
  fixture_owner owner("admission-order");
  auto inputs = owner.get().package_inputs();
  std::reverse(inputs.begin(), inputs.end());
  auto session = admit(owner.get(), std::move(inputs), owner.get().paths(),
                       owner.get().execution_identity(),
                       owner.get().materialization);
  require(session.package_inputs().size() ==
              owner.get().request.inputs().inputs().size(),
          "admission changed package-input cardinality");
  for (std::size_t index = 0; index < session.package_inputs().size(); ++index) {
    require(session.package_inputs()[index].input ==
                owner.get().request.inputs().inputs()[index].identity(),
            "package-input resources were not normalized to request order");
  }
}

void rejects_source_authority_mismatch()
{
  fixture_owner owner("admission-source");
  fixture_owner other("admission-source-other", "different source bytes\n");
  expect_error(pkgbuild_exec::error_code::source_material_mismatch, [&] {
    (void)admit(owner.get(), owner.get().package_inputs(), owner.get().paths(),
                owner.get().execution_identity(), other.get().materialization);
  });
}

void rejects_input_set_mismatch()
{
  fixture_owner owner("admission-input-set");

  auto missing = owner.get().package_inputs();
  missing.pop_back();
  expect_error(pkgbuild_exec::error_code::package_input_mismatch, [&] {
    (void)admit(owner.get(), missing, owner.get().paths("missing"),
                owner.get().execution_identity(), owner.get().materialization);
  });

  auto duplicate = owner.get().package_inputs();
  duplicate[1].input = duplicate[0].input;
  expect_error(pkgbuild_exec::error_code::package_input_mismatch, [&] {
    (void)admit(owner.get(), duplicate, owner.get().paths("duplicate"),
                owner.get().execution_identity(), owner.get().materialization);
  });

  auto aliased = owner.get().package_inputs();
  aliased[1].resource = aliased[0].resource;
  expect_error(pkgbuild_exec::error_code::package_input_mismatch, [&] {
    (void)admit(owner.get(), aliased, owner.get().paths("alias"),
                owner.get().execution_identity(), owner.get().materialization);
  });
}

void rejects_unsafe_paths()
{
  fixture_owner owner("admission-paths");

  auto relative = owner.get().package_inputs();
  relative[0].path = "relative/tool";
  expect_error(pkgbuild_exec::error_code::invalid_session, [&] {
    (void)admit(owner.get(), relative, owner.get().paths("relative"),
                owner.get().execution_identity(), owner.get().materialization);
  });

  auto root_paths = owner.get().paths("root");
  root_paths.session_root = "/";
  expect_error(pkgbuild_exec::error_code::unsafe_path_layout, [&] {
    (void)admit(owner.get(), owner.get().package_inputs(), root_paths,
                owner.get().execution_identity(), owner.get().materialization);
  });

  auto overlap = owner.get().paths("overlap");
  overlap.artifact_path = overlap.session_root / "artifact.tar";
  expect_error(pkgbuild_exec::error_code::unsafe_path_layout, [&] {
    (void)admit(owner.get(), owner.get().package_inputs(), overlap,
                owner.get().execution_identity(), owner.get().materialization);
  });

  auto input_overlap = owner.get().package_inputs();
  input_overlap[1].path = input_overlap[0].path / "nested";
  expect_error(pkgbuild_exec::error_code::unsafe_path_layout, [&] {
    (void)admit(owner.get(), input_overlap, owner.get().paths("input-overlap"),
                owner.get().execution_identity(), owner.get().materialization);
  });
}

void canonicalizes_and_rejects_credentials()
{
  fixture_owner owner("admission-credentials");
  const auto primary = static_cast<std::uint64_t>(::getgid());
  const auto first = primary == 41U ? 42U : 41U;
  const auto second = first == 42U ? 43U : 42U;

  auto session = admit(
      owner.get(), owner.get().package_inputs(), owner.get().paths("groups"),
      owner.get().execution_identity({second, first}),
      owner.get().materialization);
  require(session.identity().supplementary_groups ==
              std::vector<std::uint64_t>({first, second}),
          "supplementary groups were not canonicalized");

  expect_error(pkgbuild_exec::error_code::invalid_session, [&] {
    (void)admit(owner.get(), owner.get().package_inputs(),
                owner.get().paths("duplicate-group"),
                owner.get().execution_identity({first, first}),
                owner.get().materialization);
  });

  expect_error(pkgbuild_exec::error_code::invalid_session, [&] {
    (void)admit(owner.get(), owner.get().package_inputs(),
                owner.get().paths("primary-group"),
                owner.get().execution_identity({primary}),
                owner.get().materialization);
  });

  if (std::numeric_limits<uid_t>::max() !=
      std::numeric_limits<std::uint64_t>::max()) {
    auto identity = owner.get().execution_identity();
    identity.user_id =
        static_cast<std::uint64_t>(std::numeric_limits<uid_t>::max());
    expect_error(pkgbuild_exec::error_code::invalid_session, [&] {
      (void)admit(owner.get(), owner.get().package_inputs(),
                  owner.get().paths("uid-bound"), identity,
                  owner.get().materialization);
    });
  }
}

void rejects_unsupported_artifact_compression()
{
  fixture_owner owner("admission-compression");
  expect_error(pkgbuild_exec::error_code::invalid_session, [&] {
    (void)admit(owner.get(), owner.get().package_inputs(), owner.get().paths(),
                owner.get().execution_identity(), owner.get().materialization,
                pkgbuild::artifact_compression::zstd);
  });
}

} // namespace

int main()
{
  try {
    canonicalizes_input_order();
    rejects_source_authority_mismatch();
    rejects_input_set_mismatch();
    rejects_unsafe_paths();
    canonicalizes_and_rejects_credentials();
    rejects_unsupported_artifact_compression();
  } catch (const std::exception& value) {
    std::cerr << value.what() << '\n';
    return 1;
  }
  return 0;
}
