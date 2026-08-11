// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include <libpkgbuild-exec/libpkgbuild-exec.h>

#include <cstddef>

#if !defined(__x86_64__)
#error "libpkgbuild-exec 2 ABI layout qualification is x86-64 specific"
#endif

static_assert(sizeof(void*) == 8, "Zeppe-Lin x86-64 ABI qualification required");
static_assert(alignof(void*) == 8, "unexpected x86-64 pointer alignment");

static_assert(sizeof(pkgexec::resource_identity) == 32);
static_assert(sizeof(pkgexec::root_view_identity) == 32);
static_assert(sizeof(pkgexec::interpreter_identity) == 32);
static_assert(sizeof(pkgexec::execution_request) == 720);
static_assert(sizeof(pkgexec::execution_resources) == 96);
static_assert(sizeof(pkgexec::execution_result) == 1160);
static_assert(sizeof(pkgexec::backend_capability_profile) == 88);

static_assert(sizeof(pkgbuild_exec::package_input_resource) == 104);
static_assert(sizeof(pkgbuild_exec::session_paths) == 192);
static_assert(sizeof(pkgbuild_exec::execution_identity) == 72);
static_assert(sizeof(pkgbuild_exec::admitted_build_session) == 1080);
static_assert(sizeof(pkgbuild_exec::prepared_execution) == 936);
static_assert(sizeof(pkgbuild_exec::build_execution_result) == 1240);

int main() { return 0; }
