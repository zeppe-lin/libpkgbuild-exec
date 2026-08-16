// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../support/test.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <openssl/evp.h>

#include <libpkgbuild/libpkgbuild.h>
#include <libpkgexec/libpkgexec.h>
#include <libpkgcatalog/libpkgcatalog.h>
#include <libpkgresolve/libpkgresolve.h>
#include <libpkgsource/libpkgsource.h>
#include <libpkgstate/libpkgstate.h>

namespace pkgbuild_exec_test {

inline std::string sha256_text(std::string_view bytes)
{
  std::array<unsigned char, 32> output{};
  unsigned int size = 0;
  require(EVP_Digest(bytes.data(), bytes.size(), output.data(), &size,
                     EVP_sha256(), nullptr) == 1 && size == output.size(),
          "cannot hash fixture bytes");
  static constexpr char hex[] = "0123456789abcdef";
  std::string result(64U, '0');
  for (std::size_t index = 0; index < output.size(); ++index) {
    result[index * 2U] = hex[output[index] >> 4U];
    result[index * 2U + 1U] = hex[output[index] & 0x0fU];
  }
  return result;
}

inline pkgsource::declaration_provenance at(std::string path,
                                             std::uint32_t line)
{
  return pkgsource::declaration_provenance(
      "recipe.yml", std::move(path), line, 1);
}

inline pkgsource::source_snapshot source_snapshot(
    std::string_view first_digest, std::string_view second_digest)
{
  using namespace pkgsource;
  return seal_source(
      source_origin("recipe.yml"),
      recipe_declaration(
          package_release(package_reference("fixture"), "1.0", 1),
          package_metadata("Fixture", std::nullopt,
                           "https://example.invalid", {"MIT"}),
          {
              source_input::local(
                  "payload", "payload",
                  digest(digest_algorithm::sha256,
                         std::string(first_digest))),
              source_input::local(
                  "archive.tar", "archive.tar",
                  digest(digest_algorithm::sha256,
                         std::string(second_digest)),
                  source_unpack_kind::archive),
          },
          program(program_language::posix_shell,
                  "install -Dm755 payload /build/package/usr/bin/payload\n"),
          {
              requirement_declaration(
                  requirement_scope::build(),
                  requirement_subject(package_reference("tool")),
                  at("requirements.build[0]", 12)),
              requirement_declaration(
                  requirement_scope::build(),
                  requirement_subject(package_reference("helper")),
                  at("requirements.build[1]", 13)),
              requirement_declaration(
                  requirement_scope::check(),
                  requirement_subject(package_reference("checker")),
                  at("requirements.check[0]", 15)),
          },
          {},
          architecture_requirements(
              {architecture_reference("x86_64")},
              {architecture_reference("x86_64")}),
          at("$", 1),
          program(program_language::posix_shell, "true\n")),
      profile_catalog::seal({}));
}

inline pkgsource::source_snapshot dependency_source(std::string name)
{
  using namespace pkgsource;
  return seal_source(
      source_origin(name + "/recipe.yml"),
      recipe_declaration(
          package_release(package_reference(name), "1.0", 1),
          package_metadata(name, std::nullopt, std::nullopt, {"MIT"}),
          {}, program(program_language::posix_shell, "true\n"), {}, {},
          architecture_requirements(
              {architecture_reference("x86_64")},
              {architecture_reference("x86_64")}),
          at("$", 1)),
      profile_catalog::seal({}));
}

inline pkgstate::sha256_digest_bytes state_bytes(std::uint8_t seed)
{
  pkgstate::sha256_digest_bytes result{};
  for (std::size_t index = 0; index < result.size(); ++index) {
    result[index] = static_cast<std::uint8_t>(seed + index);
  }
  return result;
}

template<typename Identity>
Identity state_identity(std::uint8_t seed)
{
  return Identity::from_sha256(state_bytes(seed));
}

inline pkgstate::snapshot empty_state()
{
  return pkgstate::snapshot::make(pkgstate::state_target_binding::make(
      state_identity<pkgstate::managed_target_identity>(1),
      state_identity<pkgstate::state_store_identity>(2),
      state_identity<pkgstate::root_view_identity>(3),
      state_identity<pkgstate::state_backend_identity>(4),
      state_identity<pkgstate::publication_domain_identity>(5)));
}

inline pkgresolve::resolution_result resolution(
    std::string_view first_digest, std::string_view second_digest)
{
  using namespace pkgsource;
  auto profiles = profile_catalog::seal({});
  std::vector<pkgsource::source_snapshot> sources;
  sources.push_back(source_snapshot(first_digest, second_digest));
  sources.push_back(dependency_source("tool"));
  sources.push_back(dependency_source("helper"));
  sources.push_back(dependency_source("checker"));

  pkgcatalog::collection_declaration declaration(
      pkgcatalog::collection_reference("core"),
      pkgcatalog::collection_provenance(
          "/collections/core", std::nullopt,
          declaration_provenance("catalog.yml", "collections[0]", 1, 1)),
      std::move(sources));
  std::vector<pkgcatalog::catalog_collection> collections;
  collections.emplace_back(
      0, pkgcatalog::seal_collection(std::move(declaration)));
  auto catalog = pkgcatalog::catalog_snapshot::seal(
      profiles, std::move(collections));

  std::vector<pkgresolve::resolution_goal> goals;
  goals.emplace_back(
      requirement_scope::build(),
      requirement_subject(package_reference("fixture")), "test-build");
  goals.emplace_back(
      requirement_scope::check(),
      requirement_subject(package_reference("fixture")), "test-check");
  auto request = pkgresolve::resolution_request::seal(
      std::move(catalog), empty_state(),
      pkgresolve::architecture_context(
          architecture_reference("x86_64"),
          architecture_reference("x86_64")),
      std::move(goals), pkgresolve::resolution_policy());
  return pkgresolve::resolve(std::move(request));
}

inline const pkgresolve::selected_package& subject(
    const pkgresolve::resolution_result& resolved)
{
  for (const auto& selection : resolved.selections()) {
    if (selection.environment() == pkgresolve::resolution_environment::target &&
        selection.package().name() == "fixture") {
      return selection;
    }
  }
  throw std::runtime_error("fixture resolution lacks build subject");
}

inline pkgbuild::build_request build_request(
    const pkgresolve::resolution_result& resolved,
    std::uint32_t parallelism,
    std::uint32_t file_creation_mask,
    std::optional<std::int64_t> source_date_epoch)
{
  return pkgbuild::build_request::seal(
      resolved, subject(resolved).identity(),
      pkgbuild::build_policy::make(
          pkgbuild::environment_policy::hermetic(
              parallelism, file_creation_mask, source_date_epoch)));
}

inline std::string environment_value(
    const pkgexec::environment_policy& environment,
    std::string_view name)
{
  const auto found = std::find_if(
      environment.additional_variables().begin(),
      environment.additional_variables().end(),
      [&](const pkgexec::environment_variable& value) {
        return value.name() == name;
      });
  require(found != environment.additional_variables().end(),
          "required environment variable is absent");
  return found->value();
}

} // namespace pkgbuild_exec_test
