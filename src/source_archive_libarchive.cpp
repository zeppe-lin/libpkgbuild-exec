// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include "source_archive_backend.h"

#include <libpkgbuild-exec/error.h>

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace pkgbuild_exec::detail {
namespace {

class unique_fd final {
public:
  unique_fd() noexcept = default;
  explicit unique_fd(int value) noexcept : value_(value) {}
  ~unique_fd() { reset(); }
  unique_fd(const unique_fd&) = delete;
  unique_fd& operator=(const unique_fd&) = delete;
  unique_fd(unique_fd&& other) noexcept : value_(other.release()) {}
  unique_fd& operator=(unique_fd&& other) noexcept
  {
    if (this != &other) {
      reset(other.release());
    }
    return *this;
  }
  [[nodiscard]] int get() const noexcept { return value_; }
  [[nodiscard]] explicit operator bool() const noexcept { return value_ >= 0; }
  [[nodiscard]] int release() noexcept
  {
    const int value = value_;
    value_ = -1;
    return value;
  }
  void reset(int value = -1) noexcept
  {
    if (value_ >= 0) {
      (void)::close(value_);
    }
    value_ = value;
  }
private:
  int value_ = -1;
};

[[noreturn]] void fail(std::string message)
{
  throw error(error_code::source_staging_failed, std::move(message));
}

[[noreturn]] void fail_errno(std::string_view operation, int value = errno)
{
  fail(std::string(operation) + ": " + std::strerror(value));
}

struct safe_path final {
  std::string text;
  std::vector<std::string> components;
};

safe_path parse_archive_path(const char* raw)
{
  if (!raw || *raw == '\0') {
    fail("archive entry has an empty pathname");
  }
  std::string text(raw);
  while (text.size() > 1U && text.back() == '/') {
    text.pop_back();
  }
  if (text.empty() || text.front() == '/') {
    fail("archive entry pathname is not relative");
  }

  std::vector<std::string> components;
  std::size_t offset = 0;
  while (offset < text.size()) {
    const std::size_t slash = text.find('/', offset);
    const std::size_t end = slash == std::string::npos ? text.size() : slash;
    const std::string component = text.substr(offset, end - offset);
    if (component.empty() || component == "." || component == "..") {
      fail("archive entry pathname is not canonical");
    }
    components.push_back(component);
    if (slash == std::string::npos) {
      break;
    }
    offset = slash + 1U;
  }
  if (components.empty()) {
    fail("archive entry pathname is empty after normalization");
  }
  return {std::move(text), std::move(components)};
}

std::string join_components(const std::vector<std::string>& components,
                            std::size_t count)
{
  std::string result;
  for (std::size_t index = 0; index < count; ++index) {
    if (!result.empty()) {
      result.push_back('/');
    }
    result += components[index];
  }
  return result;
}

struct directory_metadata final {
  mode_t mode = 0755;
  timespec modification{0, 0};
  bool explicit_entry = false;
};

using directory_map = std::map<std::string, directory_metadata>;

mode_t admitted_mode(mode_t archive_mode, mode_t mask) noexcept
{
  return static_cast<mode_t>((archive_mode & 0777U) & ~mask);
}

timespec entry_mtime(archive_entry* entry) noexcept
{
  return {static_cast<time_t>(archive_entry_mtime(entry)),
          static_cast<long>(archive_entry_mtime_nsec(entry))};
}

void set_fd_mtime(int fd, timespec modification)
{
  const timespec times[2] = {{0, UTIME_OMIT}, modification};
  if (::futimens(fd, times) != 0) {
    fail_errno("set extracted source modification time");
  }
}

unique_fd open_child_directory(int parent, const std::string& name)
{
  unique_fd result(::openat(parent, name.c_str(),
                            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (!result) {
    fail_errno("open extracted source directory");
  }
  return result;
}

unique_fd open_parent(int root, const safe_path& path, directory_map& directories,
                      mode_t mask)
{
  unique_fd current(::dup(root));
  if (!current) {
    fail_errno("duplicate source root descriptor");
  }
  for (std::size_t index = 0; index + 1U < path.components.size(); ++index) {
    const std::string prefix = join_components(path.components, index + 1U);
    struct stat info {};
    if (::fstatat(current.get(), path.components[index].c_str(), &info,
                  AT_SYMLINK_NOFOLLOW) != 0) {
      if (errno != ENOENT) {
        fail_errno("inspect extracted source parent");
      }
      if (::mkdirat(current.get(), path.components[index].c_str(), 0700) != 0) {
        fail_errno("create extracted source parent");
      }
      directories.emplace(
          prefix, directory_metadata{admitted_mode(0777, mask), {0, 0}, false});
    } else if (!S_ISDIR(info.st_mode)) {
      fail("archive entry parent collides with a non-directory source path");
    }
    current = open_child_directory(current.get(), path.components[index]);
  }
  return current;
}

unique_fd open_existing_parent(int root, const safe_path& path)
{
  unique_fd current(::dup(root));
  if (!current) {
    fail_errno("duplicate source root descriptor");
  }
  for (std::size_t index = 0; index + 1U < path.components.size(); ++index) {
    current = open_child_directory(current.get(), path.components[index]);
  }
  return current;
}

void require_absent(int parent, const std::string& name)
{
  struct stat info {};
  if (::fstatat(parent, name.c_str(), &info, AT_SYMLINK_NOFOLLOW) == 0) {
    fail("archive entry collides with an existing source path");
  }
  if (errno != ENOENT) {
    fail_errno("inspect archive extraction destination");
  }
}

void write_regular_data(archive* input, int descriptor)
{
  std::array<unsigned char, 65536> buffer{};
  for (;;) {
    const la_ssize_t count = archive_read_data(input, buffer.data(), buffer.size());
    if (count == 0) {
      return;
    }
    if (count < 0) {
      const char* diagnostic = archive_error_string(input);
      fail(std::string("read source archive payload: ") +
           (diagnostic ? diagnostic : "libarchive failure"));
    }
    std::size_t offset = 0;
    while (offset < static_cast<std::size_t>(count)) {
      const ssize_t written = ::write(descriptor, buffer.data() + offset,
                                      static_cast<std::size_t>(count) - offset);
      if (written < 0 && errno == EINTR) {
        continue;
      }
      if (written <= 0) {
        fail_errno("write extracted source file", written < 0 ? errno : EIO);
      }
      offset += static_cast<std::size_t>(written);
    }
  }
}

bool symlink_target_stays_beneath(const safe_path& entry,
                                  const std::string& target)
{
  if (target.empty() || target.front() == '/') {
    return false;
  }
  std::vector<std::string> stack(entry.components.begin(),
                                 entry.components.end() - 1);
  std::size_t offset = 0;
  while (offset <= target.size()) {
    const std::size_t slash = target.find('/', offset);
    const std::size_t end = slash == std::string::npos ? target.size() : slash;
    const std::string component = target.substr(offset, end - offset);
    if (component.empty() || component == ".") {
      // No movement.
    } else if (component == "..") {
      if (stack.empty()) {
        return false;
      }
      stack.pop_back();
    } else {
      stack.push_back(component);
    }
    if (slash == std::string::npos) {
      break;
    }
    offset = slash + 1U;
  }
  return true;
}

void extract_directory(archive_entry* entry, int root, const safe_path& path,
                       mode_t mask, directory_map& directories)
{
  auto parent = open_parent(root, path, directories, mask);
  const auto& name = path.components.back();
  const auto found = directories.find(path.text);
  if (found == directories.end()) {
    require_absent(parent.get(), name);
    if (::mkdirat(parent.get(), name.c_str(), 0700) != 0) {
      fail_errno("create extracted source directory");
    }
    directories.emplace(path.text,
                        directory_metadata{admitted_mode(archive_entry_perm(entry), mask),
                                           entry_mtime(entry), true});
    return;
  }
  if (found->second.explicit_entry) {
    fail("archive contains a duplicate directory entry");
  }
  found->second.mode = admitted_mode(archive_entry_perm(entry), mask);
  found->second.modification = entry_mtime(entry);
  found->second.explicit_entry = true;
}

void extract_regular(archive* input, archive_entry* entry, int root,
                     const safe_path& path, mode_t mask,
                     directory_map& directories)
{
  auto parent = open_parent(root, path, directories, mask);
  const auto& name = path.components.back();
  require_absent(parent.get(), name);
  unique_fd output(::openat(parent.get(), name.c_str(),
                            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                            0600));
  if (!output) {
    fail_errno("create extracted source file");
  }
  write_regular_data(input, output.get());
  if (::fchmod(output.get(), admitted_mode(archive_entry_perm(entry), mask)) != 0) {
    fail_errno("set extracted source file mode");
  }
  set_fd_mtime(output.get(), entry_mtime(entry));
  if (::fsync(output.get()) != 0) {
    fail_errno("synchronize extracted source file");
  }
}

void extract_symlink(archive_entry* entry, int root, const safe_path& path,
                     mode_t mask, directory_map& directories)
{
  const char* raw_target = archive_entry_symlink(entry);
  if (!raw_target) {
    fail("archive symbolic link has no target");
  }
  const std::string target(raw_target);
  if (!symlink_target_stays_beneath(path, target)) {
    fail("archive symbolic link escapes the source tree");
  }
  auto parent = open_parent(root, path, directories, mask);
  const auto& name = path.components.back();
  require_absent(parent.get(), name);
  if (::symlinkat(target.c_str(), parent.get(), name.c_str()) != 0) {
    fail_errno("create extracted source symbolic link");
  }
  const timespec times[2] = {{0, UTIME_OMIT}, entry_mtime(entry)};
  if (::utimensat(parent.get(), name.c_str(), times, AT_SYMLINK_NOFOLLOW) != 0) {
    fail_errno("set extracted source symbolic-link modification time");
  }
}

void extract_hardlink(archive_entry* entry, int root, const safe_path& path,
                      mode_t mask, directory_map& directories)
{
  const char* raw_target = archive_entry_hardlink(entry);
  if (!raw_target) {
    fail("archive hard link has no target");
  }
  const safe_path target = parse_archive_path(raw_target);
  auto source_parent = open_existing_parent(root, target);
  const auto& source_name = target.components.back();
  struct stat source_info {};
  if (::fstatat(source_parent.get(), source_name.c_str(), &source_info,
                AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(source_info.st_mode)) {
    fail("archive hard-link target is not an already extracted regular file");
  }
  auto destination_parent = open_parent(root, path, directories, mask);
  const auto& destination_name = path.components.back();
  require_absent(destination_parent.get(), destination_name);
  if (::linkat(source_parent.get(), source_name.c_str(), destination_parent.get(),
               destination_name.c_str(), 0) != 0) {
    fail_errno("create extracted source hard link");
  }
}

unique_fd open_path_directory(int root, const std::string& text)
{
  safe_path path = parse_archive_path(text.c_str());
  unique_fd current(::dup(root));
  if (!current) {
    fail_errno("duplicate source root descriptor");
  }
  for (const auto& component : path.components) {
    current = open_child_directory(current.get(), component);
  }
  return current;
}

void seal_directories(int root, const directory_map& directories)
{
  std::vector<std::pair<std::string, directory_metadata>> ordered(
      directories.begin(), directories.end());
  std::sort(ordered.begin(), ordered.end(), [](const auto& lhs, const auto& rhs) {
    const auto left_depth = std::count(lhs.first.begin(), lhs.first.end(), '/');
    const auto right_depth = std::count(rhs.first.begin(), rhs.first.end(), '/');
    return left_depth != right_depth ? left_depth > right_depth
                                     : lhs.first > rhs.first;
  });
  for (const auto& item : ordered) {
    auto directory = open_path_directory(root, item.first);
    if (::fchmod(directory.get(), item.second.mode) != 0) {
      fail_errno("set extracted source directory mode");
    }
    set_fd_mtime(directory.get(), item.second.modification);
    if (::fsync(directory.get()) != 0) {
      fail_errno("synchronize extracted source directory");
    }
  }
}

class libarchive_source_archive_backend final : public source_archive_backend {
public:
  void unpack(int source_fd, int destination_fd,
              mode_t file_creation_mask) const override
  {
    unique_fd archive_fd(::dup(source_fd));
    if (!archive_fd) {
      fail_errno("duplicate staged source archive");
    }
    if (::lseek(archive_fd.get(), 0, SEEK_SET) < 0) {
      fail_errno("rewind staged source archive");
    }

    archive* raw = archive_read_new();
    if (!raw) {
      fail("cannot allocate source archive reader");
    }
    std::unique_ptr<archive, decltype(&archive_read_free)> input(raw,
                                                                 archive_read_free);
    if (archive_read_support_filter_all(input.get()) != ARCHIVE_OK ||
        archive_read_support_format_all(input.get()) != ARCHIVE_OK) {
      fail("cannot enable libarchive source formats");
    }
    if (archive_read_open_fd(input.get(), archive_fd.get(), 65536) != ARCHIVE_OK) {
      const char* diagnostic = archive_error_string(input.get());
      fail(std::string("open source archive: ") +
           (diagnostic ? diagnostic : "libarchive failure"));
    }

    directory_map directories;
    bool saw_entry = false;
    for (;;) {
      archive_entry* entry = nullptr;
      const int status = archive_read_next_header(input.get(), &entry);
      if (status == ARCHIVE_EOF) {
        break;
      }
      if (status != ARCHIVE_OK) {
        const char* diagnostic = archive_error_string(input.get());
        fail(std::string("read source archive header: ") +
             (diagnostic ? diagnostic : "libarchive failure"));
      }
      saw_entry = true;
      if ((archive_format(input.get()) & ARCHIVE_FORMAT_BASE_MASK) ==
          ARCHIVE_FORMAT_RAW) {
        fail("declared source archive decoded as raw data");
      }

      const safe_path path = parse_archive_path(archive_entry_pathname(entry));
      if (archive_entry_hardlink(entry)) {
        extract_hardlink(entry, destination_fd, path, file_creation_mask,
                         directories);
        if (archive_read_data_skip(input.get()) != ARCHIVE_OK) {
          fail("cannot skip source hard-link payload");
        }
        continue;
      }

      switch (archive_entry_filetype(entry)) {
      case AE_IFDIR:
        extract_directory(entry, destination_fd, path, file_creation_mask,
                          directories);
        if (archive_read_data_skip(input.get()) != ARCHIVE_OK) {
          fail("cannot skip source directory payload");
        }
        break;
      case AE_IFREG:
        extract_regular(input.get(), entry, destination_fd, path,
                        file_creation_mask, directories);
        break;
      case AE_IFLNK:
        extract_symlink(entry, destination_fd, path, file_creation_mask,
                        directories);
        if (archive_read_data_skip(input.get()) != ARCHIVE_OK) {
          fail("cannot skip source symbolic-link payload");
        }
        break;
      default:
        fail("source archive contains an unsupported object type");
      }
    }
    if (!saw_entry) {
      fail("declared source archive contains no entries");
    }
    seal_directories(destination_fd, directories);
    if (archive_read_close(input.get()) != ARCHIVE_OK) {
      const char* diagnostic = archive_error_string(input.get());
      fail(std::string("close source archive: ") +
           (diagnostic ? diagnostic : "libarchive failure"));
    }
  }
};

} // namespace

std::unique_ptr<source_archive_backend> make_libarchive_source_archive_backend()
{
  return std::make_unique<libarchive_source_archive_backend>();
}

} // namespace pkgbuild_exec::detail
