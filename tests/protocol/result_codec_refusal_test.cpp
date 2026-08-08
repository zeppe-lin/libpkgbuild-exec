// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../fixtures/backend.h"
#include "../fixtures/session.h"
#include "../support/test.h"

#include <iostream>
#include <string>

namespace {

using namespace pkgbuild_exec_test;

pkgbuild::build_request different_build_request(
    const fixture_owner::state& state)
{
  return build_request(state.resolution, 5);
}

void rejects_corrupt_bytes()
{
  fixture_owner owner("codec-corruption");
  fixture_backend backend(backend_mode::succeed);
  const auto result = pkgbuild_exec::execute(owner.get().session(), backend);
  const auto encoding = pkgbuild_exec::encode_build_execution_result(result);

  auto corrupted = encoding;
  corrupted[corrupted.size() / 2U] ^= 0x01U;
  expect_error(pkgbuild_exec::error_code::corrupt_encoding, [&] {
    (void)pkgbuild_exec::decode_build_execution_result(
        corrupted, result.build().request(), result.execution().request(),
        result.execution().backend());
  });

  auto truncated = encoding;
  truncated.pop_back();
  expect_error(pkgbuild_exec::error_code::corrupt_encoding, [&] {
    (void)pkgbuild_exec::decode_build_execution_result(
        truncated, result.build().request(), result.execution().request(),
        result.execution().backend());
  });

  auto extended = encoding;
  extended.push_back(0U);
  expect_error(pkgbuild_exec::error_code::corrupt_encoding, [&] {
    (void)pkgbuild_exec::decode_build_execution_result(
        extended, result.build().request(), result.execution().request(),
        result.execution().backend());
  });
}

void rejects_substituted_authority()
{
  fixture_owner owner("codec-authority");
  fixture_backend backend(backend_mode::succeed);
  const auto result = pkgbuild_exec::execute(owner.get().session(), backend);
  const auto encoding = pkgbuild_exec::encode_build_execution_result(result);

  expect_error(pkgbuild_exec::error_code::authority_mismatch, [&] {
    (void)pkgbuild_exec::decode_build_execution_result(
        encoding, different_build_request(owner.get()),
        result.execution().request(), result.execution().backend());
  });

  auto other_execution_request = pkgexec::execution_request::seal(
      result.execution().request().program(),
      pkgexec::execution_purpose::check(),
      result.execution().request().interpreter(),
      result.execution().request().root_view(),
      result.execution().request().resources(),
      result.execution().request().environment(),
      result.execution().request().credentials(),
      result.execution().request().limits(),
      result.execution().request().cancellation());
  expect_error(pkgbuild_exec::error_code::authority_mismatch, [&] {
    (void)pkgbuild_exec::decode_build_execution_result(
        encoding, result.build().request(), other_execution_request,
        result.execution().backend());
  });

  expect_error(pkgbuild_exec::error_code::authority_mismatch, [&] {
    (void)pkgbuild_exec::decode_build_execution_result(
        encoding, result.build().request(), result.execution().request(),
        capabilities('b'));
  });
}

} // namespace

int main()
{
  try {
    rejects_corrupt_bytes();
    rejects_substituted_authority();
  } catch (const std::exception& value) {
    std::cerr << value.what() << '\n';
    return 1;
  }
  return 0;
}
