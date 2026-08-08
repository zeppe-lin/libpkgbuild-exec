// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../support/test.h"

#include <array>
#include <iostream>
#include <string_view>
#include <utility>

#include <libpkgbuild-exec/libpkgbuild-exec.h>

int main()
{
  using namespace pkgbuild_exec;
  using pkgbuild_exec_test::require;

  try {
    const std::array<std::pair<error_code, std::string_view>, 16> errors{{
        {error_code::invalid_session, "invalid-session"},
        {error_code::source_material_mismatch, "source-material-mismatch"},
        {error_code::package_input_mismatch, "package-input-mismatch"},
        {error_code::unsafe_path_layout, "unsafe-path-layout"},
        {error_code::resource_preparation_failed,
         "resource-preparation-failed"},
        {error_code::source_staging_failed, "source-staging-failed"},
        {error_code::identity_derivation_failed,
         "identity-derivation-failed"},
        {error_code::backend_contract_violation,
         "backend-contract-violation"},
        {error_code::payload_inspection_failed,
         "payload-inspection-failed"},
        {error_code::artifact_encoding_failed, "artifact-encoding-failed"},
        {error_code::artifact_publication_failed,
         "artifact-publication-failed"},
        {error_code::artifact_verification_failed,
         "artifact-verification-failed"},
        {error_code::artifact_cleanup_failed, "artifact-cleanup-failed"},
        {error_code::inconsistent_result, "inconsistent-result"},
        {error_code::corrupt_encoding, "corrupt-encoding"},
        {error_code::authority_mismatch, "authority-mismatch"},
    }};
    for (const auto& [value, expected] : errors) {
      require(to_string(value) == expected, "error string mapping drifted");
    }

    const std::array<
        std::pair<result_sealing_failure_kind, std::string_view>, 5>
        failures{{
            {result_sealing_failure_kind::payload_inspection,
             "payload-inspection"},
            {result_sealing_failure_kind::artifact_encoding,
             "artifact-encoding"},
            {result_sealing_failure_kind::artifact_publication,
             "artifact-publication"},
            {result_sealing_failure_kind::artifact_verification,
             "artifact-verification"},
            {result_sealing_failure_kind::artifact_cleanup,
             "artifact-cleanup"},
        }};
    for (const auto& [value, expected] : failures) {
      require(to_string(value) == expected,
              "result-sealing failure string mapping drifted");
    }
  } catch (const std::exception& value) {
    std::cerr << value.what() << '\n';
    return 1;
  }
  return 0;
}
