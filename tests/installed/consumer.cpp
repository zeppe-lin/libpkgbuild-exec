// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include <libpkgbuild-exec/libpkgbuild-exec.h>

#include <libpkgcatalog/libpkgcatalog.h>
#include <libpkgresolve/libpkgresolve.h>
#include <libpkgstate/libpkgstate.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;

namespace {

pkgsource::declaration_provenance at(std::string path, std::uint32_t line)
{
  return pkgsource::declaration_provenance(
      "recipe.yml", std::move(path), line, 1);
}

pkgsource::source_snapshot source()
{
  using namespace pkgsource;
  return seal_source(
      source_origin("recipe.yml"),
      recipe_declaration(
          package_release(package_reference("fixture"), "1.0", 1),
          package_metadata("Fixture", std::nullopt,
                           "https://example.invalid", {"MIT"}),
          {}, program(program_language::posix_shell, "true\n"), {}, {},
          architecture_requirements(
              {architecture_reference("x86_64")},
              {architecture_reference("x86_64")}),
          at("$", 1)),
      profile_catalog::seal({}));
}

template<typename Identity>
Identity state_identity(std::uint8_t seed)
{
  pkgstate::sha256_digest_bytes bytes{};
  for (std::size_t index = 0; index < bytes.size(); ++index)
    bytes[index] = static_cast<std::uint8_t>(seed + index);
  return Identity::from_sha256(bytes);
}

pkgstate::snapshot empty_state()
{
  return pkgstate::snapshot::make(pkgstate::state_target_binding::make(
      state_identity<pkgstate::managed_target_identity>(1),
      state_identity<pkgstate::state_store_identity>(2),
      state_identity<pkgstate::root_view_identity>(3),
      state_identity<pkgstate::state_backend_identity>(4),
      state_identity<pkgstate::publication_domain_identity>(5)));
}

pkgbuild::build_request build_request()
{
  using namespace pkgsource;
  auto profiles = profile_catalog::seal({});
  std::vector<source_snapshot> sources;
  sources.push_back(source());
  pkgcatalog::collection_declaration declaration(
      pkgcatalog::collection_reference("core"),
      pkgcatalog::collection_provenance(
          "/collections/core", std::nullopt,
          declaration_provenance("catalog.yml", "collections[0]", 1, 1)),
      std::move(sources));
  std::vector<pkgcatalog::catalog_collection> collections;
  collections.emplace_back(0, pkgcatalog::seal_collection(std::move(declaration)));
  auto catalog = pkgcatalog::catalog_snapshot::seal(
      std::move(profiles), std::move(collections));

  std::vector<pkgresolve::resolution_goal> goals;
  goals.emplace_back(
      requirement_scope::build(),
      requirement_subject(package_reference("fixture")), "installed-consumer");
  auto resolution = pkgresolve::resolve(pkgresolve::resolution_request::seal(
      std::move(catalog), empty_state(),
      pkgresolve::architecture_context(
          architecture_reference("x86_64"), architecture_reference("x86_64")),
      std::move(goals), pkgresolve::resolution_policy()));

  const pkgresolve::selected_package* selected = nullptr;
  for (const auto& candidate : resolution.selections()) {
    if (candidate.environment() == pkgresolve::resolution_environment::target &&
        candidate.package().name() == "fixture") {
      selected = &candidate;
      break;
    }
  }
  if (selected == nullptr)
    throw std::runtime_error("fixture resolution lacks build subject");

  return pkgbuild::build_request::seal(
      resolution, selected->identity(),
      pkgbuild::build_policy::make(
          pkgbuild::environment_policy::hermetic(1, 0022, 1700000000)));
}

pkgexec::backend_capability_profile capabilities()
{
  return pkgexec::backend_capability_profile::seal(
      pkgexec::backend_identity::from_sha256(std::string(64U, 'a')),
      {
          pkgexec::execution_guarantee::exact_interpreter,
          pkgexec::execution_guarantee::closed_environment,
          pkgexec::execution_guarantee::root_view,
          pkgexec::execution_guarantee::read_only_resources,
          pkgexec::execution_guarantee::writable_resources,
          pkgexec::execution_guarantee::fixed_credentials,
          pkgexec::execution_guarantee::network_denied,
          pkgexec::execution_guarantee::complete_stdout_capture,
          pkgexec::execution_guarantee::complete_stderr_capture,
          pkgexec::execution_guarantee::cleanup_verified,
      });
}

class refusing_backend final : public pkgexec::execution_backend {
public:
  pkgexec::backend_capability_profile capabilities() const override
  {
    return ::capabilities();
  }

  pkgexec::execution_result execute(
      const pkgexec::execution_request& request,
      const pkgexec::execution_resources&) override
  {
    return pkgexec::execution_result::failed_before_start(
        request, ::capabilities(),
        pkgexec::execution_failure_kind::backend_unsupported, {},
        "installed consumer refusal");
  }
};

} // namespace

int main()
{
  const fs::path root = fs::temp_directory_path() /
      ("libpkgbuild-exec-installed-" + std::to_string(::getpid()));
  std::error_code ignored;
  fs::remove_all(root, ignored);
  fs::create_directories(root / "local");
  fs::create_directories(root / "store");
  fs::create_directories(root / "root");

  try {
    auto request = build_request();
    auto materialization = pkgfetch::materialize(
        pkgfetch::materialization_request::seal(
            request.source(), root / "local", root / "store"));
    auto session = pkgbuild_exec::admitted_build_session::admit(
        request, std::move(materialization), {},
        {
            pkgexec::root_view_identity::from_sha256(std::string(64U, 'b')),
            root / "root",
            root / "session",
            root / "package",
            root / "artifact.tar",
        },
        {
            pkgexec::interpreter_identity::from_sha256(std::string(64U, 'c')),
            static_cast<std::uint64_t>(::getuid()),
            static_cast<std::uint64_t>(::getgid()),
            {},
        });

    const auto projected = pkgbuild_exec::seal_execution_request(session);
    const auto paths = pkgbuild_exec::project_prepared_paths(session);
    if (projected.purpose().kind() != pkgexec::execution_purpose_kind::build ||
        paths.source_tree != root / "session/source")
      return 1;

    refusing_backend backend;
    const auto result = pkgbuild_exec::execute(session, backend);
    const auto encoding = pkgbuild_exec::encode_build_execution_result(result);
    const auto decoded = pkgbuild_exec::decode_build_execution_result(
        encoding, result.build().request(), result.execution().request(),
        result.execution().backend());
    pkgbuild_exec::error sample(
        pkgbuild_exec::error_code::authority_mismatch,
        "installed consumer");
    fs::remove_all(root, ignored);
    return decoded.build().identity() == result.build().identity() &&
                   decoded.execution().identity() == result.execution().identity() &&
                   sample.code() == pkgbuild_exec::error_code::authority_mismatch
               ? 0
               : 1;
  } catch (...) {
    fs::remove_all(root, ignored);
    throw;
  }
}
