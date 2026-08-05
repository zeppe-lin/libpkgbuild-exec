// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include <libpkgbuild-exec/result_codec.h>

#include <libpkgbuild-exec/error.h>
#include <libpkgexec/result_codec.h>

#include "result_identity.h"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pkgbuild_exec {
namespace {

constexpr std::array<std::uint8_t, 8> encoding_magic{
    'P', 'K', 'G', 'B', 'X', 'R', '1', 0};
constexpr std::size_t checksum_size = 32U;
constexpr std::size_t maximum_diagnostic_size = 1024U * 1024U;
constexpr std::size_t maximum_text_size = 1024U * 1024U;
constexpr std::size_t maximum_payload_entry_count = 1024U * 1024U;
constexpr std::size_t maximum_backend_identity_size = 4096U;

[[noreturn]] void inconsistent(const std::string& message)
{
  throw error(error_code::inconsistent_result, message);
}

[[noreturn]] void corrupt(const std::string& message)
{
  throw error(error_code::corrupt_encoding, message);
}

[[noreturn]] void mismatch(const std::string& message)
{
  throw error(error_code::authority_mismatch, message);
}

std::string sha256_hex(std::string_view value)
{
  using context_pointer =
      std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
  context_pointer context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  if (!context ||
      EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1 ||
      EVP_DigestUpdate(context.get(), value.data(), value.size()) != 1)
    inconsistent("cannot initialize build-execution record checksum");

  std::array<unsigned char, 32> output{};
  unsigned int size = 0;
  if (EVP_DigestFinal_ex(context.get(), output.data(), &size) != 1 ||
      size != output.size())
    inconsistent("cannot finalize build-execution record checksum");

  static constexpr char digits[] = "0123456789abcdef";
  std::string result(output.size() * 2U, '0');
  for (std::size_t index = 0; index < output.size(); ++index) {
    result[index * 2U] = digits[output[index] >> 4U];
    result[index * 2U + 1U] = digits[output[index] & 0x0fU];
  }
  return result;
}

class writer final {
public:
  void byte(std::uint8_t value)
  {
    output_.push_back(value);
    check_size();
  }

  void u16(std::uint16_t value)
  {
    byte(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    byte(static_cast<std::uint8_t>(value & 0xffU));
  }

  void u32(std::uint32_t value)
  {
    for (int shift = 24; shift >= 0; shift -= 8)
      byte(static_cast<std::uint8_t>((value >> shift) & 0xffU));
  }

  void u64(std::uint64_t value)
  {
    for (int shift = 56; shift >= 0; shift -= 8)
      byte(static_cast<std::uint8_t>((value >> shift) & 0xffU));
  }

  void i64(std::int64_t value)
  {
    u64(static_cast<std::uint64_t>(value));
  }

  void boolean(bool value)
  {
    byte(value ? 1U : 0U);
  }

  void raw(const std::uint8_t* data, std::size_t size)
  {
    if (size == 0U)
      return;
    if (size > maximum_build_execution_result_encoding_size - output_.size())
      inconsistent("build-execution encoding exceeds maximum size");
    output_.insert(output_.end(), data, data + size);
  }

  void bytes(const std::vector<std::uint8_t>& value)
  {
    if (value.size() > std::numeric_limits<std::uint32_t>::max())
      inconsistent("embedded execution evidence is too large");
    u32(static_cast<std::uint32_t>(value.size()));
    raw(value.data(), value.size());
  }

  void text(std::string_view value)
  {
    if (value.size() > std::numeric_limits<std::uint32_t>::max())
      inconsistent("build-execution text field is too large");
    u32(static_cast<std::uint32_t>(value.size()));
    raw(reinterpret_cast<const std::uint8_t*>(value.data()), value.size());
  }

  void identity(std::string_view value)
  {
    if (value.size() != 64U ||
        !std::all_of(value.begin(), value.end(), [](char current) {
          return (current >= '0' && current <= '9') ||
                 (current >= 'a' && current <= 'f');
        }))
      inconsistent("build-execution record contains an invalid identity");
    for (std::size_t index = 0; index < value.size(); index += 2U)
      byte(static_cast<std::uint8_t>((digit(value[index]) << 4U) |
                                     digit(value[index + 1U])));
  }

  const build_execution_result_encoding& output() const noexcept
  {
    return output_;
  }

  build_execution_result_encoding finish()
  {
    return std::move(output_);
  }

private:
  static std::uint8_t digit(char value)
  {
    return value >= '0' && value <= '9'
        ? static_cast<std::uint8_t>(value - '0')
        : static_cast<std::uint8_t>(value - 'a' + 10);
  }

  void check_size() const
  {
    if (output_.size() > maximum_build_execution_result_encoding_size)
      inconsistent("build-execution encoding exceeds maximum size");
  }

  build_execution_result_encoding output_;
};

class reader final {
public:
  reader(const build_execution_result_encoding& input, std::size_t limit)
      : input_(input), limit_(limit)
  {
  }

