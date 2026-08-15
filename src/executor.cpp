// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include <libpkgbuild-exec/executor.h>

#include <libpkgbuild-image/authority.h>

#include <libpkgbuild-exec/error.h>
#include <libpkgimage/libarchive_backend.h>
#include <libpkgsource-exec/libpkgsource-exec.h>

#include "result_identity.h"
#include "source_archive_backend.h"

#include <archive.h>
#include <archive_entry.h>
#include <openssl/evp.h>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace pkgbuild_exec {
namespace fs = std::filesystem;
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

std::string errno_message(std::string_view operation, int value)
{
  return std::string(operation) + ": " + std::strerror(value);
}

class sha256_state final {
public:
  explicit sha256_state(error_code code = error_code::source_staging_failed)
      : code_(code), context_(EVP_MD_CTX_new(), EVP_MD_CTX_free)
  {
    if (!context_ || EVP_DigestInit_ex(context_.get(), EVP_sha256(), nullptr) != 1) {
      throw error(code_, "cannot initialize SHA-256 state");
    }
  }

  void update(const void* data, std::size_t size)
  {
    if (EVP_DigestUpdate(context_.get(), data, size) != 1) {
      throw error(code_, "cannot update SHA-256 state");
    }
  }

  [[nodiscard]] std::string finish()
  {
    std::array<unsigned char, EVP_MAX_MD_SIZE> output{};
    unsigned int size = 0;
    if (EVP_DigestFinal_ex(context_.get(), output.data(), &size) != 1 ||
        size != 32U) {
      throw error(code_, "cannot finalize SHA-256 state");
    }
    static constexpr char hex[] = "0123456789abcdef";
    std::string result(64U, '0');
    for (std::size_t index = 0; index < 32U; ++index) {
      result[index * 2U] = hex[output[index] >> 4U];
      result[index * 2U + 1U] = hex[output[index] & 0x0fU];
    }
    return result;
  }

private:
  error_code code_;
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context_;
};

std::string sha256_bytes(std::string_view value)
{
  sha256_state state(error_code::identity_derivation_failed);
  state.update(value.data(), value.size());
  return state.finish();
}

std::string domain_hash(std::string_view domain, std::string_view material)
{
  std::string bytes(domain);
  bytes.push_back('\0');
  bytes.append(material);
  return sha256_bytes(bytes);
}

pkgexec::resource_identity resource_identity(
    std::string_view domain, std::string_view material)
{
  return pkgexec::resource_identity::from_sha256(
      domain_hash(domain, material));
}

pkgexec::resource_identity source_object_resource_identity(
    const pkgfetch::source_materialization& materialization)
{
  try {
    return pkgsource_exec::source_object_tree_identity(materialization);
  } catch (const pkgsource_exec::error& problem) {
    throw error(error_code::identity_derivation_failed,
                "cannot derive source-object resource identity: " +
                    std::string(problem.what()));
  }
}

pkgsource_exec::source_object_tree realize_source_object_resource(
    const pkgfetch::source_materialization& materialization,
    const fs::path& destination)
{
  try {
    return pkgsource_exec::realize_source_object_tree(materialization,
                                                       destination);
  } catch (const pkgsource_exec::error& problem) {
    throw error(error_code::source_staging_failed,
                "cannot realize source-object resource: " +
                    std::string(problem.what()));
  }
}

struct file_stamp final {
  dev_t device = 0;
  dev_t special_device = 0;
  ino_t inode = 0;
  mode_t mode = 0;
  uid_t user = 0;
  gid_t group = 0;
  nlink_t links = 0;
  off_t size = 0;
  timespec modification{};
  timespec status_change{};
};

file_stamp stamp_of(const struct stat& value)
{
  return {value.st_dev, value.st_rdev, value.st_ino, value.st_mode, value.st_uid,
          value.st_gid, value.st_nlink, value.st_size, value.st_mtim,
          value.st_ctim};
}

bool same_timespec(const timespec& first, const timespec& second) noexcept
{
  return first.tv_sec == second.tv_sec && first.tv_nsec == second.tv_nsec;
}

bool operator==(const file_stamp& first, const file_stamp& second) noexcept
{
  return first.device == second.device &&
         first.special_device == second.special_device &&
         first.inode == second.inode &&
         first.mode == second.mode && first.user == second.user &&
         first.group == second.group && first.links == second.links &&
         first.size == second.size &&
         same_timespec(first.modification, second.modification) &&
         same_timespec(first.status_change, second.status_change);
}

file_stamp fstat_stamp(int descriptor, error_code code,
                       std::string_view operation)
{
  struct stat value {};
  if (::fstat(descriptor, &value) != 0) {
    throw error(code, errno_message(operation, errno));
  }
  return stamp_of(value);
}

file_stamp fstatat_stamp(int directory, const char* name, int flags,
                         error_code code, std::string_view operation)
{
  struct stat value {};
  if (::fstatat(directory, name, &value, flags) != 0) {
    throw error(code, errno_message(operation, errno));
  }
  return stamp_of(value);
}

void write_all(int descriptor, const void* data, std::size_t size,
               error_code code, std::string_view operation)
{
  const auto* bytes = static_cast<const unsigned char*>(data);
  std::size_t offset = 0;
  while (offset < size) {
    const ssize_t count = ::write(descriptor, bytes + offset, size - offset);
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      throw error(code, errno_message(operation, count < 0 ? errno : EIO));
    }
    offset += static_cast<std::size_t>(count);
  }
}

void make_tree_removable(const fs::path& path)
{
  std::error_code ec;
  if (!fs::exists(path, ec)) {
    return;
  }
  if (ec) {
    throw error(error_code::unsafe_path_layout,
                "cannot inspect existing session path: " + ec.message());
  }
  const auto status = fs::symlink_status(path, ec);
  if (ec) {
    throw error(error_code::unsafe_path_layout,
                "cannot inspect existing session path: " + ec.message());
  }
  if (fs::is_directory(status)) {
    fs::permissions(path, fs::perms::owner_all,
                    fs::perm_options::add, ec);
    if (ec) {
      throw error(error_code::unsafe_path_layout,
                  "cannot make existing session root removable: " +
                      ec.message());
    }
    for (fs::recursive_directory_iterator iterator(
             path, fs::directory_options::none, ec), end;
         iterator != end; iterator.increment(ec)) {
      if (ec) {
        throw error(error_code::unsafe_path_layout,
                    "cannot enumerate existing session path: " + ec.message());
      }
      const auto child_status = iterator->symlink_status(ec);
      if (ec) {
        throw error(error_code::unsafe_path_layout,
                    "cannot inspect existing session entry: " + ec.message());
      }
      if (fs::is_directory(child_status)) {
        fs::permissions(iterator->path(), fs::perms::owner_all,
                        fs::perm_options::add, ec);
        if (ec) {
          throw error(error_code::unsafe_path_layout,
                      "cannot make existing session directory removable: " +
                          ec.message());
        }
      }
    }
  }
}

void reset_directory(const fs::path& path, mode_t mode)
{
  make_tree_removable(path);
  std::error_code ec;
  fs::remove_all(path, ec);
  if (ec) {
    throw error(error_code::unsafe_path_layout,
                "cannot clear session directory " + path.string() + ": " +
                    ec.message());
  }
  fs::create_directories(path, ec);
  if (ec) {
    throw error(error_code::unsafe_path_layout,
                "cannot create session directory " + path.string() + ": " +
                    ec.message());
  }
  if (::chmod(path.c_str(), mode) != 0) {
    throw error(error_code::unsafe_path_layout,
                errno_message("chmod session directory", errno));
  }
}

