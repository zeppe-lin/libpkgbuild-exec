// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include <libpkgbuild-exec/libpkgbuild-exec.h>

#include <libpkgimage/libarchive_backend.h>

#include <openssl/evp.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

void require(bool condition, std::string_view message)
{
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

std::string sha256_text(std::string_view bytes)
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

void write_file(const fs::path& path, std::string_view bytes, mode_t mode = 0644)
{
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  require(static_cast<bool>(output), "cannot create fixture file");
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  output.close();
  require(static_cast<bool>(output), "cannot write fixture file");
  require(::chmod(path.c_str(), mode) == 0, "cannot chmod fixture file");
}

std::string read_file(const fs::path& path)
{
  std::ifstream input(path, std::ios::binary);
  require(static_cast<bool>(input), "cannot open fixture file");
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void set_mtime(const fs::path& path, bool symlink = false)
{
  const timespec value[2]{{1700000000, 123456789},
                          {1700000000, 123456789}};
  require(::utimensat(AT_FDCWD, path.c_str(), value,
                      symlink ? AT_SYMLINK_NOFOLLOW : 0) == 0,
          "cannot set fixture mtime");
}

pkgsource::declaration_provenance at(std::string path, std::uint32_t line)
{
  return pkgsource::declaration_provenance(
      "recipe.yml", std::move(path), line, 1);
}

pkgsource::source_snapshot source_snapshot(
    std::string_view first_digest, std::string_view second_digest)
{
  using namespace pkgsource;
  return seal_source(
      source_origin("recipe.yml"), source_syntax::recipe_yaml_v1,
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
                         std::string(second_digest))),
          },
          program(program_language::posix_shell,
                  "install -Dm755 payload /build/package/usr/bin/payload\n"),
          {
              requirement_declaration(
                  requirement_scope::build(),
                  requirement_subject(package_reference("tool")),
                  at("requirements.build[0]", 12)),
              requirement_declaration(
                  requirement_scope::check(),
                  requirement_subject(package_reference("checker")),
                  at("requirements.check[0]", 14)),
          },
          {}, architecture_requirements({}, {}), at("$", 1)),
      profile_catalog::seal({}));
}

pkgbuild::materialized_package_input package_input(
    pkgbuild::input_scope scope, const char* name, char seed)
{
  const std::string hex(64U, seed);
  const auto package = pkgsource::package_reference(name);
  return pkgbuild::materialized_package_input(
      pkgbuild::resolved_package_input::make(
          scope, package, pkgsource::package_release(package, "1.0", 1),
          pkgsource::source_snapshot_identity::from_sha256(hex),
          pkgbuild::build_result_identity::from_sha256(hex),
          pkgbuild::artifact_identity::from_sha256(hex)),
      pkgbuild::input_tree_identity::from_sha256(hex));
}

pkgbuild::build_request build_request(
    const pkgsource::source_snapshot& source,
    const std::vector<pkgbuild::materialized_package_input>& inputs)
{
  std::vector<pkgbuild::materialized_source> materials;
  for (const auto& input : source.recipe().sources()) {
    materials.push_back(pkgbuild::materialized_source::verify(
        input, pkgbuild::sha256_digest(input.content_digest().hex())));
  }
  return pkgbuild::build_request::seal(
      source, std::move(materials), inputs,
      pkgsource::architecture_reference("x86_64"),
      pkgsource::architecture_reference("x86_64"),
      pkgbuild::build_policy::make(
          pkgbuild::environment_policy::hermetic(4, 0022, 1700000000)));
}

