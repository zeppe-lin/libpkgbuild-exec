// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "authority.h"
#include "../support/filesystem.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <libpkgbuild-exec/libpkgbuild-exec.h>
#include <libpkgfetch/libpkgfetch.h>

namespace pkgbuild_exec_test {

class fixture_owner final {
public:
  explicit fixture_owner(
      std::string suffix,
      std::string payload_bytes = "source bytes\n",
      std::string archive_bytes = "raw archive bytes\n")
  {
    const fs::path root = fs::temp_directory_path() /
        ("libpkgbuild-exec-test-" + std::to_string(::getpid()) + "-" +
         suffix);
    fs::remove_all(root);
    fs::create_directories(root / "local");
    fs::create_directories(root / "store");
    fs::create_directories(root / "root");
    fs::create_directories(root / "inputs/tool");
    fs::create_directories(root / "inputs/checker");
    write_file(root / "local/payload", payload_bytes);
    write_file(root / "local/archive.tar", archive_bytes);
    write_file(root / "inputs/tool/tool", "tool tree\n", 0555);
    write_file(root / "inputs/checker/checker", "checker tree\n", 0555);

    auto resolved = resolution(sha256_text(payload_bytes),
                               sha256_text(archive_bytes));
    auto request = build_request(resolved);
    auto source = request.source();
    auto materialization = pkgfetch::materialize(
        pkgfetch::materialization_request::seal(
            source, root / "local", root / "store"));
    value_ = std::make_unique<state>(state{
        root, std::move(resolved), std::move(source),
        std::move(materialization), std::move(request)});
  }

  ~fixture_owner()
  {
    if (value_) {
      std::error_code ignored;
      fs::remove_all(value_->root, ignored);
    }
  }

  fixture_owner(const fixture_owner&) = delete;
  fixture_owner& operator=(const fixture_owner&) = delete;

  struct state final {
    fs::path root;
    pkgresolve::resolution_result resolution;
    pkgsource::source_snapshot source;
    pkgfetch::source_materialization materialization;
    pkgbuild::build_request request;

    [[nodiscard]] std::vector<pkgbuild_exec::package_input_resource>
    package_inputs(const fs::path& base = {}) const
    {
      const fs::path resource_root = base.empty() ? root / "inputs" : base;
      std::vector<pkgbuild_exec::package_input_resource> resources;
      for (const auto& input : request.inputs().inputs()) {
        const char seed =
            input.scope() == pkgbuild::input_scope::build ? 'c' : 'd';
        resources.push_back({
            input.identity(),
            pkgexec::resource_identity::from_sha256(std::string(64U, seed)),
            resource_root / input.package().name(),
        });
      }
      return resources;
    }

    [[nodiscard]] pkgbuild_exec::session_paths paths(
        std::string name = "one") const
    {
      return {
          pkgexec::root_view_identity::from_sha256(std::string(64U, 'b')),
          root / "root",
          root / ("session-" + name),
          root / ("package-" + name),
          root / ("artifact-" + name + ".tar"),
      };
    }

    [[nodiscard]] pkgbuild_exec::execution_identity execution_identity(
        std::vector<std::uint64_t> groups = {}) const
    {
      return {
          pkgexec::interpreter_identity::from_sha256(std::string(64U, 'c')),
          static_cast<std::uint64_t>(::getuid()),
          static_cast<std::uint64_t>(::getgid()),
          std::move(groups),
      };
    }

    [[nodiscard]] pkgbuild_exec::admitted_build_session session(
        std::string name = "one") const
    {
      return pkgbuild_exec::admitted_build_session::admit(
          request, materialization, package_inputs(), paths(std::move(name)),
          execution_identity());
    }
  };

  state& get() { return *value_; }
  const state& get() const { return *value_; }

private:
  std::unique_ptr<state> value_;
};

} // namespace pkgbuild_exec_test
