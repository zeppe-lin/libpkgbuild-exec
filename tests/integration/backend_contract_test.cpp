// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../fixtures/backend.h"
#include "../fixtures/session.h"
#include "../support/test.h"

#include <iostream>

namespace {

using namespace pkgbuild_exec_test;

void rejects_capability_exceptions_before_effects()
{
  fixture_owner owner("backend-capabilities");
  for (const auto mode : {
           backend_mode::capabilities_throw_exception,
           backend_mode::capabilities_throw_nonstandard,
       }) {
    const auto session = owner.get().session(
        mode == backend_mode::capabilities_throw_exception ? "std" : "other");
    fixture_backend backend(mode);
    expect_error(pkgbuild_exec::error_code::backend_contract_violation, [&] {
      (void)pkgbuild_exec::execute(session, backend);
    });
    require(!fs::exists(session.paths().session_root) &&
                !fs::exists(session.paths().package_output_root),
            "backend capability refusal happened after build preparation");
  }
}

void translates_backend_execution_exceptions()
{
  fixture_owner owner("backend-throws");
  for (const auto mode : {
           backend_mode::throw_exception,
           backend_mode::throw_nonstandard,
       }) {
    fixture_backend backend(mode);
    expect_error(pkgbuild_exec::error_code::backend_contract_violation, [&] {
      (void)pkgbuild_exec::execute(
          owner.get().session(
              mode == backend_mode::throw_exception ? "std" : "other"),
          backend);
    });
  }
}

void rejects_foreign_request_evidence()
{
  fixture_owner owner("backend-request");
  fixture_backend backend(backend_mode::wrong_request);
  expect_error(pkgbuild_exec::error_code::backend_contract_violation, [&] {
    (void)pkgbuild_exec::execute(owner.get().session(), backend);
  });
}

void rejects_foreign_backend_profile_evidence()
{
  fixture_owner owner("backend-profile");
  fixture_backend backend(backend_mode::wrong_backend);
  expect_error(pkgbuild_exec::error_code::backend_contract_violation, [&] {
    (void)pkgbuild_exec::execute(owner.get().session(), backend);
  });
}

} // namespace

int main()
{
  try {
    rejects_capability_exceptions_before_effects();
    translates_backend_execution_exceptions();
    rejects_foreign_request_evidence();
    rejects_foreign_backend_profile_evidence();
  } catch (const std::exception& value) {
    std::cerr << value.what() << '\n';
    return 1;
  }
  return 0;
}
