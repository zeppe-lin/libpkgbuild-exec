// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../fixtures/session.h"
#include "../support/filesystem.h"
#include "../support/test.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace {

using namespace pkgbuild_exec_test;
namespace fs = std::filesystem;

void expect_archive_refusal(std::string name, std::string bytes)
{
  fixture_owner owner(std::move(name), "source bytes\n", std::move(bytes));
  expect_error(pkgbuild_exec::error_code::source_staging_failed, [&] {
    (void)pkgbuild_exec::prepare(owner.get().session("archive-refusal"));
  });
}

void realizes_safe_link_algebra()
{
  const auto bytes = archive_fixture_bytes({
      {"tree/", AE_IFDIR, 0750, {}, {}, {}, 100},
      {"tree/value", AE_IFREG, 04777, "payload\n", {}, {}, 101},
      {"tree/link", AE_IFLNK, 0777, {}, "value", {}, 102},
      {"tree/hard", AE_IFREG, 0777, {}, {}, "tree/value", 103},
  });
  fixture_owner owner("archive-links", "source bytes\n", bytes);
  const auto prepared = pkgbuild_exec::prepare(owner.get().session("links"));
  const auto tree = prepared.workspace / "tree";
  require(read_file(tree / "value") == "payload\n",
          "safe source archive regular payload changed");
  require(fs::read_symlink(tree / "link") == "value",
          "safe source archive symlink target changed");
  struct stat value {};
  struct stat hard {};
  struct stat link {};
  struct stat directory {};
  require(::stat((tree / "value").c_str(), &value) == 0 &&
              ::stat((tree / "hard").c_str(), &hard) == 0 &&
              value.st_dev == hard.st_dev && value.st_ino == hard.st_ino,
          "safe source archive hard link lost inode identity");
  require((value.st_mode & 07777U) == 0755U && value.st_mtim.tv_sec == 101,
          "source archive regular metadata escaped build umask/mtime authority");
  require(::lstat((tree / "link").c_str(), &link) == 0 &&
              S_ISLNK(link.st_mode) && link.st_mtim.tv_sec == 102,
          "source archive symlink metadata changed");
  require(::stat(tree.c_str(), &directory) == 0 &&
              (directory.st_mode & 07777U) == 0750U &&
              directory.st_mtim.tv_sec == 100,
          "source archive directory metadata changed");
}

void rejects_path_and_link_escape()
{
  expect_archive_refusal(
      "archive-dotdot",
      archive_fixture_bytes({{"../escaped", AE_IFREG, 0644, "x"}}));
  expect_archive_refusal(
      "archive-absolute",
      archive_fixture_bytes({{"/escaped", AE_IFREG, 0644, "x"}}));
  expect_archive_refusal(
      "archive-symlink-up",
      archive_fixture_bytes({{"tree/link", AE_IFLNK, 0777, {}, "../../outside"}}));
  expect_archive_refusal(
      "archive-symlink-absolute",
      archive_fixture_bytes({{"tree/link", AE_IFLNK, 0777, {}, "/outside"}}));
  expect_archive_refusal(
      "archive-hardlink-up",
      archive_fixture_bytes({
          {"tree/value", AE_IFREG, 0644, "x"},
          {"tree/hard", AE_IFREG, 0644, {}, {}, "../value"},
      }));
}

void rejects_ambiguous_or_unsupported_tree()
{
  expect_archive_refusal(
      "archive-forward-hardlink",
      archive_fixture_bytes({
          {"tree/hard", AE_IFREG, 0644, {}, {}, "tree/value"},
          {"tree/value", AE_IFREG, 0644, "x"},
      }));
  expect_archive_refusal(
      "archive-duplicate-file",
      archive_fixture_bytes({
          {"tree/value", AE_IFREG, 0644, "one"},
          {"tree/value", AE_IFREG, 0644, "two"},
      }));
  expect_archive_refusal(
      "archive-parent-collision",
      archive_fixture_bytes({
          {"tree", AE_IFREG, 0644, "not-a-directory"},
          {"tree/value", AE_IFREG, 0644, "x"},
      }));
  expect_archive_refusal(
      "archive-duplicate-directory",
      archive_fixture_bytes({
          {"tree/", AE_IFDIR, 0755},
          {"tree/", AE_IFDIR, 0755},
      }));
  expect_archive_refusal(
      "archive-fifo",
      archive_fixture_bytes({{"tree/pipe", AE_IFIFO, 0644}}));
}

void rejects_non_archive_and_empty_archive()
{
  expect_archive_refusal("archive-raw", "not an archive payload\n");
  expect_archive_refusal("archive-empty", archive_fixture_bytes({}));
}

} // namespace

int main()
{
  try {
    realizes_safe_link_algebra();
    rejects_path_and_link_escape();
    rejects_ambiguous_or_unsupported_tree();
    rejects_non_archive_and_empty_archive();
  } catch (const std::exception& value) {
    std::cerr << value.what() << '\n';
    return 1;
  }
  return 0;
}
