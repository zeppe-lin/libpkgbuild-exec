// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../fixtures/backend.h"
#include "../fixtures/session.h"
#include "../support/filesystem.h"
#include "../support/test.h"

#include <algorithm>
#include <iostream>
#include <string_view>

namespace {

using namespace pkgbuild_exec_test;

const pkgbuild::payload_entry& entry(
    const pkgbuild::payload_manifest& payload,
    std::string_view path)
{
  const auto found = std::find_if(
      payload.entries().begin(), payload.entries().end(),
      [&](const pkgbuild::payload_entry& value) {
        return value.path().string() == path;
      });
  require(found != payload.entries().end(), "payload entry is absent");
  return *found;
}

void seals_complete_success()
{
  fixture_owner owner("success");
  fixture_backend backend(backend_mode::succeed);
  const auto result = pkgbuild_exec::execute(owner.get().session(), backend);

  require(result.execution().status() == pkgexec::execution_status::succeeded &&
              result.execution().established_guarantees() ==
                  result.execution().request().required_guarantees(),
          "successful execution evidence is not request-exact");
  require(result.build().outcome() == pkgbuild::build_outcome::succeeded &&
              result.build().payload() && result.build().artifact() &&
              result.image_authority(),
          "successful build evidence is incomplete");
  require(!result.sealing_failure() && result.diagnostic().empty(),
          "successful build retained failure evidence");

  const auto& payload = *result.build().payload();
  require(payload.entries().size() == 6U,
          "fixture payload entry cardinality changed");
  require(entry(payload, "usr").type() == pkgbuild::payload_entry_type::directory &&
              entry(payload, "usr/bin").type() ==
                  pkgbuild::payload_entry_type::directory,
          "directory payload entries were not retained");
  const auto& regular = entry(payload, "usr/bin/payload");
  require(regular.type() == pkgbuild::payload_entry_type::regular &&
              regular.size() == std::string_view("artifact bytes\n").size() &&
              regular.regular_content().has_value(),
          "regular payload evidence is incomplete");
  const auto& hardlink = entry(payload, "usr/bin/payload-hard");
  require(hardlink.type() == pkgbuild::payload_entry_type::hardlink &&
              hardlink.hardlink_target() &&
              hardlink.hardlink_target()->string() == "usr/bin/payload",
          "hard-link topology was not retained");
  const auto& symlink = entry(payload, "usr/bin/payload-link");
  require(symlink.type() == pkgbuild::payload_entry_type::symlink &&
              symlink.symlink_target() &&
              *symlink.symlink_target() == "payload",
          "symbolic-link target was not retained");
  require(entry(payload, "usr/bin/pipe").type() ==
              pkgbuild::payload_entry_type::fifo,
          "FIFO payload entry was not retained");

  const fs::path artifact = owner.get().root / "artifact-one.tar";
  require(fs::is_regular_file(artifact), "artifact was not published");
  require((fs::status(artifact).permissions() & fs::perms::owner_write) ==
              fs::perms::none,
          "published artifact is writable");
  require(result.image_authority()->build().identity() ==
              result.build().identity() &&
              result.image_authority()->image().receipt().entry_count() ==
                  payload.entries().size(),
          "independent image authority is not bound to the successful build");
}

void effect_paths_do_not_change_build_identity_or_artifact_bytes()
{
  fixture_owner owner("success-determinism");
  fixture_backend backend(backend_mode::succeed);
  const auto first = pkgbuild_exec::execute(owner.get().session("first"), backend);
  const auto second = pkgbuild_exec::execute(owner.get().session("second"), backend);

  require(first.build() == second.build(),
          "effect coordinates contaminated build-result identity");
  require(first.build().artifact()->complete_digest() ==
              second.build().artifact()->complete_digest(),
          "effect coordinates changed artifact authority");
  require(read_file(owner.get().root / "artifact-first.tar") ==
              read_file(owner.get().root / "artifact-second.tar"),
          "identical payloads produced different package_tar bytes");
}

} // namespace

int main()
{
  try {
    seals_complete_success();
    effect_paths_do_not_change_build_identity_or_artifact_bytes();
  } catch (const std::exception& value) {
    std::cerr << value.what() << '\n';
    return 1;
  }
  return 0;
}
