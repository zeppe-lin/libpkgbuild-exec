// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../fixtures/backend.h"
#include "../fixtures/session.h"
#include "../support/filesystem.h"
#include "../support/test.h"

#include <iostream>

#include <sys/resource.h>

namespace {

using namespace pkgbuild_exec_test;


class nofile_limit_guard final {
public:
  explicit nofile_limit_guard(rlim_t value)
  {
    require(::getrlimit(RLIMIT_NOFILE, &original_) == 0,
            "cannot inspect fixture file-descriptor limit");
    require(original_.rlim_cur >= value && original_.rlim_max >= value,
            "fixture file-descriptor ceiling is too low");
    rlimit limited = original_;
    limited.rlim_cur = value;
    require(::setrlimit(RLIMIT_NOFILE, &limited) == 0,
            "cannot lower fixture file-descriptor limit");
    active_ = true;
  }

  ~nofile_limit_guard()
  {
    if (active_) {
      (void)::setrlimit(RLIMIT_NOFILE, &original_);
    }
  }

  nofile_limit_guard(const nofile_limit_guard&) = delete;
  nofile_limit_guard& operator=(const nofile_limit_guard&) = delete;

private:
  rlimit original_{};
  bool active_ = false;
};

void publication_is_non_replacing()
{
  fixture_owner owner("publication-conflict");
  const fs::path destination = owner.get().root / "artifact-one.tar";
  write_file(destination, "existing artifact\n", 0444);
  fixture_backend backend(backend_mode::succeed);
  const auto result = pkgbuild_exec::execute(owner.get().session(), backend);

  require(result.build().outcome() == pkgbuild::build_outcome::failed &&
              result.sealing_failure() ==
                  pkgbuild_exec::result_sealing_failure_kind::artifact_publication,
          "publication conflict has the wrong result-sealing classification");
  require(read_file(destination) == "existing artifact\n",
          "publication replaced existing authoritative bytes");
  require(!result.image_authority(),
          "publication conflict retained successful image authority");
}

void large_payload_is_descriptor_bounded()
{
  fixture_owner owner("large-payload");
  fixture_backend backend(backend_mode::large_payload);
  nofile_limit_guard limit(64);
  const auto result = pkgbuild_exec::execute(owner.get().session(), backend);

  require(result.build().outcome() == pkgbuild::build_outcome::succeeded &&
              result.build().payload() && result.build().artifact() &&
              result.image_authority(),
          "large payload exhausted descriptor authority during sealing");
  require(result.build().payload()->entries().size() > 256U,
          "large payload fixture did not exercise descriptor scaling");
}

void unsupported_output_object_fails_payload_inspection()
{
  fixture_owner owner("unsupported-payload");
  fixture_backend backend(backend_mode::unsupported_payload);
  const auto result = pkgbuild_exec::execute(owner.get().session(), backend);
  require(result.build().outcome() == pkgbuild::build_outcome::failed &&
              result.sealing_failure() ==
                  pkgbuild_exec::result_sealing_failure_kind::payload_inspection,
          "unsupported payload object has the wrong sealing classification");
  require(!fs::exists(owner.get().root / "artifact-one.tar"),
          "failed payload inspection published an artifact");
}

void empty_output_fails_payload_inspection()
{
  fixture_owner owner("empty-payload");
  fixture_backend backend(backend_mode::empty_payload);
  const auto result = pkgbuild_exec::execute(owner.get().session(), backend);
  require(result.build().outcome() == pkgbuild::build_outcome::failed &&
              result.sealing_failure() ==
                  pkgbuild_exec::result_sealing_failure_kind::payload_inspection,
          "empty package output was not refused as incomplete payload evidence");
  require(!result.build().payload() && !result.build().artifact() &&
              !result.image_authority(),
          "empty output refusal invented successful artifact evidence");
}

} // namespace

int main()
{
  try {
    publication_is_non_replacing();
    large_payload_is_descriptor_bounded();
    unsupported_output_object_fails_payload_inspection();
    empty_output_fails_payload_inspection();
  } catch (const std::exception& value) {
    std::cerr << value.what() << '\n';
    return 1;
  }
  return 0;
}
