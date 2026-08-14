// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../fixtures/session.h"
#include "../support/filesystem.h"
#include "../support/test.h"

#include <iostream>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

namespace {

using namespace pkgbuild_exec_test;

void prepares_exact_resources()
{
  fixture_owner owner("prepare");
  const auto session = owner.get().session();
  const auto projected = pkgbuild_exec::seal_execution_request(session);
  const auto prepared = pkgbuild_exec::prepare(session);

  require(prepared.request == projected,
          "effectful preparation changed canonical request authority");
  require(read_file(prepared.source_tree / "payload") == "source bytes\n" &&
              read_file(prepared.source_tree / "archive.tar") ==
                  archive_fixture_bytes(),
          "source staging changed verified raw bytes");
  require(read_file(prepared.workspace / "tree/hello.txt") ==
              "archive payload\n",
          "declared source archive was not realized into the build workspace");
  require((fs::status(prepared.source_tree).permissions() &
           fs::perms::owner_write) == fs::perms::none &&
              (fs::status(prepared.source_tree / "payload").permissions() &
               fs::perms::owner_write) == fs::perms::none,
          "staged source authority is writable");
  struct stat realized_status {};
  require(::stat((prepared.workspace / "tree/hello.txt").c_str(),
                 &realized_status) == 0,
          "cannot inspect realized source archive file");
  require(static_cast<std::uint64_t>(realized_status.st_uid) ==
              session.identity().user_id &&
              static_cast<std::uint64_t>(realized_status.st_gid) ==
                  session.identity().group_id,
          "archive ownership escaped admitted build credentials");
  require((realized_status.st_mode & 07777U) == 0755U,
          "archive mode escaped admitted build umask or retained privilege bits");
  struct stat realized_parent_status {};
  require(::stat((prepared.workspace / "tree").c_str(),
                 &realized_parent_status) == 0,
          "cannot inspect implicit realized source archive directory");
  require(realized_parent_status.st_mtim.tv_sec == 0 &&
              realized_parent_status.st_mtim.tv_nsec == 0,
          "implicit archive directory retained host-clock metadata");
  require(fs::is_directory(prepared.workspace / "home") &&
              fs::is_directory(prepared.temporary_root) &&
              fs::is_directory(session.paths().package_output_root),
          "writable build resources were not prepared");

  for (const auto& binding : prepared.request.resources().bindings()) {
    const auto& material =
        prepared.resources.materialization(binding.resource());
    require(material.host_path().is_absolute(),
            "prepared resource materialization is not absolute");
  }

  for (const auto& path : {
           prepared.workspace,
           prepared.temporary_root,
           session.paths().package_output_root,
       }) {
    struct stat status {};
    require(::stat(path.c_str(), &status) == 0,
            "cannot inspect writable prepared resource");
    require(static_cast<std::uint64_t>(status.st_uid) ==
                session.identity().user_id &&
                static_cast<std::uint64_t>(status.st_gid) ==
                    session.identity().group_id,
            "prepared writable resource ownership changed");
  }
}

void rejects_changed_source_bytes()
{
  fixture_owner owner("prepare-mutated-source");
  const fs::path object = owner.get().materialization.objects()[0].object_path();
  require(::chmod(object.c_str(), 0644) == 0,
          "cannot make source fixture mutable");
  write_file(object, "mutated bytes\n", 0644);
  expect_error(pkgbuild_exec::error_code::source_staging_failed, [&] {
    (void)pkgbuild_exec::prepare(owner.get().session());
  });
}

void rejects_replaced_source_path()
{
  fixture_owner owner("prepare-source-symlink");
  const fs::path object = owner.get().materialization.objects()[0].object_path();
  const fs::path replacement = owner.get().root / "replacement";
  write_file(replacement, "source bytes\n", 0444);
  require(fs::remove(object), "cannot remove verified source object fixture");
  require(::symlink(replacement.c_str(), object.c_str()) == 0,
          "cannot replace verified source object with symlink");
  expect_error(pkgbuild_exec::error_code::source_staging_failed, [&] {
    (void)pkgbuild_exec::prepare(owner.get().session());
  });
}

void rejects_archive_path_escape()
{
  fixture_owner owner(
      "prepare-archive-escape", "source bytes\n",
      single_file_archive_fixture_bytes("../escaped", "escaped\n"));
  const auto session = owner.get().session("archive-escape");
  const auto paths = pkgbuild_exec::project_prepared_paths(session);
  const auto escaped = paths.workspace.parent_path() / "escaped";
  expect_error(pkgbuild_exec::error_code::source_staging_failed, [&] {
    (void)pkgbuild_exec::prepare(session);
  });
  require(!fs::exists(escaped),
          "source archive escaped the admitted build workspace");
}

} // namespace

int main()
{
  try {
    prepares_exact_resources();
    rejects_changed_source_bytes();
    rejects_replaced_source_path();
    rejects_archive_path_escape();
  } catch (const std::exception& value) {
    std::cerr << value.what() << '\n';
    return 1;
  }
  return 0;
}
