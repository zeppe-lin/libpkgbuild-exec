// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include <libpkgbuild-exec/model.h>

#include <libpkgbuild-exec/error.h>

#include <algorithm>
#include <limits>
#include <set>

#include <sys/types.h>
#include <utility>

namespace pkgbuild_exec {
namespace {

std::filesystem::path normalize_absolute(
    const std::filesystem::path& value, std::string_view field,
    bool allow_root = false)
{
  if (value.empty() || !value.is_absolute()) {
    throw error(error_code::invalid_session,
                std::string(field) + " must be an absolute path");
  }
  const auto normalized = value.lexically_normal();
  if (!allow_root && normalized == std::filesystem::path("/")) {
    throw error(error_code::unsafe_path_layout,
                std::string(field) + " cannot be the filesystem root");
  }
  return normalized;
}

bool contains_or_equals(const std::filesystem::path& parent,
                        const std::filesystem::path& child)
{
  auto p = parent.begin();
  auto c = child.begin();
  for (; p != parent.end(); ++p, ++c) {
    if (c == child.end() || *p != *c) {
      return false;
    }
  }
  return true;
}

bool overlaps(const std::filesystem::path& first,
              const std::filesystem::path& second)
{
  return contains_or_equals(first, second) ||
         contains_or_equals(second, first);
}

void require_disjoint(const std::filesystem::path& first,
                      std::string_view first_name,
                      const std::filesystem::path& second,
                      std::string_view second_name)
{
  if (overlaps(first, second)) {
    throw error(error_code::unsafe_path_layout,
                std::string(first_name) + " overlaps " +
                std::string(second_name));
  }
}

} // namespace

std::string_view to_string(result_sealing_failure_kind value) noexcept
{
  switch (value) {
    case result_sealing_failure_kind::payload_inspection:
      return "payload-inspection";
    case result_sealing_failure_kind::artifact_encoding:
      return "artifact-encoding";
    case result_sealing_failure_kind::artifact_publication:
      return "artifact-publication";
    case result_sealing_failure_kind::artifact_verification:
      return "artifact-verification";
    case result_sealing_failure_kind::artifact_cleanup:
      return "artifact-cleanup";
  }
  return "unknown";
}

admitted_build_session::admitted_build_session(
    pkgbuild::build_request request,
    pkgfetch::source_materialization sources,
    std::vector<package_input_tree> package_inputs,
    session_paths paths,
    execution_identity identity,
    pkgbuild::artifact_compression compression)
    : request_(std::move(request)), sources_(std::move(sources)),
      package_inputs_(std::move(package_inputs)), paths_(std::move(paths)),
      identity_(std::move(identity)), compression_(compression)
{
}

admitted_build_session admitted_build_session::admit(
    pkgbuild::build_request request,
    pkgfetch::source_materialization sources,
    std::vector<package_input_tree> package_inputs,
    session_paths paths,
    execution_identity identity,
    pkgbuild::artifact_compression compression)
{
  if (request.policy().output_layout() !=
      pkgbuild::output_layout_kind::package_root_v1) {
    throw error(error_code::invalid_session,
                "unsupported build output layout");
  }
  if (compression != pkgbuild::artifact_compression::none) {
    throw error(error_code::invalid_session,
                "libpkgbuild-exec 0.1 admits only uncompressed package_tar_v1");
  }

  if (request.source().identity() != sources.source().identity()) {
    throw error(error_code::source_material_mismatch,
                "source materialization does not belong to the build request");
  }
  if (sources.objects().size() != request.sources().materials().size()) {
    throw error(error_code::source_material_mismatch,
                "source materialization cardinality differs from the build request");
  }

  std::set<std::string> observed_source_names;
  for (const auto& object : sources.objects()) {
    if (!observed_source_names.insert(
            object.declaration().local_name()).second) {
      throw error(error_code::source_material_mismatch,
                  "source materialization repeats a declared local name");
    }
    if (object.observed_digest() != object.declaration().content_digest()) {
      throw error(error_code::source_material_mismatch,
                  "verified source object contradicts its declaration digest");
    }
    (void)normalize_absolute(object.object_path(), "verified source object");
  }

  for (const auto& expected : request.sources().materials()) {
    const auto first = std::find_if(
        sources.objects().begin(), sources.objects().end(),
        [&](const pkgfetch::verified_source_object& object) {
          return object.declaration() == expected.declaration();
        });
    if (first == sources.objects().end()) {
      throw error(error_code::source_material_mismatch,
                  "a build-request source is absent from materialization");
    }
    const auto duplicate = std::find_if(
        std::next(first), sources.objects().end(),
        [&](const pkgfetch::verified_source_object& object) {
          return object.declaration() == expected.declaration();
        });
    if (duplicate != sources.objects().end()) {
      throw error(error_code::source_material_mismatch,
                  "a build-request source is materialized more than once");
    }
    if (first->observed_digest().hex() !=
        expected.observed_content().hex()) {
      throw error(error_code::source_material_mismatch,
                  "materialized source digest differs from the build request");
    }
  }

  if (package_inputs.size() != request.inputs().inputs().size()) {
    throw error(error_code::package_input_mismatch,
                "package-input tree cardinality differs from the build request");
  }

  std::vector<package_input_tree> normalized_inputs;
  normalized_inputs.reserve(request.inputs().inputs().size());
  std::vector<bool> consumed(package_inputs.size(), false);
  for (const auto& expected : request.inputs().inputs()) {
    std::optional<std::size_t> match;
    for (std::size_t index = 0; index < package_inputs.size(); ++index) {
      const auto& supplied = package_inputs[index];
      if (supplied.input == expected.resolved().identity() &&
          supplied.tree == expected.tree()) {
        if (match) {
          throw error(error_code::package_input_mismatch,
                      "a package-input subject is supplied more than once");
        }
        match = index;
      }
    }
    if (!match) {
      throw error(error_code::package_input_mismatch,
                  "a sealed package input lacks its concrete host tree");
    }
    consumed[*match] = true;
    auto supplied = package_inputs[*match];
    supplied.path = normalize_absolute(supplied.path, "package input tree");
    normalized_inputs.push_back(std::move(supplied));
  }
  if (std::find(consumed.begin(), consumed.end(), false) != consumed.end()) {
    throw error(error_code::package_input_mismatch,
                "an extra package-input tree is not present in the build request");
  }

  paths.root_view_path = normalize_absolute(paths.root_view_path, "root view");
  paths.session_root = normalize_absolute(paths.session_root, "session root");
  paths.package_output_root =
      normalize_absolute(paths.package_output_root, "package output root");
  paths.artifact_path = normalize_absolute(paths.artifact_path, "artifact path");
  if (paths.artifact_path.filename().empty()) {
    throw error(error_code::invalid_session,
                "artifact path must name a regular-file destination");
  }

  require_disjoint(paths.root_view_path, "root view", paths.session_root,
                   "session root");
  require_disjoint(paths.root_view_path, "root view",
                   paths.package_output_root, "package output root");
  require_disjoint(paths.session_root, "session root",
                   paths.package_output_root, "package output root");

  for (std::size_t index = 0; index < normalized_inputs.size(); ++index) {
    const auto& path = normalized_inputs[index].path;
    require_disjoint(path, "package input tree", paths.root_view_path,
                     "root view");
    require_disjoint(path, "package input tree", paths.session_root,
                     "session root");
    require_disjoint(path, "package input tree", paths.package_output_root,
                     "package output root");
    for (std::size_t previous = 0; previous < index; ++previous) {
      require_disjoint(path, "package input tree",
                       normalized_inputs[previous].path,
                       "package input tree");
    }
  }

  const std::vector<std::pair<std::filesystem::path, std::string_view>>
      mutable_or_mounted_roots{
          {paths.root_view_path, "root view"},
          {paths.session_root, "session root"},
          {paths.package_output_root, "package output root"},
      };
  for (const auto& value : mutable_or_mounted_roots) {
    require_disjoint(paths.artifact_path, "artifact path",
                     value.first, value.second);
  }
  for (const auto& input : normalized_inputs) {
    require_disjoint(paths.artifact_path, "artifact path", input.path,
                     "package input tree");
  }

  for (const auto& object : sources.objects()) {
    const auto object_path = object.object_path().lexically_normal();
    require_disjoint(object_path, "verified source object",
                     paths.session_root, "session root");
    require_disjoint(object_path, "verified source object",
                     paths.package_output_root, "package output root");
    require_disjoint(object_path, "verified source object",
                     paths.artifact_path, "artifact path");
  }

  if (identity.user_id >=
          static_cast<std::uint64_t>(std::numeric_limits<uid_t>::max()) ||
      identity.group_id >=
          static_cast<std::uint64_t>(std::numeric_limits<gid_t>::max()) ||
      std::any_of(identity.supplementary_groups.begin(),
                  identity.supplementary_groups.end(),
                  [](std::uint64_t value) {
                    return value >= static_cast<std::uint64_t>(
                                        std::numeric_limits<gid_t>::max());
                  })) {
    throw error(error_code::invalid_session,
                "execution credentials exceed native numeric identifier bounds");
  }

  std::sort(identity.supplementary_groups.begin(),
            identity.supplementary_groups.end());
  if (std::adjacent_find(identity.supplementary_groups.begin(),
                         identity.supplementary_groups.end()) !=
      identity.supplementary_groups.end()) {
    throw error(error_code::invalid_session,
                "supplementary execution groups must be unique");
  }
  if (std::binary_search(identity.supplementary_groups.begin(),
                         identity.supplementary_groups.end(),
                         identity.group_id)) {
    throw error(error_code::invalid_session,
                "primary execution group cannot be supplementary");
  }

  return admitted_build_session(
      std::move(request), std::move(sources), std::move(normalized_inputs),
      std::move(paths), std::move(identity), compression);
}

const pkgbuild::build_request&
admitted_build_session::request() const noexcept
{
  return request_;
}

const pkgfetch::source_materialization&
admitted_build_session::sources() const noexcept
{
  return sources_;
}

const std::vector<package_input_tree>&
admitted_build_session::package_inputs() const noexcept
{
  return package_inputs_;
}

const session_paths& admitted_build_session::paths() const noexcept
{
  return paths_;
}

const execution_identity& admitted_build_session::identity() const noexcept
{
  return identity_;
}

pkgbuild::artifact_compression
admitted_build_session::compression() const noexcept
{
  return compression_;
}

build_execution_result::build_execution_result(
    pkgexec::execution_result execution,
    pkgbuild::build_result build,
    std::optional<result_sealing_failure_kind> sealing_failure,
    std::string diagnostic,
    std::optional<pkgimage::archive_inspection_receipt> artifact_inspection)
    : execution_(std::move(execution)), build_(std::move(build)),
      sealing_failure_(sealing_failure), diagnostic_(std::move(diagnostic)),
      artifact_inspection_(std::move(artifact_inspection))
{
}

const pkgexec::execution_result&
build_execution_result::execution() const noexcept
{
  return execution_;
}

const pkgbuild::build_result& build_execution_result::build() const noexcept
{
  return build_;
}

const std::optional<result_sealing_failure_kind>&
build_execution_result::sealing_failure() const noexcept
{
  return sealing_failure_;
}

const std::string& build_execution_result::diagnostic() const noexcept
{
  return diagnostic_;
}

const std::optional<pkgimage::archive_inspection_receipt>&
build_execution_result::artifact_inspection() const noexcept
{
  return artifact_inspection_;
}

} // namespace pkgbuild_exec
