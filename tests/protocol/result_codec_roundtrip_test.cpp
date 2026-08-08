// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../fixtures/backend.h"
#include "../fixtures/session.h"
#include "../support/test.h"

#include <iostream>

namespace {

using namespace pkgbuild_exec_test;

void require_codec_equal(
    const pkgbuild_exec::build_execution_result& expected,
    const pkgbuild_exec::build_execution_result& observed)
{
  require(observed.execution().identity() == expected.execution().identity(),
          "codec changed execution evidence");
  require(observed.build().identity() == expected.build().identity(),
          "codec changed build evidence");
  require(observed.sealing_failure() == expected.sealing_failure(),
          "codec changed sealing failure evidence");
  require(observed.diagnostic() == expected.diagnostic(),
          "codec changed diagnostic evidence");
  require(observed.image_authority().has_value() ==
              expected.image_authority().has_value(),
          "codec changed image-authority presence");
  if (expected.image_authority()) {
    require(observed.image_authority()->identity() ==
                expected.image_authority()->identity(),
            "codec changed image authority");
  }
}

void roundtrip(const pkgbuild_exec::build_execution_result& result)
{
  const auto encoding = pkgbuild_exec::encode_build_execution_result(result);
  const auto decoded = pkgbuild_exec::decode_build_execution_result(
      encoding, result.build().request(), result.execution().request(),
      result.execution().backend());
  require_codec_equal(result, decoded);
  require(pkgbuild_exec::encode_build_execution_result(decoded) == encoding,
          "build-execution encoding is not canonical");
}

void qualifies_all_evidence_shapes()
{
  fixture_owner success_owner("codec-success");
  fixture_backend succeeding(backend_mode::succeed);
  const auto success = pkgbuild_exec::execute(
      success_owner.get().session(), succeeding);
  roundtrip(success);

  fixture_owner before_owner("codec-before-start");
  fixture_backend before(backend_mode::fail_before_start);
  roundtrip(pkgbuild_exec::execute(before_owner.get().session(), before));

  fixture_owner after_owner("codec-after-start");
  fixture_backend after(backend_mode::fail_after_start);
  roundtrip(pkgbuild_exec::execute(after_owner.get().session(), after));

  fixture_owner sealing_owner("codec-sealing");
  fixture_backend unsupported(backend_mode::unsupported_payload);
  roundtrip(pkgbuild_exec::execute(sealing_owner.get().session(), unsupported));
}

} // namespace

int main()
{
  try {
    qualifies_all_evidence_shapes();
  } catch (const std::exception& value) {
    std::cerr << value.what() << '\n';
    return 1;
  }
  return 0;
}
