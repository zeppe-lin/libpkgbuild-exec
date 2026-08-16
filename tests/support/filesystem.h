// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "test.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace pkgbuild_exec_test {
namespace fs = std::filesystem;

inline void write_file(const fs::path& path, std::string_view bytes,
                       mode_t mode = 0644)
{
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  require(static_cast<bool>(output), "cannot create fixture file");
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  output.close();
  require(static_cast<bool>(output), "cannot write fixture file");
  require(::chmod(path.c_str(), mode) == 0, "cannot chmod fixture file");
}

inline std::string read_file(const fs::path& path)
{
  std::ifstream input(path, std::ios::binary);
  require(static_cast<bool>(input),
          "cannot open fixture file: " + path.string());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

inline void set_mtime(const fs::path& path, bool symlink = false,
                      std::int64_t seconds = 1700000000,
                      long nanoseconds = 123456789)
{
  const timespec value[2]{{static_cast<time_t>(seconds), nanoseconds},
                          {static_cast<time_t>(seconds), nanoseconds}};
  require(::utimensat(AT_FDCWD, path.c_str(), value,
                      symlink ? AT_SYMLINK_NOFOLLOW : 0) == 0,
          "cannot set fixture mtime");
}

} // namespace pkgbuild_exec_test
