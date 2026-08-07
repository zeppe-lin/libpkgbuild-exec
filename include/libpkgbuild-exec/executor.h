// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

/*! \file executor.h
 *  \brief Build request translation, execution, inspection, and result sealing.
 */
#pragma once

#include <libpkgbuild-exec/model.h>

namespace pkgbuild_exec {

/*! \brief Seal the backend-neutral request without touching host resources.
 *
 * This pure projection is the canonical way to reproduce the exact execution
 * request retained by durable build evidence.  It validates the admitted
 * logical/resource bindings but performs no directory creation, removal,
 * staging, ownership change, or artifact mutation.
 */
[[nodiscard]] pkgexec::execution_request seal_execution_request(
    const admitted_build_session& session);

/*! \brief Project adapter-owned prepared paths without host effects. */
[[nodiscard]] prepared_paths project_prepared_paths(
    const admitted_build_session& session);

/*! \brief Stage exact source bytes and bind call-scoped host resources. */
[[nodiscard]] prepared_execution prepare(
    const admitted_build_session& session);

/*! \brief Execute one admitted build and retain execution plus build evidence. */
[[nodiscard]] build_execution_result execute(
    const admitted_build_session& session,
    pkgexec::execution_backend& backend);

} // namespace pkgbuild_exec