void prepare_writable_directory(const fs::path& path, mode_t mode,
                                std::uint64_t user, std::uint64_t group)
{
  reset_directory(path, mode);
  unique_fd directory(::open(path.c_str(),
                              O_RDONLY | O_DIRECTORY | O_CLOEXEC |
                                  O_NOFOLLOW));
  if (!directory) {
    throw error(error_code::resource_preparation_failed,
                errno_message("open writable resource directory", errno));
  }
  if (::fchown(directory.get(), static_cast<uid_t>(user),
               static_cast<gid_t>(group)) != 0 ||
      ::fchmod(directory.get(), mode) != 0 || ::fsync(directory.get()) != 0) {
    throw error(error_code::resource_preparation_failed,
                errno_message("prepare writable resource directory", errno));
  }
}

void prepare_writable_child(const fs::path& path, mode_t mode,
                            std::uint64_t user, std::uint64_t group)
{
  if (::mkdir(path.c_str(), mode) != 0) {
    throw error(error_code::resource_preparation_failed,
                errno_message("create writable resource child", errno));
  }
  unique_fd directory(::open(path.c_str(),
                              O_RDONLY | O_DIRECTORY | O_CLOEXEC |
                                  O_NOFOLLOW));
  if (!directory ||
      ::fchown(directory.get(), static_cast<uid_t>(user),
               static_cast<gid_t>(group)) != 0 ||
      ::fchmod(directory.get(), mode) != 0 || ::fsync(directory.get()) != 0) {
    throw error(error_code::resource_preparation_failed,
                errno_message("prepare writable resource child", errno));
  }
}

std::string package_input_name(
    const pkgbuild::build_input& input)
{
  return input.package().name();
}

std::string join_input_names(const pkgbuild::build_request& request,
                             pkgbuild::input_scope scope)
{
  std::string value;
  for (const auto& input : request.inputs().for_scope(scope)) {
    if (!value.empty()) {
      value.push_back(':');
    }
    value += package_input_name(input);
  }
  return value;
}

pkgexec::environment_policy execution_environment(
    const pkgbuild::build_request& request)
{
  const auto& policy = request.policy().environment();
  std::vector<pkgexec::environment_variable> variables;
  variables.emplace_back("PKG_SOURCE_ROOT", "/build/source");
  variables.emplace_back("PKG_BUILD_ROOT", "/build/work");
  variables.emplace_back("PKG_DESTDIR", "/build/package");
  variables.emplace_back("PKG_BUILD_INPUT_ROOT", "/build/inputs/build");
  variables.emplace_back("PKG_CHECK_INPUT_ROOT", "/build/inputs/check");
  variables.emplace_back("PKG_BUILD_INPUTS",
                         join_input_names(request, pkgbuild::input_scope::build));
  variables.emplace_back("PKG_CHECK_INPUTS",
                         join_input_names(request, pkgbuild::input_scope::check));
  variables.emplace_back("PKG_BUILD_ARCH",
                         request.architectures().build().name());
  variables.emplace_back("PKG_TARGET_ARCH",
                         request.architectures().target().name());
  variables.emplace_back("PKG_JOBS", std::to_string(policy.parallelism()));

  return pkgexec::environment_policy::hermetic(
      {pkgexec::logical_path::parse("/usr/bin"),
       pkgexec::logical_path::parse("/bin")},
      pkgexec::logical_path::parse("/build/work/home"),
      pkgexec::logical_path::parse("/tmp"), policy.parallelism(),
      policy.file_creation_mask(), policy.source_date_epoch(),
      pkgexec::network_policy::denied, pkgexec::stdin_policy::null_device,
      pkgexec::stream_policy::capture_complete,
      pkgexec::stream_policy::capture_complete, std::move(variables));
}

struct inode_key final {
  dev_t device = 0;
  ino_t inode = 0;
  friend bool operator<(const inode_key& first,
                        const inode_key& second) noexcept
  {
    return first.device < second.device ||
           (first.device == second.device && first.inode < second.inode);
  }
};

pkgbuild::payload_time payload_time(const file_stamp& value)
{
  // package_tar uses the restricted pax profile and therefore seals
  // modification times at whole-second precision.
  return {static_cast<std::int64_t>(value.modification.tv_sec), 0U};
}

struct retained_regular final {
  pkgbuild::payload_path path;
  file_stamp stamp;
  std::uint64_t size = 0;
  std::string digest;
};

struct inspected_payload final {
  pkgbuild::payload_manifest manifest;
  std::vector<retained_regular> regulars;
  file_stamp root_stamp;
};

std::vector<std::string> directory_names(int descriptor)
{
  const int duplicate = ::dup(descriptor);
  if (duplicate < 0) {
    throw error(error_code::payload_inspection_failed,
                errno_message("duplicate package directory", errno));
  }
  DIR* raw = ::fdopendir(duplicate);
  if (!raw) {
    const int saved = errno;
    (void)::close(duplicate);
    throw error(error_code::payload_inspection_failed,
                errno_message("open package directory stream", saved));
  }
  struct directory_closer final {
    void operator()(DIR* value) const noexcept { (void)::closedir(value); }
  };
  std::unique_ptr<DIR, directory_closer> directory(raw);
  std::vector<std::string> result;
  errno = 0;
  while (dirent* entry = ::readdir(directory.get())) {
    const std::string name(entry->d_name);
    if (name != "." && name != "..") {
      result.push_back(name);
    }
    errno = 0;
  }
  if (errno != 0) {
    throw error(error_code::payload_inspection_failed,
                errno_message("read package directory", errno));
  }
  std::sort(result.begin(), result.end());
  return result;
}

std::string read_symlink(int directory, const std::string& name)
{
  std::vector<char> buffer(256U);
  for (;;) {
    const ssize_t count = ::readlinkat(directory, name.c_str(), buffer.data(),
                                       buffer.size());
    if (count < 0) {
      throw error(error_code::payload_inspection_failed,
                  errno_message("read package symbolic link", errno));
    }
    if (static_cast<std::size_t>(count) < buffer.size()) {
      return std::string(buffer.data(), static_cast<std::size_t>(count));
    }
    if (buffer.size() >= 1024U * 1024U) {
      throw error(error_code::payload_inspection_failed,
                  "package symbolic-link target exceeds supported bound");
    }
    buffer.resize(buffer.size() * 2U);
  }
}

std::pair<std::string, std::uint64_t> hash_regular(
    int descriptor, const file_stamp& before,
    error_code code = error_code::payload_inspection_failed)
{
  if (::lseek(descriptor, 0, SEEK_SET) < 0) {
    throw error(code, errno_message("rewind regular file", errno));
  }
  sha256_state digest(code);
  std::uint64_t size = 0;
  std::array<unsigned char, 65536> buffer{};
  for (;;) {
    const ssize_t count = ::read(descriptor, buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count < 0) {
      throw error(code, errno_message("read regular file", errno));
    }
    if (count == 0) {
      break;
    }
    digest.update(buffer.data(), static_cast<std::size_t>(count));
    size += static_cast<std::uint64_t>(count);
  }
  const file_stamp after = fstat_stamp(descriptor, code,
                                       "reinspect regular file");
  if (!(before == after) || size != static_cast<std::uint64_t>(before.size)) {
    throw error(code, "regular file changed during inspection");
  }
  return {digest.finish(), size};
}

