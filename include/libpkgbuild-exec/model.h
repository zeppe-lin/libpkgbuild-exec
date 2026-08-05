// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

/*! \file model.h
 *  \brief Call-scoped build realization and retained adapter evidence.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <libpkgbuild/libpkgbuild.h>
#include <libpkgexec/libpkgexec.h>
#include <libpkgfetch/libpkgfetch.h>
#include <libpkgbuild-image/libpkgbuild-image.h>

namespace pkgbuild_exec {

namespace detail {
class codec_access;
class executor_access;
}

/*! \brief Stable class of a post-execution result-sealing failure. */
enum class result_sealing_failure_kind {
  payload_inspection,
  artifact_encoding,
  artifact_publication,
  artifact_verification,
  artifact_cleanup,
};

[[nodiscard]] std::string_view to_string(
    result_sealing_failure_kind value) noexcept;

/*! \brief One logical build input and its call-scoped host resource. */
struct package_input_resource final {
  pkgbuild::build_input_identity input;
  pkgexec::resource_identity resource;
  std::filesystem::path path;
};

/*! \brief Explicit effect coordinates for one build realization. */
struct session_paths final {
  pkgexec::root_view_identity root_view;
  std::filesystem::path root_view_path;
  std::filesystem::path session_root;
  std::filesystem::path package_output_root;
  std::filesystem::path artifact_path;
};

/*! \brief Exact interpreter and numeric credentials requested from a backend. */
struct execution_identity final {
  pkgexec::interpreter_identity interpreter;
  std::uint64_t user_id = 0;
  std::uint64_t group_id = 0;
  std::vector<std::uint64_t> supplementary_groups;
};

/*! \brief A complete call-scoped build session admitted before mutation. */
class admitted_build_session final {
public:
  [[nodiscard]] static admitted_build_session admit(
      pkgbuild::build_request request,
      pkgfetch::source_materialization sources,
      std::vector<package_input_resource> package_inputs,
      session_paths paths,
      execution_identity identity,
      pkgbuild::artifact_compression compression =
          pkgbuild::artifact_compression::none);

  [[nodiscard]] const pkgbuild::build_request& request() const noexcept;
  [[nodiscard]] const pkgfetch::source_materialization& sources() const noexcept;
  [[nodiscard]] const std::vector<package_input_resource>&
  package_inputs() const noexcept;
  [[nodiscard]] const session_paths& paths() const noexcept;
  [[nodiscard]] const execution_identity& identity() const noexcept;
  [[nodiscard]] pkgbuild::artifact_compression compression() const noexcept;

private:
  admitted_build_session(pkgbuild::build_request request,
                         pkgfetch::source_materialization sources,
                         std::vector<package_input_resource> package_inputs,
                         session_paths paths,
                         execution_identity identity,
                         pkgbuild::artifact_compression compression);

  pkgbuild::build_request request_;
  pkgfetch::source_materialization sources_;
  std::vector<package_input_resource> package_inputs_;
  session_paths paths_;
  execution_identity identity_;
  pkgbuild::artifact_compression compression_;
};

/*! \brief Backend-neutral request and exact call-scoped resources. */
struct prepared_execution final {
  pkgexec::execution_request request;
  pkgexec::execution_resources resources;
  std::filesystem::path source_tree;
  std::filesystem::path workspace;
  std::filesystem::path temporary_root;
};

/*! \brief Retained execution evidence and the corresponding sealed build result. */
class build_execution_result final {
public:
  [[nodiscard]] const pkgexec::execution_result& execution() const noexcept;
  [[nodiscard]] const pkgbuild::build_result& build() const noexcept;
  [[nodiscard]] const std::optional<result_sealing_failure_kind>&
  sealing_failure() const noexcept;
  [[nodiscard]] const std::string& diagnostic() const noexcept;
  [[nodiscard]] const std::optional<
      pkgbuild::image_adapter::build_image_authority>&
  image_authority() const noexcept;

private:
  friend class detail::codec_access;
  friend class detail::executor_access;
  build_execution_result(
      pkgexec::execution_result execution,
      pkgbuild::build_result build,
      std::optional<result_sealing_failure_kind> sealing_failure,
      std::string diagnostic,
      std::optional<pkgbuild::image_adapter::build_image_authority>
          image_authority);

  pkgexec::execution_result execution_;
  pkgbuild::build_result build_;
  std::optional<result_sealing_failure_kind> sealing_failure_;
  std::string diagnostic_;
  std::optional<pkgbuild::image_adapter::build_image_authority>
      image_authority_;
};

} // namespace pkgbuild_exec
