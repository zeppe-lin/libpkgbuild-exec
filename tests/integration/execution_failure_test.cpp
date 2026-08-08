// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../fixtures/backend.h"
#include "../fixtures/session.h"
#include "../support/test.h"

#include <iostream>
#include <string_view>

namespace {

using namespace pkgbuild_exec_test;

void require_failed_build(
    const pkgbuild_exec::build_execution_result& result,
    pkgexec::execution_start_state start,
    std::string_view diagnostic)
{
  require(result.execution().status() == pkgexec::execution_status::failed &&
              result.execution().start_state() == start,
          "execution failure stage was lost");
  require(result.build().outcome() == pkgbuild::build_outcome::failed &&
              result.build().failure_evidence().has_value() &&
              !result.build().payload() && !result.build().artifact(),
          "execution failure did not seal an evidence-only failed build");
  require(!result.sealing_failure() && !result.image_authority(),
          "execution failure was misclassified as post-execution sealing");
  require(result.diagnostic() == diagnostic,
          "execution diagnostic was not retained exactly");
}

void maps_pre_start_failure()
{
  fixture_owner owner("failure-before-start");
  fixture_backend backend(backend_mode::fail_before_start);
  const auto result = pkgbuild_exec::execute(owner.get().session(), backend);
  require_failed_build(result, pkgexec::execution_start_state::not_started,
                       "fixture execution failure");
}

void maps_started_program_failure()
{
  fixture_owner owner("failure-after-start");
  fixture_backend backend(backend_mode::fail_after_start);
  const auto result = pkgbuild_exec::execute(owner.get().session(), backend);
  require_failed_build(result, pkgexec::execution_start_state::started,
                       "fixture program failure");
  require(result.execution().termination() &&
              result.execution().termination()->kind() ==
                  pkgexec::process_termination_kind::exited &&
              result.execution().termination()->value() == 7U,
          "started failure termination evidence was lost");
  require(result.execution().standard_output() &&
              result.execution().standard_output()->material() &&
              *result.execution().standard_output()->material() ==
                  "fixture stdout\n",
          "started failure stdout evidence was lost");
}

} // namespace

int main()
{
  try {
    maps_pre_start_failure();
    maps_started_program_failure();
  } catch (const std::exception& value) {
    std::cerr << value.what() << '\n';
    return 1;
  }
  return 0;
}
