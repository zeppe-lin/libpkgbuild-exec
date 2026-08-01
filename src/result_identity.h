// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <libpkgbuild-exec/model.h>

namespace pkgbuild_exec::detail {

[[nodiscard]] pkgbuild::execution_evidence_identity execution_evidence_identity(
    const pkgexec::execution_result& result);
[[nodiscard]] pkgbuild::failure_evidence_identity execution_failure_identity(
    const pkgexec::execution_result& result);
[[nodiscard]] pkgbuild::failure_evidence_identity sealing_failure_identity(
    const pkgexec::execution_result& result,
    result_sealing_failure_kind kind);

} // namespace pkgbuild_exec::detail