  std::uint8_t byte()
  {
    require(1U);
    return input_[offset_++];
  }

  std::uint16_t u16()
  {
    std::uint16_t value = 0U;
    for (int index = 0; index < 2; ++index)
      value = static_cast<std::uint16_t>((value << 8U) | byte());
    return value;
  }

  std::uint32_t u32()
  {
    std::uint32_t value = 0U;
    for (int index = 0; index < 4; ++index)
      value = (value << 8U) | byte();
    return value;
  }

  std::uint64_t u64()
  {
    std::uint64_t value = 0U;
    for (int index = 0; index < 8; ++index)
      value = (value << 8U) | byte();
    return value;
  }

  std::int64_t i64()
  {
    return static_cast<std::int64_t>(u64());
  }

  bool boolean()
  {
    const auto value = byte();
    if (value > 1U)
      corrupt("build-execution encoding contains an invalid boolean");
    return value == 1U;
  }

  std::string identity()
  {
    static constexpr char digits[] = "0123456789abcdef";
    require(32U);
    std::string value(64U, '0');
    for (std::size_t index = 0; index < 32U; ++index) {
      const auto current = input_[offset_++];
      value[index * 2U] = digits[(current >> 4U) & 0x0fU];
      value[index * 2U + 1U] = digits[current & 0x0fU];
    }
    return value;
  }

  std::string text(std::size_t maximum)
  {
    const auto size = static_cast<std::size_t>(u32());
    if (size > maximum)
      corrupt("build-execution text field exceeds its limit");
    require(size);
    std::string value(
        reinterpret_cast<const char*>(input_.data() + offset_), size);
    offset_ += size;
    return value;
  }

  std::vector<std::uint8_t> bytes(std::size_t maximum)
  {
    const auto size = static_cast<std::size_t>(u32());
    if (size > maximum)
      corrupt("embedded execution evidence exceeds its limit");
    require(size);
    std::vector<std::uint8_t> value(
        input_.begin() + static_cast<std::ptrdiff_t>(offset_),
        input_.begin() + static_cast<std::ptrdiff_t>(offset_ + size));
    offset_ += size;
    return value;
  }

  void finish() const
  {
    if (offset_ != limit_)
      corrupt("build-execution encoding contains trailing payload bytes");
  }

private:
  void require(std::size_t size) const
  {
    if (offset_ > limit_ || size > limit_ - offset_)
      corrupt("build-execution encoding is truncated");
  }

