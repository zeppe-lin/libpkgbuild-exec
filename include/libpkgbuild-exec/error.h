// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

/*! \file error.h
 *  \brief Build-execution adapter failures.
 */
#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace pkgbuild_exec {

enum class error_code {
  invalid_session,
  source_material_mismatch,
  package_input_mismatch,
  unsafe_path_layout,
  resource_preparation_failed,
  source_staging_failed,
  identity_derivation_failed,
  backend_contract_violation,
  payload_inspection_failed,
  artifact_encoding_failed,
  artifact_publication_failed,
  artifact_verification_failed,
  artifact_cleanup_failed,
};

[[nodiscard]] std::string_view to_string(error_code value) noexcept;

class error final : public std::runtime_error {
public:
  error(error_code code, std::string message);
  [[nodiscard]] error_code code() const noexcept;
private:
  error_code code_;
};

} // namespace pkgbuild_exec