std::string environment_value(const pkgexec::environment_policy& environment,
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

enum class backend_mode {
  succeed,
  fail,
  unsupported_payload,
  wrong_request,
  throw_exception,
};

class fixture_backend final : public pkgexec::execution_backend {
public:
  explicit fixture_backend(backend_mode mode) : mode_(mode) {}

  pkgexec::backend_capability_profile capabilities() const override
  {
    return ::capabilities();
  }

  pkgexec::execution_result execute(
      const pkgexec::execution_request& request,
      const pkgexec::execution_resources& resources) override
  {
    if (mode_ == backend_mode::throw_exception) {
      throw std::runtime_error("fixture backend exception");
    }

    inspect_request(request, resources);
    if (mode_ == backend_mode::fail) {
      return pkgexec::execution_result::failed_before_start(
          request, capabilities(),
          pkgexec::execution_failure_kind::backend_unsupported, {},
          "fixture execution failure");
    }

    const auto output_slot = pkgexec::resource_slot::singleton(
        pkgexec::resource_role::package_output_root);
    const auto& output_binding = request.resources().binding(output_slot);
    const fs::path output =
        resources.materialization(output_binding.resource()).host_path();
    emit_payload(output, mode_ == backend_mode::unsupported_payload);

    auto evidence_request = request;
    if (mode_ == backend_mode::wrong_request) {
      evidence_request = pkgexec::execution_request::seal(
          request.program(), pkgexec::execution_purpose::check(),
          request.interpreter(), request.root_view(), request.resources(),
          request.environment(), request.credentials(), request.limits(),
          request.cancellation());
    }
    return pkgexec::execution_result::succeeded(
        evidence_request, capabilities(), request.interpreter(),
        pkgexec::stream_capture::retained("fixture stdout\n"),
        pkgexec::stream_capture::retained("fixture stderr\n"),
        capabilities().guarantees(), "fixture success");
  }

private:
  static void inspect_request(
      const pkgexec::execution_request& request,
      const pkgexec::execution_resources& resources)
  {
    require(request.purpose().kind() ==
                pkgexec::execution_purpose_kind::build,
            "adapter did not request build execution");
    require(request.environment().network() ==
                pkgexec::network_policy::denied,
            "adapter did not deny networking");
    require(request.environment().standard_input() ==
                pkgexec::stdin_policy::closed,
            "adapter did not close standard input");
    require(request.environment().standard_output() ==
                pkgexec::stream_policy::capture_complete &&
                request.environment().standard_error() ==
                    pkgexec::stream_policy::capture_complete,
            "adapter did not request complete stream capture");
    require(environment_value(request.environment(), "PKG_SOURCE_ROOT") ==
                "/build/source",
            "source-root environment is wrong");
    require(environment_value(request.environment(), "PKG_BUILD_INPUTS") ==
                "tool",
            "build-input environment is wrong");
    require(environment_value(request.environment(), "PKG_CHECK_INPUTS") ==
                "checker",
            "check-input environment is wrong");
    require(environment_value(request.environment(), "PKG_JOBS") == "4",
            "parallelism environment is wrong");

    const auto source_slot = pkgexec::resource_slot::named(
        pkgexec::resource_role::source_tree, "sources");
    const auto& source_binding = request.resources().binding(source_slot);
    require(source_binding.access() == pkgexec::resource_access::read_only &&
                source_binding.mount_point().string() == "/build/source",
            "source binding is wrong");
    const fs::path source =
        resources.materialization(source_binding.resource()).host_path();
    require(read_file(source / "payload") == "source bytes\n",
            "staged source bytes are wrong");
    require(read_file(source / "archive.tar") == "raw archive bytes\n",
            "source object was transformed");
    require((fs::status(source / "payload").permissions() &
             fs::perms::owner_write) == fs::perms::none,
            "staged source object is writable");

    for (const auto role : {
             pkgexec::resource_role::build_workspace,
             pkgexec::resource_role::package_output_root,
             pkgexec::resource_role::private_temporary_root,
         }) {
      const auto slot = pkgexec::resource_slot::singleton(role);
      const auto& binding = request.resources().binding(slot);
      const fs::path path =
          resources.materialization(binding.resource()).host_path();
      struct stat status {};
      require(::stat(path.c_str(), &status) == 0,
              "cannot inspect writable fixture resource");
      require(static_cast<std::uint64_t>(status.st_uid) ==
                  request.credentials().user_id() &&
                  static_cast<std::uint64_t>(status.st_gid) ==
                      request.credentials().group_id(),
              "writable resource ownership differs from execution credentials");
    }

    const auto build_slot = pkgexec::resource_slot::named(
        pkgexec::resource_role::build_input_tree, "tool");
    const auto check_slot = pkgexec::resource_slot::named(
        pkgexec::resource_role::check_input_tree, "checker");
    require(request.resources().binding(build_slot).mount_point().string() ==
                "/build/inputs/build/tool",
            "build input mount is wrong");
    require(request.resources().binding(check_slot).mount_point().string() ==
                "/build/inputs/check/checker",
            "check input mount is wrong");
  }

  static void emit_payload(const fs::path& root, bool unsupported)
  {
    fs::create_directories(root / "usr/bin");
    write_file(root / "usr/bin/payload", "artifact bytes\n", 0755);
    require(::link((root / "usr/bin/payload").c_str(),
                   (root / "usr/bin/payload-hard").c_str()) == 0,
            "cannot create fixture hard link");
    require(::symlink("payload",
                      (root / "usr/bin/payload-link").c_str()) == 0,
            "cannot create fixture symbolic link");
    require(::mkfifo((root / "usr/bin/pipe").c_str(), 0644) == 0,
            "cannot create fixture fifo");

    if (unsupported) {
      const int descriptor = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
      require(descriptor >= 0, "cannot create fixture socket");
      sockaddr_un address{};
      address.sun_family = AF_UNIX;
      const std::string path = (root / "usr/bin/socket").string();
      require(path.size() < sizeof(address.sun_path),
              "fixture socket path is too long");
      std::copy(path.begin(), path.end(), address.sun_path);
      require(::bind(descriptor, reinterpret_cast<sockaddr*>(&address),
                     sizeof(address)) == 0,
              "cannot bind fixture socket");
      require(::close(descriptor) == 0, "cannot close fixture socket");
    }

    set_mtime(root / "usr/bin/payload");
    set_mtime(root / "usr/bin/payload-hard");
    set_mtime(root / "usr/bin/payload-link", true);
    set_mtime(root / "usr/bin/pipe");
    if (unsupported) {
      set_mtime(root / "usr/bin/socket");
    }
    set_mtime(root / "usr/bin");
    set_mtime(root / "usr");
  }

  backend_mode mode_;
};

// The semantic types do not have empty constructors. Keep fixture construction
// explicit without manufacturing placeholder authority objects.
class fixture_owner final {
public:
  explicit fixture_owner(std::string suffix)
  {
    const fs::path root = fs::temp_directory_path() /
        ("libpkgbuild-exec-test-" + std::to_string(::getpid()) + "-" + suffix);
    fs::remove_all(root);
    fs::create_directories(root / "local");
    fs::create_directories(root / "store");
    fs::create_directories(root / "root");
    fs::create_directories(root / "inputs/tool");
    fs::create_directories(root / "inputs/checker");
    write_file(root / "local/payload", "source bytes\n");
    write_file(root / "local/archive.tar", "raw archive bytes\n");
    write_file(root / "inputs/tool/tool", "tool tree\n", 0555);
    write_file(root / "inputs/checker/checker", "checker tree\n", 0555);

    auto source = source_snapshot(sha256_text("source bytes\n"),
                                  sha256_text("raw archive bytes\n"));
    auto materialization = pkgfetch::materialize(
        pkgfetch::materialization_request::seal(
            source, root / "local", root / "store"));
    std::vector<pkgbuild::materialized_package_input> inputs{
        package_input(pkgbuild::input_scope::build, "tool", 'c'),
        package_input(pkgbuild::input_scope::check, "checker", 'd'),
    };
    auto request = build_request(source, inputs);
    value_ = std::make_unique<state>(state{
        root, std::move(source), std::move(materialization),
        std::move(inputs), std::move(request)});
  }

  ~fixture_owner()
  {
    if (value_) {
      std::error_code ignored;
      fs::remove_all(value_->root, ignored);
    }
  }

  struct state final {
    fs::path root;
    pkgsource::source_snapshot source;
    pkgfetch::source_materialization materialization;
    std::vector<pkgbuild::materialized_package_input> inputs;
    pkgbuild::build_request request;

    pkgbuild_exec::admitted_build_session session(
        std::string name = "one") const
    {
      std::vector<pkgbuild_exec::package_input_tree> trees{
          {inputs[0].resolved().identity(), inputs[0].tree(),
           root / "inputs/tool"},
          {inputs[1].resolved().identity(), inputs[1].tree(),
           root / "inputs/checker"},
      };
      return pkgbuild_exec::admitted_build_session::admit(
          request, materialization, std::move(trees),
          {
              pkgexec::root_view_identity::from_sha256(
                  std::string(64U, 'b')),
              root / "root",
              root / ("session-" + name),
              root / ("package-" + name),
              root / ("artifact-" + name + ".tar"),
          },
          {
              pkgexec::interpreter_identity::from_sha256(
                  std::string(64U, 'c')),
              static_cast<std::uint64_t>(::getuid()),
              static_cast<std::uint64_t>(::getgid()),
              {},
          });
    }
  };

  state& get() { return *value_; }
  const state& get() const { return *value_; }

private:
  std::unique_ptr<state> value_;
};

template<typename Function>
void expect_error(pkgbuild_exec::error_code code, Function&& function)
{
  try {
    function();
  } catch (const pkgbuild_exec::error& value) {
    require(value.code() == code, "adapter threw the wrong error code");
    return;
  }
  throw std::runtime_error("adapter did not reject an invalid operation");
}

void test_prepare_contract()
{
  fixture_owner owner("prepare");
  auto session = owner.get().session();
  auto prepared = pkgbuild_exec::prepare(session);
  require(prepared.request.program() == owner.get().request.build_program(),
          "execution program differs from sealed build program");
  require(prepared.request.environment().network() ==
              pkgexec::network_policy::denied,
          "prepared build networking is not denied");
  require(prepared.request.credentials().user_id() == ::getuid() &&
              prepared.request.credentials().group_id() == ::getgid(),
          "prepared credentials differ from the session");
  require(prepared.request.limits().empty(),
          "0.1 unexpectedly invented resource limits");
  require(prepared.request.cancellation().mode() ==
              pkgexec::cancellation_mode::disabled,
          "0.1 unexpectedly invented cancellation");
  require(read_file(prepared.source_tree / "payload") == "source bytes\n",
          "source staging failed");
  require(read_file(prepared.source_tree / "archive.tar") ==
              "raw archive bytes\n",
          "raw source material was transformed");
  require((fs::status(prepared.source_tree).permissions() &
           fs::perms::owner_write) == fs::perms::none,
          "source tree is writable");
}

void test_source_revalidation()
{
  fixture_owner owner("source-revalidation");
  const fs::path object = owner.get().materialization.objects()[0].object_path();
  require(::chmod(object.c_str(), 0644) == 0,
          "cannot make source fixture mutable");
  write_file(object, "mutated bytes\n", 0644);
  expect_error(pkgbuild_exec::error_code::source_staging_failed, [&] {
    (void)pkgbuild_exec::prepare(owner.get().session());
  });
}

void test_unsafe_layout_rejected()
{
  fixture_owner owner("unsafe-layout");
  const auto& state = owner.get();
  std::vector<pkgbuild_exec::package_input_tree> trees{
      {state.inputs[0].resolved().identity(), state.inputs[0].tree(),
       state.root / "inputs/tool"},
      {state.inputs[1].resolved().identity(), state.inputs[1].tree(),
       state.root / "inputs/checker"},
  };
  expect_error(pkgbuild_exec::error_code::unsafe_path_layout, [&] {
    (void)pkgbuild_exec::admitted_build_session::admit(
        state.request, state.materialization, trees,
        {
            pkgexec::root_view_identity::from_sha256(std::string(64U, 'b')),
            state.root / "root", state.root / "session",
            state.root / "package", state.root / "session/artifact.tar",
        },
        {
            pkgexec::interpreter_identity::from_sha256(std::string(64U, 'c')),
            static_cast<std::uint64_t>(::getuid()),
            static_cast<std::uint64_t>(::getgid()), {},
        });
  });
}

void test_package_input_mismatch()
{
  fixture_owner owner("input-mismatch");
  const auto& state = owner.get();
  std::vector<pkgbuild_exec::package_input_tree> trees{
      {state.inputs[0].resolved().identity(), state.inputs[0].tree(),
       state.root / "inputs/tool"},
  };
  expect_error(pkgbuild_exec::error_code::package_input_mismatch, [&] {
    (void)pkgbuild_exec::admitted_build_session::admit(
        state.request, state.materialization, trees,
        {
            pkgexec::root_view_identity::from_sha256(std::string(64U, 'b')),
            state.root / "root", state.root / "session",
            state.root / "package", state.root / "artifact.tar",
        },
        {
            pkgexec::interpreter_identity::from_sha256(std::string(64U, 'c')),
            static_cast<std::uint64_t>(::getuid()),
            static_cast<std::uint64_t>(::getgid()), {},
        });
  });
}

void test_credential_bounds()
{
  if (std::numeric_limits<uid_t>::max() ==
      std::numeric_limits<std::uint64_t>::max()) {
    return;
  }
  fixture_owner owner("credential-bounds");
  const auto& state = owner.get();
  std::vector<pkgbuild_exec::package_input_tree> trees{
      {state.inputs[0].resolved().identity(), state.inputs[0].tree(),
       state.root / "inputs/tool"},
      {state.inputs[1].resolved().identity(), state.inputs[1].tree(),
       state.root / "inputs/checker"},
  };
  expect_error(pkgbuild_exec::error_code::invalid_session, [&] {
    (void)pkgbuild_exec::admitted_build_session::admit(
        state.request, state.materialization, trees,
        {
            pkgexec::root_view_identity::from_sha256(std::string(64U, 'b')),
            state.root / "root", state.root / "session",
            state.root / "package", state.root / "artifact.tar",
        },
        {
            pkgexec::interpreter_identity::from_sha256(std::string(64U, 'c')),
            static_cast<std::uint64_t>(std::numeric_limits<uid_t>::max()),
            static_cast<std::uint64_t>(::getgid()), {},
        });
  });
}

void test_execution_failure()
{
  fixture_owner owner("execution-failure");
  fixture_backend backend(backend_mode::fail);
  auto result = pkgbuild_exec::execute(owner.get().session(), backend);
  require(result.execution().status() == pkgexec::execution_status::failed,
          "execution failure was lost");
  require(result.build().outcome() == pkgbuild::build_outcome::failed &&
              result.build().failure_evidence().has_value(),
          "execution failure did not seal a failed build");
  require(!result.sealing_failure().has_value(),
          "execution failure was misclassified as result sealing");
  require(result.diagnostic() == "fixture execution failure",
          "execution diagnostic was not retained");
}

void test_success_and_determinism()
{
  fixture_owner owner("success");
  fixture_backend backend(backend_mode::succeed);
  auto first = pkgbuild_exec::execute(owner.get().session("first"), backend);
  auto second = pkgbuild_exec::execute(owner.get().session("second"), backend);

  require(first.build().outcome() == pkgbuild::build_outcome::succeeded,
          first.diagnostic());
  require(first.build().payload() &&
              first.build().payload()->entries().size() == 6U,
          "complete payload was not sealed");
  require(first.build().artifact() && first.artifact_inspection(),
          "artifact evidence is incomplete");
  require(!first.sealing_failure() && first.diagnostic().empty(),
          "successful sealing retained a failure");
  require(first.execution().standard_output() &&
              first.execution().standard_output()->material() &&
              *first.execution().standard_output()->material() ==
                  "fixture stdout\n",
          "execution stdout evidence was lost");

  const fs::path first_path = owner.get().root / "artifact-first.tar";
  const fs::path second_path = owner.get().root / "artifact-second.tar";
  require(fs::is_regular_file(first_path) && fs::is_regular_file(second_path),
          "artifact was not published");
  require((fs::status(first_path).permissions() & fs::perms::owner_write) ==
              fs::perms::none,
          "published artifact is writable");
  require(first.build().artifact()->complete_digest() ==
              second.build().artifact()->complete_digest() &&
              read_file(first_path) == read_file(second_path),
          "identical payloads produced different package_tar_v1 bytes");
  require(first.build() == second.build(),
          "effect coordinates contaminated build-result identity");
}

void test_publication_is_non_replacing()
{
  fixture_owner owner("publication");
  const fs::path destination = owner.get().root / "artifact-one.tar";
  write_file(destination, "existing artifact\n", 0444);
  fixture_backend backend(backend_mode::succeed);
  auto result = pkgbuild_exec::execute(owner.get().session(), backend);
  require(result.build().outcome() == pkgbuild::build_outcome::failed,
          "publication conflict did not fail the build");
  require(result.sealing_failure() ==
              pkgbuild_exec::result_sealing_failure_kind::artifact_publication,
          "publication conflict has the wrong stable failure kind");
  require(read_file(destination) == "existing artifact\n",
          "publication replaced authoritative existing bytes");
}

void test_unsupported_payload_fails_sealing()
{
  fixture_owner owner("unsupported-payload");
  fixture_backend backend(backend_mode::unsupported_payload);
  auto result = pkgbuild_exec::execute(owner.get().session(), backend);
  require(result.build().outcome() == pkgbuild::build_outcome::failed,
          "unsupported output object did not fail the build");
  require(result.sealing_failure() ==
              pkgbuild_exec::result_sealing_failure_kind::payload_inspection,
          "unsupported object has the wrong sealing failure kind");
  require(!fs::exists(owner.get().root / "artifact-one.tar"),
          "failed sealing published an artifact");
}

void test_backend_contract_violations()
{
  fixture_owner owner("backend-contract");
  fixture_backend wrong(backend_mode::wrong_request);
  expect_error(pkgbuild_exec::error_code::backend_contract_violation, [&] {
    (void)pkgbuild_exec::execute(owner.get().session("wrong"), wrong);
  });

  fixture_backend throwing(backend_mode::throw_exception);
  expect_error(pkgbuild_exec::error_code::backend_contract_violation, [&] {
    (void)pkgbuild_exec::execute(owner.get().session("throw"), throwing);
  });
}

} // namespace

int main()
{
  try {
    test_prepare_contract();
    test_source_revalidation();
    test_unsafe_layout_rejected();
    test_package_input_mismatch();
    test_credential_bounds();
    test_execution_failure();
    test_success_and_determinism();
    test_publication_is_non_replacing();
    test_unsupported_payload_fails_sealing();
    test_backend_contract_violations();
  } catch (const std::exception& value) {
    std::cerr << value.what() << '\n';
    return 1;
  }
  return 0;
}