  const build_execution_result_encoding& input_;
  std::size_t limit_;
  std::size_t offset_ = 0U;
};

pkgbuild::payload_entry_type decode_entry_type(std::uint8_t value)
{
  if (value > static_cast<std::uint8_t>(
                  pkgbuild::payload_entry_type::block_device))
    corrupt("build-execution encoding contains an unknown payload type");
  return static_cast<pkgbuild::payload_entry_type>(value);
}

pkgbuild::artifact_encoding decode_artifact_encoding(std::uint8_t value)
{
  if (value != static_cast<std::uint8_t>(
                   pkgbuild::artifact_encoding::package_tar))
    corrupt("build-execution encoding contains an unknown artifact encoding");
  return pkgbuild::artifact_encoding::package_tar;
}

pkgbuild::artifact_compression decode_artifact_compression(std::uint8_t value)
{
  if (value > static_cast<std::uint8_t>(pkgbuild::artifact_compression::zstd))
    corrupt("build-execution encoding contains an unknown compression");
  return static_cast<pkgbuild::artifact_compression>(value);
}

result_sealing_failure_kind decode_sealing_failure(std::uint8_t value)
{
  if (value > static_cast<std::uint8_t>(
                  result_sealing_failure_kind::artifact_cleanup))
    corrupt("build-execution encoding contains an unknown sealing failure");
  return static_cast<result_sealing_failure_kind>(value);
}

void encode_payload_entry(writer& output, const pkgbuild::payload_entry& entry)
{
  output.byte(static_cast<std::uint8_t>(entry.type()));
  output.text(entry.path().string());
  output.u32(entry.mode());
  output.u64(entry.uid());
  output.u64(entry.gid());
  output.u64(entry.size());
  output.i64(entry.modification_time().seconds);
  output.u32(entry.modification_time().nanoseconds);

  switch (entry.type()) {
    case pkgbuild::payload_entry_type::regular:
      if (!entry.regular_content())
        inconsistent("regular payload entry lacks content evidence");
      output.identity(entry.regular_content()->hex());
      return;
    case pkgbuild::payload_entry_type::directory:
    case pkgbuild::payload_entry_type::fifo:
      return;
    case pkgbuild::payload_entry_type::symlink:
      if (!entry.symlink_target())
        inconsistent("symbolic-link payload entry lacks a target");
      output.text(*entry.symlink_target());
      return;
    case pkgbuild::payload_entry_type::hardlink:
      if (!entry.hardlink_target())
        inconsistent("hard-link payload entry lacks a target");
      output.text(entry.hardlink_target()->string());
      return;
    case pkgbuild::payload_entry_type::character_device:
    case pkgbuild::payload_entry_type::block_device:
      if (!entry.device())
        inconsistent("device payload entry lacks a device number");
      output.u64(entry.device()->major);
      output.u64(entry.device()->minor);
      return;
  }
}

pkgbuild::payload_entry decode_payload_entry(reader& input)
{
  const auto type = decode_entry_type(input.byte());
  auto path = pkgbuild::payload_path::parse(input.text(maximum_text_size));
  const auto mode = input.u32();
  const auto uid = input.u64();
  const auto gid = input.u64();
  const auto size = input.u64();
  const pkgbuild::payload_time time{input.i64(), input.u32()};

  switch (type) {
    case pkgbuild::payload_entry_type::regular:
      return pkgbuild::payload_entry::regular(
          std::move(path), mode, uid, gid, size, time,
          pkgbuild::sha256_digest(input.identity()));
    case pkgbuild::payload_entry_type::directory:
      if (size != 0U)
        corrupt("directory payload entry has a nonzero size");
      return pkgbuild::payload_entry::directory(
          std::move(path), mode, uid, gid, time);
    case pkgbuild::payload_entry_type::symlink:
      if (size != 0U)
        corrupt("symbolic-link payload entry has a nonzero size");
      return pkgbuild::payload_entry::symlink(
          std::move(path), mode, uid, gid, time,
          input.text(maximum_text_size));
    case pkgbuild::payload_entry_type::hardlink:
      if (size != 0U)
        corrupt("hard-link payload entry has a nonzero size");
      return pkgbuild::payload_entry::hardlink(
          std::move(path), mode, uid, gid, time,
          pkgbuild::payload_path::parse(input.text(maximum_text_size)));
    case pkgbuild::payload_entry_type::fifo:
      if (size != 0U)
        corrupt("FIFO payload entry has a nonzero size");
      return pkgbuild::payload_entry::fifo(
          std::move(path), mode, uid, gid, time);
    case pkgbuild::payload_entry_type::character_device:
      if (size != 0U)
        corrupt("character-device payload entry has a nonzero size");
      return pkgbuild::payload_entry::character_device(
          std::move(path), mode, uid, gid, time,
          pkgbuild::device_number{input.u64(), input.u64()});
    case pkgbuild::payload_entry_type::block_device:
      if (size != 0U)
        corrupt("block-device payload entry has a nonzero size");
      return pkgbuild::payload_entry::block_device(
          std::move(path), mode, uid, gid, time,
          pkgbuild::device_number{input.u64(), input.u64()});
  }
  corrupt("build-execution encoding contains an unknown payload type");
}

void require_receipt_binding(
    const pkgbuild::payload_manifest& payload,
    const pkgbuild::sealed_artifact& artifact,
    const pkgimage::archive_inspection_receipt& receipt,
    bool encoding)
{
  const auto expected_digest = pkgimage::complete_archive_digest::parse(
      "v1:sha256:" + artifact.complete_digest().hex());
  if (receipt.archive_digest() != expected_digest) {
    if (encoding)
      inconsistent("archive inspection receipt identifies other artifact bytes");
    corrupt("archive inspection receipt identifies other artifact bytes");
  }
  if (receipt.entry_count() != payload.entries().size()) {
    if (encoding)
      inconsistent("archive inspection receipt has another entry count");
    corrupt("archive inspection receipt has another entry count");
  }
}

pkgbuild::build_result expected_build_result(
    const pkgexec::execution_result& execution,
    pkgbuild::build_request request,
    const std::optional<result_sealing_failure_kind>& sealing_failure,
    const std::optional<pkgbuild::payload_manifest>& payload,
    const std::optional<pkgbuild::sealed_artifact>& artifact)
{
  const auto execution_evidence = detail::execution_evidence_identity(execution);
  if (execution.status() != pkgexec::execution_status::succeeded) {
    if (sealing_failure || payload || artifact)
      inconsistent("failed execution carries sealing or artifact evidence");
    return pkgbuild::build_result::failed(
        std::move(request), execution_evidence,
        detail::execution_failure_identity(execution));
  }
  if (sealing_failure) {
    if (payload || artifact)
      inconsistent("sealing failure carries a successful artifact");
    return pkgbuild::build_result::failed(
        std::move(request), execution_evidence,
        detail::sealing_failure_identity(execution, *sealing_failure));
  }
  if (!payload || !artifact)
    inconsistent("successful execution lacks retained payload or artifact");
  return pkgbuild::build_result::succeeded(
      std::move(request), *payload, *artifact, execution_evidence);
}

void validate_result(const build_execution_result& result)
{
  if (result.execution().request().purpose().kind() !=
      pkgexec::execution_purpose_kind::build)
    inconsistent("build execution result retains a non-build request");

  const auto& build = result.build();
  const bool succeeded =
      result.execution().status() == pkgexec::execution_status::succeeded &&
      !result.sealing_failure();
  if (succeeded != (build.outcome() == pkgbuild::build_outcome::succeeded))
    inconsistent("build outcome does not match execution and sealing evidence");

  if (result.execution().status() != pkgexec::execution_status::succeeded) {
    if (result.sealing_failure() || result.image_authority())
      inconsistent("failed execution carries post-execution evidence");
    if (result.diagnostic() != result.execution().diagnostic())
      inconsistent("failed execution diagnostic differs from execution evidence");
  } else if (result.sealing_failure()) {
    if (result.image_authority())
      inconsistent("failed result sealing carries build-image authority");
  } else {
    if (!result.diagnostic().empty())
      inconsistent("successful build carries a sealing diagnostic");
    if (!build.payload() || !build.artifact() ||
        !result.image_authority())
      inconsistent("successful build lacks artifact evidence");
    require_receipt_binding(
        *build.payload(), *build.artifact(),
        result.image_authority()->image().receipt(), true);
    if (result.image_authority()->build().identity() != build.identity())
      inconsistent("build-image authority retains another build result");
  }

  auto expected = expected_build_result(
      result.execution(), build.request(), result.sealing_failure(),
      build.payload(), build.artifact());
  if (expected.identity() != build.identity())
    inconsistent("build result identity does not match retained evidence");
}

void encode_receipt(writer& output,
                    const pkgimage::archive_inspection_receipt& receipt)
{
  output.u32(receipt.schema_version());
  output.text(receipt.backend_identity().string());
  output.text(receipt.archive_digest().string());
  output.text(receipt.image_identity().string());
  output.u64(receipt.entry_count());
  output.text(receipt.identity().string());
}

pkgimage::archive_inspection_receipt decode_receipt(reader& input)
{
  if (input.u32() != 1U)
    corrupt("archive inspection receipt schema is unsupported");
  auto backend = pkgimage::archive_backend_identity::parse(
      input.text(maximum_backend_identity_size));
  auto archive_digest = pkgimage::complete_archive_digest::parse(
      input.text(maximum_text_size));
  auto image_identity = pkgimage::package_image_identity::parse(
      input.text(maximum_text_size));
  const auto entry_count = input.u64();
  auto receipt = pkgimage::archive_inspection_receipt(
      std::move(backend), std::move(archive_digest),
      std::move(image_identity), entry_count);
  const auto retained_identity = input.text(maximum_text_size);
  if (receipt.identity().string() != retained_identity)
    corrupt("archive inspection receipt identity mismatch");
  return receipt;
}

} // namespace

namespace detail {

class codec_access final {
public:
  static build_execution_result make(
      pkgexec::execution_result execution,
      pkgbuild::build_result build,
      std::optional<result_sealing_failure_kind> sealing_failure,
      std::string diagnostic,
      std::optional<pkgbuild::image_adapter::build_image_authority>
          image_authority)
  {
    return build_execution_result(
        std::move(execution), std::move(build), sealing_failure,
        std::move(diagnostic), std::move(image_authority));
  }
};

} // namespace detail

build_execution_result_encoding encode_build_execution_result(
    const build_execution_result& result)
{
  validate_result(result);
  if (result.diagnostic().size() > maximum_diagnostic_size)
    inconsistent("build-execution diagnostic exceeds its limit");
  if (result.build().payload() &&
      result.build().payload()->entries().size() > maximum_payload_entry_count)
    inconsistent("build payload entry count exceeds its limit");

  writer output;
  output.raw(encoding_magic.data(), encoding_magic.size());
  output.u16(build_execution_result_encoding_version);
  output.identity(result.build().request().identity().hex());
  output.identity(result.execution().request().identity().hex());
  output.identity(result.execution().backend().identity().hex());
  output.identity(result.execution().identity().hex());
  output.identity(result.build().identity().hex());
  output.bytes(pkgexec::encode_execution_result(result.execution()));
  output.byte(static_cast<std::uint8_t>(result.build().outcome()));
  output.boolean(result.sealing_failure().has_value());
  if (result.sealing_failure())
    output.byte(static_cast<std::uint8_t>(*result.sealing_failure()));
  output.text(result.diagnostic());

  output.boolean(result.build().payload().has_value());
  if (result.build().payload()) {
    const auto& entries = result.build().payload()->entries();
    output.u32(static_cast<std::uint32_t>(entries.size()));
    for (const auto& entry : entries)
      encode_payload_entry(output, entry);
  }

  output.boolean(result.build().artifact().has_value());
  if (result.build().artifact()) {
    const auto& artifact = *result.build().artifact();
    output.byte(static_cast<std::uint8_t>(artifact.encoding()));
    output.byte(static_cast<std::uint8_t>(artifact.compression()));
    output.u64(artifact.byte_count());
    output.identity(artifact.complete_digest().hex());
  }

  output.boolean(result.image_authority().has_value());
  if (result.image_authority())
    encode_receipt(output, result.image_authority()->image().receipt());

  const auto& payload = output.output();
  output.identity(sha256_hex(std::string_view(
      reinterpret_cast<const char*>(payload.data()), payload.size())));
  return output.finish();
}

build_execution_result decode_build_execution_result(
    const build_execution_result_encoding& encoding,
    pkgbuild::build_request build_request,
    pkgexec::execution_request execution_request,
    pkgexec::backend_capability_profile backend)
{
  try {
    if (encoding.size() > maximum_build_execution_result_encoding_size)
      corrupt("build-execution encoding exceeds maximum size");
    if (encoding.size() < encoding_magic.size() + 2U + checksum_size)
      corrupt("build-execution encoding is truncated");

    const auto payload_size = encoding.size() - checksum_size;
    const auto actual_checksum = sha256_hex(std::string_view(
        reinterpret_cast<const char*>(encoding.data()), payload_size));
    static constexpr char digits[] = "0123456789abcdef";
    std::string retained_checksum(64U, '0');
    for (std::size_t index = 0; index < checksum_size; ++index) {
      const auto current = encoding[payload_size + index];
      retained_checksum[index * 2U] = digits[(current >> 4U) & 0x0fU];
      retained_checksum[index * 2U + 1U] = digits[current & 0x0fU];
    }
    if (retained_checksum != actual_checksum)
      corrupt("build-execution encoding checksum mismatch");

    reader input(encoding, payload_size);
    for (const auto expected : encoding_magic)
      if (input.byte() != expected)
        corrupt("build-execution encoding has invalid magic");
    if (input.u16() != build_execution_result_encoding_version)
      corrupt("build-execution encoding version is unsupported");

    const auto build_request_identity = input.identity();
    const auto execution_request_identity = input.identity();
    const auto backend_identity = input.identity();
    const auto execution_identity = input.identity();
    const auto build_identity = input.identity();
    if (build_request.identity().hex() != build_request_identity)
      mismatch("build-execution record belongs to another build request");
    if (execution_request.identity().hex() != execution_request_identity)
      mismatch("build-execution record belongs to another execution request");
    if (backend.identity().hex() != backend_identity)
      mismatch("build-execution record belongs to another backend profile");
    if (execution_request.purpose().kind() !=
        pkgexec::execution_purpose_kind::build)
      mismatch("supplied execution request is not a build request");

    auto execution_encoding = input.bytes(
        pkgexec::maximum_execution_result_encoding_size);
    auto execution = [&]() -> pkgexec::execution_result {
      try {
        return pkgexec::decode_execution_result(
            execution_encoding, std::move(execution_request),
            std::move(backend));
      } catch (const pkgexec::error& problem) {
        if (problem.code() == pkgexec::error_code::authority_mismatch)
          mismatch("embedded execution evidence belongs to another authority");
        corrupt("embedded execution evidence is invalid: " +
                std::string(problem.what()));
      }
    }();
    if (execution.identity().hex() != execution_identity)
      corrupt("embedded execution evidence identity mismatch");

    const auto outcome_value = input.byte();
    if (outcome_value > static_cast<std::uint8_t>(
                            pkgbuild::build_outcome::failed))
      corrupt("build-execution encoding contains an unknown build outcome");
    const auto outcome = static_cast<pkgbuild::build_outcome>(outcome_value);
    std::optional<result_sealing_failure_kind> sealing_failure;
    if (input.boolean())
      sealing_failure = decode_sealing_failure(input.byte());
    auto diagnostic = input.text(maximum_diagnostic_size);

    std::optional<pkgbuild::payload_manifest> payload;
    if (input.boolean()) {
      const auto count = static_cast<std::size_t>(input.u32());
      if (count > maximum_payload_entry_count)
        corrupt("build payload entry count exceeds its limit");
      std::vector<pkgbuild::payload_entry> entries;
      entries.reserve(count);
      for (std::size_t index = 0; index < count; ++index)
        entries.push_back(decode_payload_entry(input));
      payload = pkgbuild::payload_manifest::seal(std::move(entries));
    }

    std::optional<pkgbuild::sealed_artifact> artifact;
    if (input.boolean()) {
      const auto artifact_encoding = decode_artifact_encoding(input.byte());
      const auto artifact_compression =
          decode_artifact_compression(input.byte());
      const auto artifact_size = input.u64();
      auto artifact_digest = pkgbuild::sha256_digest(input.identity());
      artifact = pkgbuild::sealed_artifact::make(
          artifact_encoding, artifact_compression, artifact_size,
          std::move(artifact_digest));
    }

    std::optional<pkgimage::archive_inspection_receipt> receipt;
    if (input.boolean())
      receipt = decode_receipt(input);
    input.finish();

    if ((outcome == pkgbuild::build_outcome::succeeded) !=
        (execution.status() == pkgexec::execution_status::succeeded &&
         !sealing_failure))
      corrupt("build outcome does not match execution and sealing evidence");
    if (execution.status() != pkgexec::execution_status::succeeded) {
      if (sealing_failure || payload || artifact || receipt)
        corrupt("failed execution carries post-execution evidence");
      if (diagnostic != execution.diagnostic())
        corrupt("failed execution diagnostic differs from execution evidence");
    } else if (sealing_failure) {
      if (payload || artifact || receipt)
        corrupt("sealing failure carries successful artifact evidence");
    } else {
      if (!diagnostic.empty() || !payload || !artifact || !receipt)
        corrupt("successful build has an invalid retained evidence shape");
      require_receipt_binding(*payload, *artifact, *receipt, false);
    }

    auto build = expected_build_result(
        execution, std::move(build_request), sealing_failure, payload, artifact);
    if (build.identity().hex() != build_identity)
      corrupt("build result identity mismatch");

    std::optional<pkgbuild::image_adapter::build_image_authority>
        image_authority;
    if (receipt)
      image_authority =
          pkgbuild::image_adapter::build_image_authority::restore(
              build, *receipt);
    auto decoded = detail::codec_access::make(
        std::move(execution), std::move(build), sealing_failure,
        std::move(diagnostic), std::move(image_authority));
    validate_result(decoded);
    if (encode_build_execution_result(decoded) != encoding)
      corrupt("build-execution encoding is not canonical");
    return decoded;
  } catch (const error& problem) {
    if (problem.code() == error_code::corrupt_encoding ||
        problem.code() == error_code::authority_mismatch)
      throw;
    throw error(error_code::corrupt_encoding,
                "build-execution encoding violates the result contract: " +
                    std::string(problem.what()));
  } catch (const std::exception& problem) {
    throw error(error_code::corrupt_encoding,
                "build-execution encoding violates a subordinate contract: " +
                    std::string(problem.what()));
  }
}

} // namespace pkgbuild_exec
