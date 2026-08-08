// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../support/filesystem.h"

#include <algorithm>
#include <string>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <libpkgexec/libpkgexec.h>

namespace pkgbuild_exec_test {

inline pkgexec::backend_capability_profile capabilities(char seed = 'a')
{
  return pkgexec::backend_capability_profile::seal(
      pkgexec::backend_identity::from_sha256(std::string(64U, seed)),
      {
          pkgexec::execution_guarantee::exact_interpreter,
          pkgexec::execution_guarantee::closed_environment,
          pkgexec::execution_guarantee::root_view,
          pkgexec::execution_guarantee::read_only_resources,
          pkgexec::execution_guarantee::writable_resources,
          pkgexec::execution_guarantee::fixed_credentials,
          pkgexec::execution_guarantee::network_denied,
          pkgexec::execution_guarantee::complete_stdout_capture,
          pkgexec::execution_guarantee::complete_stderr_capture,
          pkgexec::execution_guarantee::cleanup_verified,
      });
}

enum class backend_mode {
  succeed,
  fail_before_start,
  fail_after_start,
  unsupported_payload,
  empty_payload,
  wrong_request,
  wrong_backend,
  throw_exception,
  throw_nonstandard,
  capabilities_throw_exception,
  capabilities_throw_nonstandard,
};

class fixture_backend final : public pkgexec::execution_backend {
public:
  explicit fixture_backend(backend_mode mode) : mode_(mode) {}

  pkgexec::backend_capability_profile capabilities() const override
  {
    if (mode_ == backend_mode::capabilities_throw_exception) {
      throw std::runtime_error("fixture capability exception");
    }
    if (mode_ == backend_mode::capabilities_throw_nonstandard) {
      throw 17;
    }
    return ::pkgbuild_exec_test::capabilities();
  }

  pkgexec::execution_result execute(
      const pkgexec::execution_request& request,
      const pkgexec::execution_resources& resources) override
  {
    if (mode_ == backend_mode::throw_exception) {
      throw std::runtime_error("fixture backend exception");
    }
    if (mode_ == backend_mode::throw_nonstandard) {
      throw 23;
    }
    if (mode_ == backend_mode::fail_before_start) {
      return pkgexec::execution_result::failed_before_start(
          request, ::pkgbuild_exec_test::capabilities(),
          pkgexec::execution_failure_kind::backend_unsupported, {},
          "fixture execution failure");
    }
    if (mode_ == backend_mode::fail_after_start) {
      return pkgexec::execution_result::failed_after_start(
          request, ::pkgbuild_exec_test::capabilities(), request.interpreter(),
          pkgexec::process_termination::exited(7),
          pkgexec::stream_capture::retained("fixture stdout\n"),
          pkgexec::stream_capture::retained("fixture stderr\n"),
          request.required_guarantees(), pkgexec::cleanup_outcome::verified,
          pkgexec::execution_failure_kind::program_exited_nonzero,
          "fixture program failure");
    }

    if (mode_ != backend_mode::empty_payload) {
      emit_payload(output_path(request, resources),
                   mode_ == backend_mode::unsupported_payload);
    }

    auto evidence_request = request;
    if (mode_ == backend_mode::wrong_request) {
      evidence_request = pkgexec::execution_request::seal(
          request.program(), pkgexec::execution_purpose::check(),
          request.interpreter(), request.root_view(), request.resources(),
          request.environment(), request.credentials(), request.limits(),
          request.cancellation());
    }
    const auto evidence_backend =
        mode_ == backend_mode::wrong_backend
            ? ::pkgbuild_exec_test::capabilities('b')
            : ::pkgbuild_exec_test::capabilities();
    return pkgexec::execution_result::succeeded(
        std::move(evidence_request), evidence_backend, request.interpreter(),
        pkgexec::stream_capture::retained("fixture stdout\n"),
        pkgexec::stream_capture::retained("fixture stderr\n"),
        request.required_guarantees(), "fixture success");
  }

private:
  static fs::path output_path(
      const pkgexec::execution_request& request,
      const pkgexec::execution_resources& resources)
  {
    const auto output_slot = pkgexec::resource_slot::singleton(
        pkgexec::resource_role::package_output_root);
    const auto& binding = request.resources().binding(output_slot);
    return resources.materialization(binding.resource()).host_path();
  }

  static void emit_payload(const fs::path& root, bool unsupported)
  {
    fs::create_directories(root / "usr/bin");
    write_file(root / "usr/bin/payload", "artifact bytes\n", 0755);
    require(::link((root / "usr/bin/payload").c_str(),
                   (root / "usr/bin/payload-hard").c_str()) == 0,
            "cannot create fixture hard link");
    require(::symlink("payload",
                      (root / "usr/bin/payload-link").c_str()) == 0,
            "cannot create fixture symbolic link");
    require(::mkfifo((root / "usr/bin/pipe").c_str(), 0644) == 0,
            "cannot create fixture fifo");

    if (unsupported) {
      const int descriptor = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
      require(descriptor >= 0, "cannot create fixture socket");
      sockaddr_un address{};
      address.sun_family = AF_UNIX;
      const std::string path = (root / "usr/bin/socket").string();
      require(path.size() < sizeof(address.sun_path),
              "fixture socket path is too long");
      std::copy(path.begin(), path.end(), address.sun_path);
      require(::bind(descriptor, reinterpret_cast<sockaddr*>(&address),
                     sizeof(address)) == 0,
              "cannot bind fixture socket");
      require(::close(descriptor) == 0, "cannot close fixture socket");
    }

    set_mtime(root / "usr/bin/payload");
    set_mtime(root / "usr/bin/payload-hard");
    set_mtime(root / "usr/bin/payload-link", true);
    set_mtime(root / "usr/bin/pipe");
    if (unsupported) {
      set_mtime(root / "usr/bin/socket");
    }
    set_mtime(root / "usr/bin");
    set_mtime(root / "usr");
  }

  backend_mode mode_;
};

} // namespace pkgbuild_exec_test
