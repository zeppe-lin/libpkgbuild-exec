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

#include <archive.h>
#include <archive_entry.h>
#include <libpkgfetch/libpkgfetch.h>

namespace pkgbuild_exec_test {


inline std::string archive_fixture_bytes()
{
  std::array<unsigned char, 65536> storage{};
  std::size_t used = 0;
  archive* raw = archive_write_new();
  require(raw != nullptr, "cannot allocate source archive fixture writer");
  std::unique_ptr<archive, decltype(&archive_write_free)> output(
      raw, archive_write_free);
  require(archive_write_set_format_pax_restricted(output.get()) == ARCHIVE_OK,
          "cannot select source archive fixture format");
  require(archive_write_open_memory(output.get(), storage.data(), storage.size(),
                                    &used) == ARCHIVE_OK,
          "cannot open source archive fixture buffer");

  static constexpr std::string_view payload = "archive payload\n";
  archive_entry* raw_file = archive_entry_new();
  require(raw_file != nullptr, "cannot allocate source archive fixture file");
  std::unique_ptr<archive_entry, decltype(&archive_entry_free)> file(
      raw_file, archive_entry_free);
  archive_entry_set_pathname(file.get(), "tree/hello.txt");
  archive_entry_set_filetype(file.get(), AE_IFREG);
  archive_entry_set_perm(file.get(), 04777);
  archive_entry_set_uid(file.get(), 1000);
  archive_entry_set_gid(file.get(), 1000);
  archive_entry_set_size(file.get(), static_cast<la_int64_t>(payload.size()));
  archive_entry_set_mtime(file.get(), 1700000000, 0);
  require(archive_write_header(output.get(), file.get()) == ARCHIVE_OK,
          "cannot write source archive fixture file header");
  require(archive_write_data(output.get(), payload.data(), payload.size()) ==
              static_cast<la_ssize_t>(payload.size()),
          "cannot write source archive fixture file payload");
  require(archive_write_close(output.get()) == ARCHIVE_OK,
          "cannot close source archive fixture writer");
  return std::string(reinterpret_cast<const char*>(storage.data()), used);
}

inline std::string single_file_archive_fixture_bytes(
    std::string_view pathname, std::string_view payload)
{
  std::array<unsigned char, 65536> storage{};
  std::size_t used = 0;
  archive* raw = archive_write_new();
  require(raw != nullptr, "cannot allocate custom source archive writer");
  std::unique_ptr<archive, decltype(&archive_write_free)> output(
      raw, archive_write_free);
  require(archive_write_set_format_pax_restricted(output.get()) == ARCHIVE_OK,
          "cannot select custom source archive format");
  require(archive_write_open_memory(output.get(), storage.data(), storage.size(),
                                    &used) == ARCHIVE_OK,
          "cannot open custom source archive buffer");

  archive_entry* raw_file = archive_entry_new();
  require(raw_file != nullptr, "cannot allocate custom source archive entry");
  std::unique_ptr<archive_entry, decltype(&archive_entry_free)> file(
      raw_file, archive_entry_free);
  archive_entry_set_pathname(file.get(), std::string(pathname).c_str());
  archive_entry_set_filetype(file.get(), AE_IFREG);
  archive_entry_set_perm(file.get(), 0644);
  archive_entry_set_size(file.get(), static_cast<la_int64_t>(payload.size()));
  archive_entry_set_mtime(file.get(), 1700000000, 0);
  require(archive_write_header(output.get(), file.get()) == ARCHIVE_OK,
          "cannot write custom source archive header");
  require(archive_write_data(output.get(), payload.data(), payload.size()) ==
              static_cast<la_ssize_t>(payload.size()),
          "cannot write custom source archive payload");
  require(archive_write_close(output.get()) == ARCHIVE_OK,
          "cannot close custom source archive writer");
  return std::string(reinterpret_cast<const char*>(storage.data()), used);
}

class fixture_owner final {
public:
  explicit fixture_owner(
      std::string suffix,
      std::string payload_bytes = "source bytes\n",
      std::string archive_bytes = {})
  {
    if (archive_bytes.empty()) {
      archive_bytes = archive_fixture_bytes();
    }
    const fs::path root = fs::temp_directory_path() /
        ("libpkgbuild-exec-test-" + std::to_string(::getpid()) + "-" +
         suffix);
    fs::remove_all(root);
    fs::create_directories(root / "local");
    fs::create_directories(root / "store");
    fs::create_directories(root / "root");
    fs::create_directories(root / "inputs/tool");
    fs::create_directories(root / "inputs/helper");
    fs::create_directories(root / "inputs/checker");
    write_file(root / "local/payload", payload_bytes);
    write_file(root / "local/archive.tar", archive_bytes);
    write_file(root / "inputs/tool/tool", "tool tree\n", 0555);
    write_file(root / "inputs/helper/helper", "helper tree\n", 0555);
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
      std::size_t index = 0;
      for (const auto& input :
           request.inputs().for_scope(pkgbuild::input_scope::build)) {
        const char seed = static_cast<char>('c' + index++);
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