void inspect_directory(
    int directory, const std::string& prefix,
    std::vector<pkgbuild::payload_entry>& entries,
    std::vector<retained_regular>& regulars,
    std::map<inode_key, pkgbuild::payload_path>& hardlink_targets)
{
  const file_stamp directory_before = fstat_stamp(
      directory, error_code::payload_inspection_failed,
      "inspect package directory");
  if (!S_ISDIR(directory_before.mode)) {
    throw error(error_code::payload_inspection_failed,
                "package traversal descriptor is not a directory");
  }

  for (const auto& name : directory_names(directory)) {
    const file_stamp before = fstatat_stamp(
        directory, name.c_str(), AT_SYMLINK_NOFOLLOW,
        error_code::payload_inspection_failed, "inspect package entry");
    const std::string relative =
        prefix.empty() ? name : prefix + "/" + name;
    auto path = pkgbuild::payload_path::parse(relative);
    const std::uint32_t mode =
        static_cast<std::uint32_t>(before.mode & 07777U);
    const std::uint64_t user = static_cast<std::uint64_t>(before.user);
    const std::uint64_t group = static_cast<std::uint64_t>(before.group);
    const auto modification = payload_time(before);

    if (S_ISDIR(before.mode)) {
      unique_fd child(::openat(directory, name.c_str(),
                               O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
      if (!child) {
        throw error(error_code::payload_inspection_failed,
                    errno_message("open package directory", errno));
      }
      if (!(before == fstat_stamp(child.get(),
                                  error_code::payload_inspection_failed,
                                  "reinspect opened package directory"))) {
        throw error(error_code::payload_inspection_failed,
                    "package directory changed before traversal");
      }
      entries.push_back(pkgbuild::payload_entry::directory(
          path, mode, user, group, modification));
      inspect_directory(child.get(), relative, entries, regulars,
                        hardlink_targets);
      if (!(before == fstat_stamp(child.get(),
                                  error_code::payload_inspection_failed,
                                  "reinspect traversed package directory"))) {
        throw error(error_code::payload_inspection_failed,
                    "package directory changed during traversal");
      }
      continue;
    }

    if (S_ISREG(before.mode)) {
      unique_fd file(::openat(directory, name.c_str(),
                              O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
      if (!file) {
        throw error(error_code::payload_inspection_failed,
                    errno_message("open package regular file", errno));
      }
      if (!(before == fstat_stamp(file.get(),
                                  error_code::payload_inspection_failed,
                                  "reinspect opened package regular file"))) {
        throw error(error_code::payload_inspection_failed,
                    "package regular file changed before inspection");
      }
      const inode_key key{before.device, before.inode};
      if (before.links > 1U) {
        const auto found = hardlink_targets.find(key);
        if (found != hardlink_targets.end()) {
          const file_stamp path_after = fstatat_stamp(
              directory, name.c_str(), AT_SYMLINK_NOFOLLOW,
              error_code::payload_inspection_failed,
              "reinspect package hard-link path");
          if (!(before == path_after)) {
            throw error(error_code::payload_inspection_failed,
                        "package hard-link path changed during inspection");
          }
          entries.push_back(pkgbuild::payload_entry::hardlink(
              std::move(path), mode, user, group, modification,
              found->second));
          continue;
        }
        hardlink_targets.emplace(key, path);
      }
      auto observed = hash_regular(file.get(), before);
      const file_stamp path_after = fstatat_stamp(
          directory, name.c_str(), AT_SYMLINK_NOFOLLOW,
          error_code::payload_inspection_failed,
          "reinspect package regular-file path");
      if (!(before == path_after)) {
        throw error(error_code::payload_inspection_failed,
                    "package regular-file path changed during inspection");
      }
      entries.push_back(pkgbuild::payload_entry::regular(
          path, mode, user, group, observed.second, modification,
          pkgbuild::sha256_digest(observed.first)));
      regulars.push_back({std::move(path), before, observed.second,
                          std::move(observed.first)});
      continue;
    }

    if (S_ISLNK(before.mode)) {
      const std::string target = read_symlink(directory, name);
      const file_stamp after = fstatat_stamp(
          directory, name.c_str(), AT_SYMLINK_NOFOLLOW,
          error_code::payload_inspection_failed,
          "reinspect package symbolic link");
      if (!(before == after)) {
        throw error(error_code::payload_inspection_failed,
                    "package symbolic link changed during inspection");
      }
      entries.push_back(pkgbuild::payload_entry::symlink(
          std::move(path), mode, user, group, modification, target));
      continue;
    }

    const file_stamp after = fstatat_stamp(
        directory, name.c_str(), AT_SYMLINK_NOFOLLOW,
        error_code::payload_inspection_failed, "reinspect package special file");
    if (!(before == after)) {
      throw error(error_code::payload_inspection_failed,
                  "package special file changed during inspection");
    }
    if (S_ISFIFO(before.mode)) {
      entries.push_back(pkgbuild::payload_entry::fifo(
          std::move(path), mode, user, group, modification));
    } else if (S_ISCHR(before.mode)) {
      entries.push_back(pkgbuild::payload_entry::character_device(
          std::move(path), mode, user, group, modification,
          {static_cast<std::uint64_t>(major(before.special_device)),
           static_cast<std::uint64_t>(minor(before.special_device))}));
    } else if (S_ISBLK(before.mode)) {
      entries.push_back(pkgbuild::payload_entry::block_device(
          std::move(path), mode, user, group, modification,
          {static_cast<std::uint64_t>(major(before.special_device)),
           static_cast<std::uint64_t>(minor(before.special_device))}));
    } else {
      throw error(error_code::payload_inspection_failed,
                  "package output contains an unsupported object: " +
                      relative);
    }
  }

  if (!(directory_before == fstat_stamp(
                               directory,
                               error_code::payload_inspection_failed,
                               "reinspect package directory"))) {
    throw error(error_code::payload_inspection_failed,
                "package directory changed during enumeration");
  }
}

inspected_payload inspect_payload(const fs::path& root)
{
  unique_fd directory(::open(root.c_str(),
                              O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (!directory) {
    throw error(error_code::payload_inspection_failed,
                errno_message("open package output root", errno));
  }
  std::vector<pkgbuild::payload_entry> entries;
  std::vector<retained_regular> regulars;
  std::map<inode_key, pkgbuild::payload_path> hardlinks;
  inspect_directory(directory.get(), {}, entries, regulars, hardlinks);
  if (entries.empty()) {
    throw error(error_code::payload_inspection_failed,
                "package output root contains no explicit payload entries");
  }
  auto manifest = pkgbuild::payload_manifest::seal(std::move(entries));
  const auto root_stamp = fstat_stamp(
      directory.get(), error_code::payload_inspection_failed,
      "retain package output root");
  return {std::move(manifest), std::move(regulars), root_stamp};
}

const retained_regular& regular_for(
    const inspected_payload& payload, const pkgbuild::payload_path& path)
{
  const auto found = std::find_if(
      payload.regulars.begin(), payload.regulars.end(),
      [&](const retained_regular& value) { return value.path == path; });
  if (found == payload.regulars.end()) {
    throw error(error_code::artifact_encoding_failed,
                "payload manifest lost retained regular-file material");
  }
  return *found;
}

void archive_require(int result, archive* value, std::string_view operation)
{
  if (result != ARCHIVE_OK) {
    const char* diagnostic = archive_error_string(value);
    throw error(error_code::artifact_encoding_failed,
                std::string(operation) + ": " +
                    (diagnostic ? diagnostic : "libarchive failure"));
  }
}

struct archive_fd_client final { int descriptor = -1; };

la_ssize_t archive_write_callback(archive* value, void* client,
                                  const void* data, std::size_t size)
{
  auto& output = *static_cast<archive_fd_client*>(client);
  const auto* bytes = static_cast<const unsigned char*>(data);
  std::size_t offset = 0;
  while (offset < size) {
    const ssize_t count =
        ::write(output.descriptor, bytes + offset, size - offset);
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      archive_set_error(value, count < 0 ? errno : EIO,
                        "write artifact bytes");
      return -1;
    }
    offset += static_cast<std::size_t>(count);
  }
  return static_cast<la_ssize_t>(size);
}

int archive_close_callback(archive*, void*) { return ARCHIVE_OK; }

class temporary_artifact final {
public:
  explicit temporary_artifact(const fs::path& destination)
  {
    std::error_code ec;
    fs::create_directories(destination.parent_path(), ec);
    if (ec) {
      throw error(error_code::artifact_publication_failed,
                  "cannot create artifact parent: " + ec.message());
    }
    std::string pattern =
        (destination.parent_path() /
         ("." + destination.filename().string() + ".tmp.XXXXXX"))
            .string();
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    const int descriptor = ::mkstemp(writable.data());
    if (descriptor < 0) {
      throw error(error_code::artifact_publication_failed,
                  errno_message("create temporary artifact", errno));
    }
    descriptor_.reset(descriptor);
    path_ = writable.data();
    if (::fcntl(descriptor_.get(), F_SETFD, FD_CLOEXEC) != 0) {
      const int saved = errno;
      descriptor_.reset();
      (void)::unlink(path_.c_str());
      throw error(error_code::artifact_publication_failed,
                  errno_message("seal temporary artifact descriptor", saved));
    }
  }

  ~temporary_artifact()
  {
    if (!path_.empty()) {
      (void)::unlink(path_.c_str());
    }
    if (!published_.empty() && owns_publication_) {
      struct stat descriptor_info {};
      struct stat path_info {};
      if (::fstat(descriptor_.get(), &descriptor_info) == 0 &&
          ::lstat(published_.c_str(), &path_info) == 0 &&
          descriptor_info.st_dev == path_info.st_dev &&
          descriptor_info.st_ino == path_info.st_ino) {
        (void)::unlink(published_.c_str());
      }
    }
  }

  temporary_artifact(const temporary_artifact&) = delete;
  temporary_artifact& operator=(const temporary_artifact&) = delete;

  [[nodiscard]] int descriptor() const noexcept { return descriptor_.get(); }
  [[nodiscard]] const fs::path& path() const noexcept { return path_; }

  void publish(
      const fs::path& destination,
      const std::pair<std::string, std::uint64_t>& expected)
  {
    if (::link(path_.c_str(), destination.c_str()) != 0) {
      if (errno == EEXIST) {
        adopt_exact_existing(destination, expected);
        return;
      }
      throw error(error_code::artifact_publication_failed,
                  errno_message("publish artifact without replacement", errno));
    }

    struct stat temporary_info {};
    if (::fstat(descriptor_.get(), &temporary_info) != 0) {
      const int saved = errno;
      (void)::unlink(destination.c_str());
      throw error(error_code::artifact_publication_failed,
                  errno_message("inspect temporary artifact publication", saved));
    }
    struct stat published_info {};
    if (::lstat(destination.c_str(), &published_info) != 0) {
      const int saved = errno;
      (void)::unlink(destination.c_str());
      throw error(error_code::artifact_publication_failed,
                  errno_message("inspect published artifact", saved));
    }
    if (temporary_info.st_dev != published_info.st_dev ||
        temporary_info.st_ino != published_info.st_ino ||
        !S_ISREG(published_info.st_mode)) {
      (void)::unlink(destination.c_str());
      throw error(error_code::artifact_publication_failed,
                  errno_message("verify published artifact", ESTALE));
    }

    unique_fd parent(::open(destination.parent_path().c_str(),
                            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!parent || ::fsync(parent.get()) != 0) {
      const int saved = errno;
      (void)::unlink(destination.c_str());
      throw error(error_code::artifact_publication_failed,
                  errno_message("synchronize artifact publication", saved));
    }
    if (::unlink(path_.c_str()) != 0) {
      const int saved = errno;
      (void)::unlink(destination.c_str());
      throw error(error_code::artifact_publication_failed,
                  errno_message("remove temporary artifact name", saved));
    }
    if (::fsync(parent.get()) != 0) {
      const int saved = errno;
      (void)::unlink(destination.c_str());
      (void)::fsync(parent.get());
      throw error(error_code::artifact_publication_failed,
                  errno_message("synchronize final artifact name", saved));
    }
    path_.clear();
    published_ = destination;
    owns_publication_ = true;
  }

  void verify_published_binding() const
  {
    if (published_.empty()) {
      throw error(error_code::artifact_verification_failed,
                  "artifact has not been published for verification");
    }
    const file_stamp descriptor = fstat_stamp(
        descriptor_.get(), error_code::artifact_verification_failed,
        "reinspect published artifact descriptor");
    struct stat path_info {};
    if (::lstat(published_.c_str(), &path_info) != 0) {
      throw error(error_code::artifact_verification_failed,
                  errno_message("reinspect published artifact path", errno));
    }
    const file_stamp path = stamp_of(path_info);
    if (!(descriptor == path) || !S_ISREG(path.mode) ||
        (path.mode & 0222U) != 0U) {
      throw error(error_code::artifact_verification_failed,
                  "published artifact path no longer names retained bytes");
    }
  }

  void rollback()
  {
    if (published_.empty()) {
      return;
    }
    if (!owns_publication_) {
      published_.clear();
      return;
    }
    struct stat descriptor_info {};
    struct stat path_info {};
    if (::fstat(descriptor_.get(), &descriptor_info) != 0 ||
        ::lstat(published_.c_str(), &path_info) != 0 ||
        descriptor_info.st_dev != path_info.st_dev ||
        descriptor_info.st_ino != path_info.st_ino) {
      throw error(error_code::artifact_cleanup_failed,
                  "cannot prove ownership of the published artifact name");
    }
    unique_fd parent(::open(published_.parent_path().c_str(),
                            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!parent) {
      throw error(error_code::artifact_cleanup_failed,
                  errno_message("open artifact parent for rollback", errno));
    }
    if (::unlink(published_.c_str()) != 0 || ::fsync(parent.get()) != 0) {
      throw error(error_code::artifact_cleanup_failed,
                  errno_message("rollback published artifact", errno));
    }
    published_.clear();
  }

  void retain() noexcept
  {
    published_.clear();
    owns_publication_ = false;
  }

private:
  void adopt_exact_existing(
      const fs::path& destination,
      const std::pair<std::string, std::uint64_t>& expected)
  {
    unique_fd existing(::open(
        destination.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (!existing) {
      throw error(
          error_code::artifact_publication_failed,
          errno_message("open existing artifact publication", errno));
    }
    const file_stamp before = fstat_stamp(
        existing.get(), error_code::artifact_publication_failed,
        "inspect existing artifact publication");
    if (!S_ISREG(before.mode) || (before.mode & 0222U) != 0U) {
      throw error(
          error_code::artifact_publication_failed,
          "existing artifact publication is not a read-only regular file");
    }
    const auto observed = hash_regular(
        existing.get(), before, error_code::artifact_publication_failed);
    if (observed != expected) {
      throw error(
          error_code::artifact_publication_failed,
          "existing artifact publication differs from freshly sealed bytes");
    }

    struct stat path_info {};
    if (::lstat(destination.c_str(), &path_info) != 0) {
      throw error(
          error_code::artifact_publication_failed,
          errno_message("reinspect existing artifact publication", errno));
    }
    const file_stamp path_stamp = stamp_of(path_info);
    if (!(before == path_stamp)) {
      throw error(
          error_code::artifact_publication_failed,
          "existing artifact publication changed during exact verification");
    }

    unique_fd parent(::open(
        destination.parent_path().c_str(),
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!parent) {
      throw error(
          error_code::artifact_cleanup_failed,
          errno_message("open artifact parent after exact publication", errno));
    }
    if (::unlink(path_.c_str()) != 0) {
      throw error(
          error_code::artifact_cleanup_failed,
          errno_message("remove redundant temporary artifact", errno));
    }
    path_.clear();
    if (::fsync(parent.get()) != 0) {
      throw error(
          error_code::artifact_cleanup_failed,
          errno_message("synchronize redundant artifact cleanup", errno));
    }

    descriptor_ = std::move(existing);
    published_ = destination;
    owns_publication_ = false;
  }

  unique_fd descriptor_;
  fs::path path_;
  fs::path published_;
  bool owns_publication_ = false;
};

std::optional<unique_fd> open_exact_read_only_artifact(
    const fs::path& path,
    const std::pair<std::string, std::uint64_t>& expected,
    bool absent_is_empty)
{
  unique_fd file(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
  if (!file) {
    if (absent_is_empty && errno == ENOENT) {
      return std::nullopt;
    }
    throw error(
        error_code::artifact_publication_failed,
        errno_message("open retained artifact", errno));
  }

  const file_stamp before = fstat_stamp(
      file.get(), error_code::artifact_publication_failed,
      "inspect retained artifact");
  if (!S_ISREG(before.mode) || (before.mode & 0222U) != 0U) {
    throw error(
        error_code::artifact_publication_failed,
        "retained artifact is not a read-only regular file");
  }
  const auto observed = hash_regular(
      file.get(), before, error_code::artifact_publication_failed);
  if (observed != expected) {
    throw error(
        error_code::artifact_publication_failed,
        "retained artifact differs from durable terminal evidence");
  }

  struct stat path_info {};
  if (::lstat(path.c_str(), &path_info) != 0) {
    throw error(
        error_code::artifact_publication_failed,
        errno_message("reinspect retained artifact path", errno));
  }
  if (!(before == stamp_of(path_info))) {
    throw error(
        error_code::artifact_publication_failed,
        "retained artifact path changed during exact verification");
  }
  return file;
}

void remove_private_sealed_artifact(const fs::path& path)
{
  struct stat value {};
  if (::lstat(path.c_str(), &value) != 0) {
    if (errno == ENOENT) {
      return;
    }
    throw error(
        error_code::artifact_cleanup_failed,
        errno_message("inspect private sealed artifact", errno));
  }
  if (!S_ISREG(value.st_mode)) {
    throw error(
        error_code::artifact_cleanup_failed,
        "private sealed artifact is no longer a regular file");
  }

  unique_fd parent(::open(
      path.parent_path().c_str(),
      O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (!parent) {
    throw error(
        error_code::artifact_cleanup_failed,
        errno_message("open private artifact parent", errno));
  }
  if (::unlink(path.c_str()) != 0 || ::fsync(parent.get()) != 0) {
    throw error(
        error_code::artifact_cleanup_failed,
        errno_message("remove private sealed artifact", errno));
  }
}

void copy_exact_artifact(
    int source,
    temporary_artifact& destination,
    const std::pair<std::string, std::uint64_t>& expected)
{
  if (::lseek(source, 0, SEEK_SET) < 0) {
    throw error(
        error_code::artifact_publication_failed,
        errno_message("rewind private sealed artifact", errno));
  }

  std::array<unsigned char, 65536> buffer{};
  for (;;) {
    const ssize_t count = ::read(source, buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count < 0) {
      throw error(
          error_code::artifact_publication_failed,
          errno_message("read private sealed artifact", errno));
    }
    if (count == 0) {
      break;
    }
    write_all(
        destination.descriptor(), buffer.data(),
        static_cast<std::size_t>(count),
        error_code::artifact_publication_failed,
        "copy private sealed artifact");
  }

  if (::fchmod(destination.descriptor(), 0444) != 0 ||
      ::fsync(destination.descriptor()) != 0) {
    throw error(
        error_code::artifact_publication_failed,
        errno_message("seal public artifact candidate", errno));
  }
  const file_stamp stamp = fstat_stamp(
      destination.descriptor(), error_code::artifact_publication_failed,
      "inspect public artifact candidate");
  if (!S_ISREG(stamp.mode) || (stamp.mode & 0222U) != 0U ||
      hash_regular(
          destination.descriptor(), stamp,
          error_code::artifact_publication_failed) != expected) {
    throw error(
        error_code::artifact_publication_failed,
        "public artifact candidate differs from retained sealed bytes");
  }
}

unique_fd open_regular_beneath(
    int root, const pkgbuild::payload_path& path)
{
  const int duplicate = ::fcntl(root, F_DUPFD_CLOEXEC, 3);
  if (duplicate < 0) {
    throw error(error_code::artifact_encoding_failed,
                errno_message("duplicate package output root", errno));
  }
  unique_fd current(duplicate);
  const std::string& text = path.string();
  std::size_t begin = 0;
  for (;;) {
    const auto slash = text.find('/', begin);
    const bool last = slash == std::string::npos;
    const std::string component =
        text.substr(begin, last ? std::string::npos : slash - begin);
    const int flags = O_RDONLY | O_CLOEXEC | O_NOFOLLOW |
                      (last ? 0 : O_DIRECTORY);
    unique_fd next(::openat(current.get(), component.c_str(), flags));
    if (!next) {
      throw error(error_code::artifact_encoding_failed,
                  errno_message("reopen package payload", errno));
    }
    current = std::move(next);
    if (last) {
      return current;
    }
    begin = slash + 1U;
  }
}

void write_regular_payload(archive* output, int root,
                           const retained_regular& regular)
{
  auto descriptor = open_regular_beneath(root, regular.path);
  const auto before = fstat_stamp(
      descriptor.get(), error_code::artifact_encoding_failed,
      "reinspect reopened payload");
  if (!(before == regular.stamp) || !S_ISREG(before.mode)) {
    throw error(error_code::artifact_encoding_failed,
                "package regular file changed before archive encoding");
  }
  sha256_state digest(error_code::artifact_encoding_failed);
  std::uint64_t size = 0;
  std::array<unsigned char, 65536> buffer{};
  for (;;) {
    const ssize_t count =
        ::read(descriptor.get(), buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count < 0) {
      throw error(error_code::artifact_encoding_failed,
                  errno_message("read retained payload", errno));
    }
    if (count == 0) {
      break;
    }
    digest.update(buffer.data(), static_cast<std::size_t>(count));
    std::size_t offset = 0;
    while (offset < static_cast<std::size_t>(count)) {
      const la_ssize_t written = archive_write_data(
          output, buffer.data() + offset,
          static_cast<std::size_t>(count) - offset);
      if (written <= 0) {
        const char* diagnostic = archive_error_string(output);
        throw error(error_code::artifact_encoding_failed,
                    std::string("write artifact payload: ") +
                        (diagnostic ? diagnostic : "libarchive failure"));
      }
      offset += static_cast<std::size_t>(written);
    }
    size += static_cast<std::uint64_t>(count);
  }
  const file_stamp after = fstat_stamp(
      descriptor.get(), error_code::artifact_encoding_failed,
      "reinspect encoded payload");
  if (!(regular.stamp == after) || size != regular.size ||
      digest.finish() != regular.digest) {
    throw error(error_code::artifact_encoding_failed,
                "package regular file changed during archive encoding");
  }
}

void encode_artifact(const inspected_payload& payload,
                     const fs::path& package_root,
                     temporary_artifact& temporary)
{
  unique_fd root(::open(package_root.c_str(),
                        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
  if (!root) {
    throw error(error_code::artifact_encoding_failed,
                errno_message("reopen package output root", errno));
  }
  if (!(payload.root_stamp == fstat_stamp(
                                root.get(), error_code::artifact_encoding_failed,
                                "reinspect package output root"))) {
    throw error(error_code::artifact_encoding_failed,
                "package output root changed before archive encoding");
  }

  if (::ftruncate(temporary.descriptor(), 0) != 0 ||
      ::lseek(temporary.descriptor(), 0, SEEK_SET) < 0) {
    throw error(error_code::artifact_encoding_failed,
                errno_message("initialize temporary artifact", errno));
  }

  archive* raw = archive_write_new();
  if (!raw) {
    throw error(error_code::artifact_encoding_failed,
                "cannot allocate package_tar writer");
  }
  std::unique_ptr<archive, decltype(&archive_write_free)> output(
      raw, archive_write_free);
  archive_require(archive_write_add_filter_none(output.get()), output.get(),
                  "select uncompressed artifact");
  archive_require(archive_write_set_format_pax_restricted(output.get()),
                  output.get(), "select package_tar format");
  archive_require(archive_write_set_bytes_per_block(output.get(), 512),
                  output.get(), "set artifact block size");
  archive_require(archive_write_set_bytes_in_last_block(output.get(), 512),
                  output.get(), "set artifact final block size");

  archive_fd_client client{temporary.descriptor()};
  archive_require(archive_write_open2(output.get(), &client, nullptr,
                                      archive_write_callback,
                                      archive_close_callback, nullptr),
                  output.get(), "open package_tar writer");

  for (const auto& item : payload.manifest.entries()) {
    archive_entry* raw_entry = archive_entry_new();
    if (!raw_entry) {
      throw error(error_code::artifact_encoding_failed,
                  "cannot allocate package_tar entry");
    }
    std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(
        raw_entry, archive_entry_free);
    archive_entry_set_pathname(entry.get(), item.path().string().c_str());
    archive_entry_set_perm(entry.get(), static_cast<mode_t>(item.mode()));
    archive_entry_set_uid(entry.get(), static_cast<la_int64_t>(item.uid()));
    archive_entry_set_gid(entry.get(), static_cast<la_int64_t>(item.gid()));
    archive_entry_set_mtime(entry.get(), item.modification_time().seconds,
                            item.modification_time().nanoseconds);

    switch (item.type()) {
      case pkgbuild::payload_entry_type::regular:
        archive_entry_set_filetype(entry.get(), AE_IFREG);
        archive_entry_set_size(entry.get(),
                               static_cast<la_int64_t>(item.size()));
        break;
      case pkgbuild::payload_entry_type::directory:
        archive_entry_set_filetype(entry.get(), AE_IFDIR);
        archive_entry_set_size(entry.get(), 0);
        break;
      case pkgbuild::payload_entry_type::symlink:
        archive_entry_set_filetype(entry.get(), AE_IFLNK);
        archive_entry_set_size(entry.get(), 0);
        archive_entry_set_symlink(entry.get(),
                                  item.symlink_target()->c_str());
        break;
      case pkgbuild::payload_entry_type::hardlink:
        archive_entry_set_filetype(entry.get(), AE_IFREG);
        archive_entry_set_size(entry.get(), 0);
        archive_entry_set_hardlink(
            entry.get(), item.hardlink_target()->string().c_str());
        break;
      case pkgbuild::payload_entry_type::fifo:
        archive_entry_set_filetype(entry.get(), AE_IFIFO);
        archive_entry_set_size(entry.get(), 0);
        break;
      case pkgbuild::payload_entry_type::character_device:
        archive_entry_set_filetype(entry.get(), AE_IFCHR);
        archive_entry_set_size(entry.get(), 0);
        archive_entry_set_rdevmajor(entry.get(), item.device()->major);
        archive_entry_set_rdevminor(entry.get(), item.device()->minor);
        break;
      case pkgbuild::payload_entry_type::block_device:
        archive_entry_set_filetype(entry.get(), AE_IFBLK);
        archive_entry_set_size(entry.get(), 0);
        archive_entry_set_rdevmajor(entry.get(), item.device()->major);
        archive_entry_set_rdevminor(entry.get(), item.device()->minor);
        break;
    }

    archive_require(archive_write_header(output.get(), entry.get()),
                    output.get(), "write package_tar header");
    if (item.type() == pkgbuild::payload_entry_type::regular) {
      write_regular_payload(output.get(), root.get(),
                            regular_for(payload, item.path()));
    }
    archive_require(archive_write_finish_entry(output.get()), output.get(),
                    "finish package_tar entry");
  }

  archive_require(archive_write_close(output.get()), output.get(),
                  "close package_tar writer");
  if (!(payload.root_stamp == fstat_stamp(
                                root.get(), error_code::artifact_encoding_failed,
                                "reinspect encoded package output root"))) {
    throw error(error_code::artifact_encoding_failed,
                "package output root changed during archive encoding");
  }
  if (::fchmod(temporary.descriptor(), 0444) != 0 ||
      ::fsync(temporary.descriptor()) != 0) {
    throw error(error_code::artifact_encoding_failed,
                errno_message("seal temporary artifact", errno));
  }
}

std::pair<std::string, std::uint64_t> hash_artifact(int descriptor)
{
  const file_stamp before = fstat_stamp(
      descriptor, error_code::artifact_verification_failed,
      "inspect temporary artifact");
  if (!S_ISREG(before.mode) || (before.mode & 0222U) != 0U) {
    throw error(error_code::artifact_verification_failed,
                "temporary artifact is not a read-only regular file");
  }
  auto observed = hash_regular(
      descriptor, before, error_code::artifact_verification_failed);
  return observed;
}

pkgimage::complete_archive_digest image_digest(std::string_view hex)
{
  if (hex.size() != 64U) {
    throw error(error_code::artifact_verification_failed,
                "artifact SHA-256 has invalid length");
  }
  pkgimage::sha256_digest_bytes bytes{};
  auto nibble = [](char value) -> unsigned char {
    if (value >= '0' && value <= '9') {
      return static_cast<unsigned char>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
      return static_cast<unsigned char>(value - 'a' + 10);
    }
    throw error(error_code::artifact_verification_failed,
                "artifact SHA-256 is not lowercase hexadecimal");
  };
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    bytes[index] = static_cast<unsigned char>(
        (nibble(hex[index * 2U]) << 4U) |
        nibble(hex[index * 2U + 1U]));
  }
  return pkgimage::complete_archive_digest::from_sha256(bytes);
}

std::optional<result_sealing_failure_kind> failure_kind(error_code code)
{
  switch (code) {
    case error_code::payload_inspection_failed:
      return result_sealing_failure_kind::payload_inspection;
    case error_code::artifact_encoding_failed:
      return result_sealing_failure_kind::artifact_encoding;
    case error_code::artifact_publication_failed:
      return result_sealing_failure_kind::artifact_publication;
    case error_code::artifact_verification_failed:
      return result_sealing_failure_kind::artifact_verification;
    case error_code::artifact_cleanup_failed:
      return result_sealing_failure_kind::artifact_cleanup;
    default:
      return std::nullopt;
  }
}

const package_input_resource& supplied_resource(
    const admitted_build_session& session,
    const pkgbuild::build_input& expected)
{
  const auto found = std::find_if(
      session.package_inputs().begin(), session.package_inputs().end(),
      [&](const package_input_resource& supplied) {
        return supplied.input == expected.identity();
      });
  if (found == session.package_inputs().end()) {
    throw error(error_code::package_input_mismatch,
                "package-input resource vanished after session admission");
  }
  return *found;
}

void require_unique_execution_resource_identities(
    const std::vector<pkgexec::resource_binding>& bindings)
{
  std::set<pkgexec::resource_identity> resources;
  for (const auto& binding : bindings) {
    if (!resources.insert(binding.resource()).second) {
      throw error(
          error_code::package_input_mismatch,
          "a package-input resource aliases another execution resource");
    }
  }
}

pkgexec::backend_capability_profile backend_capabilities(
    pkgexec::execution_backend& backend)
{
  try {
    return backend.capabilities();
  } catch (const std::exception& value) {
    throw error(
        error_code::backend_contract_violation,
        std::string("execution backend could not report capabilities: ") +
            value.what());
  } catch (...) {
    throw error(error_code::backend_contract_violation,
                "execution backend threw non-standard capability evidence");
  }
}

pkgexec::execution_result invoke_backend(
    pkgexec::execution_backend& backend,
    const prepared_execution& prepared)
{
  try {
    return backend.execute(prepared.request, prepared.resources);
  } catch (const std::exception& value) {
    throw error(
        error_code::backend_contract_violation,
        std::string("execution backend threw instead of returning evidence: ") +
            value.what());
  } catch (...) {
    throw error(error_code::backend_contract_violation,
                "execution backend threw non-standard execution evidence");
  }
}

} // namespace

namespace detail {

class executor_access final {
public:
  static build_execution_result make(
      pkgexec::execution_result execution,
      pkgbuild::build_result build,
      std::optional<result_sealing_failure_kind> sealing_failure,
      std::string diagnostic,
      std::optional<pkgbuild::image_adapter::build_image_authority>
          image_authority)
  {
    return build_execution_result(
        std::move(execution), std::move(build), sealing_failure,
        std::move(diagnostic), std::move(image_authority));
  }
};

} // namespace detail

prepared_paths project_prepared_paths(
    const admitted_build_session& session)
{
  return {
      session.paths().session_root / "source",
      session.paths().session_root / "work",
      session.paths().session_root / "tmp",
  };
}

fs::path project_sealed_artifact_path(
    const admitted_build_session& session)
{
  return session.paths().session_root / "sealed-artifact.tar";
}

pkgexec::execution_request seal_execution_request(
    const admitted_build_session& session)
{
  std::vector<pkgexec::resource_binding> bindings;

  const auto source_slot = pkgexec::resource_slot::named(
      pkgexec::resource_role::source_tree, "sources");
  const auto source_identity =
      source_object_resource_identity(session.sources());
  bindings.emplace_back(source_slot, source_identity,
                        pkgexec::resource_access::read_only,
                        pkgexec::logical_path::parse("/build/source"));

  for (const auto& expected : session.request().inputs().inputs()) {
    const auto& supplied = supplied_resource(session, expected);
    const auto role =
        expected.scope() == pkgbuild::input_scope::build
            ? pkgexec::resource_role::build_input_tree
            : pkgexec::resource_role::check_input_tree;
    const std::string name = package_input_name(expected);
    const auto slot = pkgexec::resource_slot::named(role, name);
    const std::string mount =
        std::string("/build/inputs/") +
        (role == pkgexec::resource_role::build_input_tree ? "build/" :
                                                            "check/") +
        name;
    bindings.emplace_back(slot, supplied.resource,
                          pkgexec::resource_access::read_only,
                          pkgexec::logical_path::parse(mount));
  }

  const auto workspace_slot = pkgexec::resource_slot::singleton(
      pkgexec::resource_role::build_workspace);
  const auto workspace_identity = resource_identity(
      "libpkgbuild-exec:workspace:v1", session.request().identity().hex());
  bindings.emplace_back(workspace_slot, workspace_identity,
                        pkgexec::resource_access::writable,
                        pkgexec::logical_path::parse("/build/work"));

  const auto output_slot = pkgexec::resource_slot::singleton(
      pkgexec::resource_role::package_output_root);
  const auto output_identity = resource_identity(
      "libpkgbuild-exec:package-output-root:v1",
      session.request().identity().hex());
  bindings.emplace_back(output_slot, output_identity,
                        pkgexec::resource_access::writable,
                        pkgexec::logical_path::parse("/build/package"));

  const auto temporary_slot = pkgexec::resource_slot::singleton(
      pkgexec::resource_role::private_temporary_root);
  const auto temporary_identity = resource_identity(
      "libpkgbuild-exec:private-temporary-root:v1",
      session.request().identity().hex());
  bindings.emplace_back(temporary_slot, temporary_identity,
                        pkgexec::resource_access::writable,
                        pkgexec::logical_path::parse("/tmp"));

  require_unique_execution_resource_identities(bindings);
  auto layout =
      pkgexec::resource_layout::seal(std::move(bindings), workspace_slot);
  return pkgexec::execution_request::seal(
      session.request().build_program(), pkgexec::execution_purpose::build(),
      session.identity().interpreter, session.paths().root_view,
      std::move(layout), execution_environment(session.request()),
      pkgexec::credential_policy::fixed(
          session.identity().user_id, session.identity().group_id,
          session.identity().supplementary_groups, true),
      pkgexec::resource_limits::make(),
      pkgexec::cancellation_policy::disabled());
}

prepared_execution prepare(const admitted_build_session& session)
{
  const auto paths = project_prepared_paths(session);
  auto request = seal_execution_request(session);

  reset_directory(session.paths().session_root, 0700);
  const auto source_tree = realize_source_object_resource(session.sources(), paths.source_tree);
  prepare_writable_directory(
      paths.workspace, 0700, session.identity().user_id,
      session.identity().group_id);
  prepare_writable_directory(
      paths.temporary_root, 0700, session.identity().user_id,
      session.identity().group_id);
  prepare_writable_directory(
      session.paths().package_output_root, 0755,
      session.identity().user_id, session.identity().group_id);
  prepare_writable_child(
      paths.workspace / "home", 0700, session.identity().user_id,
      session.identity().group_id);

  unique_fd source_directory(::open(source_tree.path.c_str(),
                                     O_RDONLY | O_DIRECTORY | O_CLOEXEC |
                                         O_NOFOLLOW));
  if (!source_directory) {
    throw error(error_code::source_staging_failed,
                errno_message("open realized source-object tree", errno));
  }
  unique_fd workspace_directory(::open(paths.workspace.c_str(),
                                        O_RDONLY | O_DIRECTORY | O_CLOEXEC |
                                            O_NOFOLLOW));
  if (!workspace_directory) {
    throw error(error_code::source_staging_failed,
                errno_message("open build workspace for source realization",
                              errno));
  }
  auto archive_backend = detail::make_libarchive_source_archive_backend();
  for (const auto& object : session.sources().objects()) {
    if (object.declaration().unpack_kind() !=
        pkgsource::source_unpack_kind::archive) {
      continue;
    }
    unique_fd staged(::openat(source_directory.get(),
                              object.declaration().local_name().c_str(),
                              O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (!staged) {
      throw error(error_code::source_staging_failed,
                  errno_message("open staged source archive", errno));
    }
    archive_backend->unpack(
        staged.get(), workspace_directory.get(),
        static_cast<mode_t>(session.request().policy().environment()
                                .file_creation_mask()));
  }
  if (::fsync(workspace_directory.get()) != 0) {
    throw error(error_code::source_staging_failed,
                errno_message("synchronize realized source workspace", errno));
  }
  std::vector<pkgexec::resource_materialization> materializations;

  const auto source_slot = pkgexec::resource_slot::named(
      pkgexec::resource_role::source_tree, "sources");
  materializations.emplace_back(
      request.resources().binding(source_slot).resource(), paths.source_tree);

  for (const auto& expected : session.request().inputs().inputs()) {
    const auto& supplied = supplied_resource(session, expected);
    materializations.emplace_back(supplied.resource, supplied.path);
  }

  const auto workspace_slot = pkgexec::resource_slot::singleton(
      pkgexec::resource_role::build_workspace);
  materializations.emplace_back(
      request.resources().binding(workspace_slot).resource(), paths.workspace);

  const auto output_slot = pkgexec::resource_slot::singleton(
      pkgexec::resource_role::package_output_root);
  materializations.emplace_back(
      request.resources().binding(output_slot).resource(),
      session.paths().package_output_root);

  const auto temporary_slot = pkgexec::resource_slot::singleton(
      pkgexec::resource_role::private_temporary_root);
  materializations.emplace_back(
      request.resources().binding(temporary_slot).resource(),
      paths.temporary_root);

  auto resources = pkgexec::execution_resources::admit(
      request, session.paths().root_view, session.paths().root_view_path,
      std::move(materializations));
  return {std::move(request), std::move(resources), paths.source_tree,
          paths.workspace, paths.temporary_root};
}

build_execution_result execute_sealed(
    const admitted_build_session& session,
    pkgexec::execution_backend& backend)
{
  const auto advertised_backend = backend_capabilities(backend);
  auto prepared = prepare(session);
  auto execution = invoke_backend(backend, prepared);
  if (execution.request() != prepared.request) {
    throw error(error_code::backend_contract_violation,
                "execution backend returned evidence for another request");
  }
  if (execution.backend() != advertised_backend) {
    throw error(
        error_code::backend_contract_violation,
        "execution backend returned evidence for another backend profile");
  }

  const auto evidence = detail::execution_evidence_identity(execution);
  if (execution.status() != pkgexec::execution_status::succeeded) {
    auto build = pkgbuild::build_result::failed(
        session.request(), evidence, detail::execution_failure_identity(execution));
    const std::string diagnostic = execution.diagnostic();
    return detail::executor_access::make(
        std::move(execution), std::move(build), std::nullopt,
        diagnostic, std::nullopt);
  }

  try {
    auto payload = inspect_payload(session.paths().package_output_root);
    const auto sealed_path = project_sealed_artifact_path(session);
    temporary_artifact temporary(sealed_path);
    encode_artifact(payload, session.paths().package_output_root, temporary);
    auto after_encoding = inspect_payload(session.paths().package_output_root);
    if (!(payload.root_stamp == after_encoding.root_stamp) ||
        payload.manifest != after_encoding.manifest) {
      throw error(error_code::artifact_encoding_failed,
                  "package payload changed during archive encoding");
    }
    const auto artifact_digest = hash_artifact(temporary.descriptor());

    temporary.publish(sealed_path, artifact_digest);

    try {
      pkgimage::libarchive_backend image_backend;
      const auto before = fstat_stamp(
          temporary.descriptor(), error_code::artifact_verification_failed,
          "inspect sealed artifact before image verification");
      auto inspected = image_backend.inspect(
          pkgimage::archive_inspection_request{
              sealed_path,
              image_digest(artifact_digest.first)});
      const auto after = fstat_stamp(
          temporary.descriptor(), error_code::artifact_verification_failed,
          "inspect sealed artifact after image verification");
      if (!(before == after)) {
        throw error(error_code::artifact_verification_failed,
                    "artifact bytes changed during independent inspection");
      }
      temporary.verify_published_binding();
      auto artifact = pkgbuild::sealed_artifact::make(
          pkgbuild::artifact_encoding::package_tar,
          session.compression(), artifact_digest.second,
          pkgbuild::sha256_digest(artifact_digest.first));
      auto build = pkgbuild::build_result::succeeded(
          session.request(), payload.manifest, std::move(artifact), evidence);
      auto authority =
          pkgbuild::image_adapter::build_image_authority::admit(
              build, inspected);
      temporary.retain();
      return detail::executor_access::make(
          std::move(execution), std::move(build), std::nullopt, {},
          std::move(authority));
    } catch (...) {
      temporary.rollback();
      throw;
    }
  } catch (const error& value) {
    const auto kind = failure_kind(value.code());
    if (!kind) {
      throw;
    }
    auto build = pkgbuild::build_result::failed(
        session.request(), evidence,
        detail::sealing_failure_identity(execution, *kind));
    return detail::executor_access::make(
        std::move(execution), std::move(build), *kind, value.what(),
        std::nullopt);
  } catch (const std::exception& value) {
    constexpr auto kind =
        result_sealing_failure_kind::artifact_verification;
    auto build = pkgbuild::build_result::failed(
        session.request(), evidence, detail::sealing_failure_identity(execution, kind));
    return detail::executor_access::make(
        std::move(execution), std::move(build), kind, value.what(),
        std::nullopt);
  }
}

void publish_sealed_artifact(
    const admitted_build_session& session,
    const build_execution_result& result)
{
  if (result.build().outcome() != pkgbuild::build_outcome::succeeded ||
      !result.build().artifact() || !result.image_authority() ||
      result.sealing_failure()) {
    throw error(
        error_code::artifact_publication_failed,
        "only a successful sealed build result can publish an artifact");
  }
  if (result.build().request().identity() != session.request().identity() ||
      result.execution().request() != seal_execution_request(session)) {
    throw error(
        error_code::artifact_publication_failed,
        "sealed artifact result belongs to another admitted build session");
  }

  const auto& artifact = *result.build().artifact();
  const std::pair<std::string, std::uint64_t> expected{
      artifact.complete_digest().hex(), artifact.byte_count()};
  const auto private_path = project_sealed_artifact_path(session);
  const auto public_path = session.paths().artifact_path;

  if (auto published = open_exact_read_only_artifact(
          public_path, expected, true)) {
    remove_private_sealed_artifact(private_path);
    return;
  }

  auto retained = open_exact_read_only_artifact(
      private_path, expected, false);
  if (!retained) {
    throw error(
        error_code::artifact_publication_failed,
        "durable terminal evidence lacks its private sealed artifact");
  }

  temporary_artifact publication(public_path);
  copy_exact_artifact(retained->get(), publication, expected);
  publication.publish(public_path, expected);
  publication.verify_published_binding();
  publication.retain();
  remove_private_sealed_artifact(private_path);
}

build_execution_result execute(
    const admitted_build_session& session,
    pkgexec::execution_backend& backend)
{
  auto result = execute_sealed(session, backend);
  if (result.build().outcome() != pkgbuild::build_outcome::succeeded) {
    return result;
  }

  try {
    publish_sealed_artifact(session, result);
    return result;
  } catch (const error& value) {
    const auto kind = failure_kind(value.code());
    if (!kind) {
      throw;
    }
    const auto evidence = detail::execution_evidence_identity(result.execution());
    auto build = pkgbuild::build_result::failed(
        session.request(), evidence,
        detail::sealing_failure_identity(result.execution(), *kind));
    return detail::executor_access::make(
        result.execution(), std::move(build), *kind, value.what(),
        std::nullopt);
  }
}

} // namespace pkgbuild_exec
