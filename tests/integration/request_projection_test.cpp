// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../fixtures/session.h"
#include "../support/test.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace pkgbuild_exec_test;

void require_exact_guarantees(const pkgexec::execution_request& request)
{
  std::vector<pkgexec::execution_guarantee> expected{
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
  };
  std::sort(expected.begin(), expected.end());
  require(request.required_guarantees() == expected,
          "build execution request requires the wrong guarantees");
}

void projects_exact_build_contract()
{
  fixture_owner owner("projection");
  const auto session = owner.get().session();
  const auto request = pkgbuild_exec::seal_execution_request(session);
  const auto paths = pkgbuild_exec::project_prepared_paths(session);

  require(!fs::exists(session.paths().session_root) &&
              !fs::exists(session.paths().package_output_root) &&
              !fs::exists(session.paths().artifact_path),
          "pure projection touched effect coordinates");
  require(paths.source_tree == session.paths().session_root / "source" &&
              paths.workspace == session.paths().session_root / "work" &&
              paths.temporary_root == session.paths().session_root / "tmp",
          "prepared path projection changed");
  require(request.program() == owner.get().request.build_program(),
          "execution program differs from sealed build authority");
  require(request.purpose().kind() == pkgexec::execution_purpose_kind::build,
          "adapter did not project build purpose");
  require(request.interpreter() == session.identity().interpreter &&
              request.root_view() == session.paths().root_view,
          "execution identity projection changed");
  require(request.environment().network() == pkgexec::network_policy::denied &&
              request.environment().standard_input() ==
                  pkgexec::stdin_policy::null_device &&
              request.environment().standard_output() ==
                  pkgexec::stream_policy::capture_complete &&
              request.environment().standard_error() ==
                  pkgexec::stream_policy::capture_complete,
          "non-interactive build process policy changed");
  require(environment_value(request.environment(), "PKG_SOURCE_ROOT") ==
              "/build/source" &&
              environment_value(request.environment(), "PKG_BUILD_ROOT") ==
                  "/build/work" &&
              environment_value(request.environment(), "PKG_DESTDIR") ==
                  "/build/package" &&
              environment_value(request.environment(), "PKG_BUILD_INPUTS") ==
                  "tool" &&
              environment_value(request.environment(), "PKG_CHECK_INPUTS") ==
                  "checker" &&
              environment_value(request.environment(), "PKG_JOBS") == "4",
          "build environment projection changed");
  require(request.credentials().user_id() == ::getuid() &&
              request.credentials().group_id() == ::getgid() &&
              request.credentials().no_new_privileges(),
          "execution credential projection changed");
  require(request.limits().empty() &&
              request.cancellation().mode() ==
                  pkgexec::cancellation_mode::disabled,
          "adapter invented limits or cancellation");
  require_exact_guarantees(request);

  const auto& source = request.resources().binding(pkgexec::resource_slot::named(
      pkgexec::resource_role::source_tree, "sources"));
  const auto& build = request.resources().binding(pkgexec::resource_slot::named(
      pkgexec::resource_role::build_input_tree, "tool"));
  const auto& check = request.resources().binding(pkgexec::resource_slot::named(
      pkgexec::resource_role::check_input_tree, "checker"));
  const auto& workspace = request.resources().binding(
      pkgexec::resource_slot::singleton(pkgexec::resource_role::build_workspace));
  const auto& output = request.resources().binding(
      pkgexec::resource_slot::singleton(
          pkgexec::resource_role::package_output_root));
  const auto& temporary = request.resources().binding(
      pkgexec::resource_slot::singleton(
          pkgexec::resource_role::private_temporary_root));

  require(source.access() == pkgexec::resource_access::read_only &&
              source.mount_point().string() == "/build/source" &&
              build.access() == pkgexec::resource_access::read_only &&
              build.mount_point().string() == "/build/inputs/build/tool" &&
              check.access() == pkgexec::resource_access::read_only &&
              check.mount_point().string() == "/build/inputs/check/checker" &&
              workspace.access() == pkgexec::resource_access::writable &&
              workspace.mount_point().string() == "/build/work" &&
              output.access() == pkgexec::resource_access::writable &&
              output.mount_point().string() == "/build/package" &&
              temporary.access() == pkgexec::resource_access::writable &&
              temporary.mount_point().string() == "/tmp",
          "execution resource projection changed");
}

void host_effect_paths_do_not_change_request_authority()
{
  fixture_owner owner("projection-effects");
  const auto first = pkgbuild_exec::seal_execution_request(
      owner.get().session("first"));
  const auto second = pkgbuild_exec::seal_execution_request(
      owner.get().session("second"));
  require(first == second,
          "session/output/artifact host coordinates contaminated request identity");

  const fs::path alternate = owner.get().root / "alternate-inputs";
  fs::create_directories(alternate / "tool");
  fs::create_directories(alternate / "checker");
  auto relocated = pkgbuild_exec::admitted_build_session::admit(
      owner.get().request, owner.get().materialization,
      owner.get().package_inputs(alternate), owner.get().paths("relocated"),
      owner.get().execution_identity());
  const auto relocated_request =
      pkgbuild_exec::seal_execution_request(relocated);
  require(first == relocated_request,
          "package-input host paths contaminated request identity");
}

void semantic_resource_identity_changes_request()
{
  fixture_owner owner("projection-resource-identity");
  const auto ordinary = pkgbuild_exec::seal_execution_request(
      owner.get().session("ordinary"));
  auto inputs = owner.get().package_inputs();
  inputs[0].resource =
      pkgexec::resource_identity::from_sha256(std::string(64U, 'e'));
  auto changed = pkgbuild_exec::admitted_build_session::admit(
      owner.get().request, owner.get().materialization, std::move(inputs),
      owner.get().paths("changed"), owner.get().execution_identity());
  require(ordinary != pkgbuild_exec::seal_execution_request(changed),
          "semantic package-input resource identity did not affect request");
}

void adapter_owned_resource_alias_fails_before_effects()
{
  fixture_owner owner("projection-alias");
  const auto ordinary = pkgbuild_exec::seal_execution_request(
      owner.get().session("ordinary"));
  const auto& source = ordinary.resources().binding(
      pkgexec::resource_slot::named(pkgexec::resource_role::source_tree,
                                    "sources"));
  auto inputs = owner.get().package_inputs();
  inputs[0].resource = source.resource();
  auto paths = owner.get().paths("alias");
  auto aliased = pkgbuild_exec::admitted_build_session::admit(
      owner.get().request, owner.get().materialization, std::move(inputs),
      paths, owner.get().execution_identity());

  expect_error(pkgbuild_exec::error_code::package_input_mismatch, [&] {
    (void)pkgbuild_exec::prepare(aliased);
  });
  require(!fs::exists(paths.session_root) &&
              !fs::exists(paths.package_output_root) &&
              !fs::exists(paths.artifact_path),
          "resource-alias refusal occurred after effectful preparation");
}

} // namespace

int main()
{
  try {
    projects_exact_build_contract();
    host_effect_paths_do_not_change_request_authority();
    semantic_resource_identity_changes_request();
    adapter_owned_resource_alias_fails_before_effects();
  } catch (const std::exception& value) {
    std::cerr << value.what() << '\n';
    return 1;
  }
  return 0;
}
