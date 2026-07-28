// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include <libpkgbuild-exec/error.h>

#include <utility>

namespace pkgbuild_exec {

std::string_view to_string(error_code value) noexcept
{
  switch (value) {
    case error_code::invalid_session: return "invalid-session";
    case error_code::source_material_mismatch: return "source-material-mismatch";
    case error_code::package_input_mismatch: return "package-input-mismatch";
    case error_code::unsafe_path_layout: return "unsafe-path-layout";
    case error_code::resource_preparation_failed: return "resource-preparation-failed";
    case error_code::source_staging_failed: return "source-staging-failed";
    case error_code::identity_derivation_failed: return "identity-derivation-failed";
    case error_code::backend_contract_violation: return "backend-contract-violation";
    case error_code::payload_inspection_failed: return "payload-inspection-failed";
    case error_code::artifact_encoding_failed: return "artifact-encoding-failed";
    case error_code::artifact_publication_failed: return "artifact-publication-failed";
    case error_code::artifact_verification_failed: return "artifact-verification-failed";
    case error_code::artifact_cleanup_failed: return "artifact-cleanup-failed";
  }
  return "unknown";
}

error::error(error_code code, std::string message)
    : std::runtime_error(std::move(message)), code_(code)
{
}

error_code error::code() const noexcept { return code_; }


} // namespace pkgbuild_exec
