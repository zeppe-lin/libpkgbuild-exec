// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include "result_identity.h"

#include <libpkgbuild-exec/error.h>

#include <openssl/evp.h>

#include <array>
#include <memory>
#include <string>
#include <string_view>

namespace pkgbuild_exec::detail {
namespace {

std::string sha256_bytes(std::string_view value)
{
  using context_pointer =
      std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
  context_pointer context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  if (!context ||
      EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1 ||
      EVP_DigestUpdate(context.get(), value.data(), value.size()) != 1)
    throw error(error_code::identity_derivation_failed,
                "cannot initialize build-result evidence identity");

  std::array<unsigned char, 32> output{};
  unsigned int size = 0;
  if (EVP_DigestFinal_ex(context.get(), output.data(), &size) != 1 ||
      size != output.size())
    throw error(error_code::identity_derivation_failed,
                "cannot finalize build-result evidence identity");

  static constexpr char digits[] = "0123456789abcdef";
  std::string result(output.size() * 2U, '0');
  for (std::size_t index = 0; index < output.size(); ++index) {
    result[index * 2U] = digits[output[index] >> 4U];
    result[index * 2U + 1U] = digits[output[index] & 0x0fU];
  }
  return result;
}

std::string domain_hash(std::string_view domain, std::string_view material)
{
  std::string bytes(domain);
  bytes.push_back('\0');
  bytes.append(material);
  return sha256_bytes(bytes);
}

} // namespace

pkgbuild::execution_evidence_identity execution_evidence_identity(
    const pkgexec::execution_result& result)
{
  return pkgbuild::execution_evidence_identity::from_sha256(domain_hash(
      "libpkgbuild-exec:execution-evidence:v1", result.identity().hex()));
}

pkgbuild::failure_evidence_identity execution_failure_identity(
    const pkgexec::execution_result& result)
{
  return pkgbuild::failure_evidence_identity::from_sha256(domain_hash(
      "libpkgbuild-exec:execution-failure:v1", result.identity().hex()));
}

pkgbuild::failure_evidence_identity sealing_failure_identity(
    const pkgexec::execution_result& result,
    result_sealing_failure_kind kind)
{
  std::string material(result.identity().hex());
  material.push_back('\0');
  material.append(to_string(kind));
  return pkgbuild::failure_evidence_identity::from_sha256(domain_hash(
      "libpkgbuild-exec:result-sealing-failure:v1", material));
}

} // namespace pkgbuild_exec::detail
