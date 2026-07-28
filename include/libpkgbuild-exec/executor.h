// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

/*! \file executor.h
 *  \brief Build request translation, execution, inspection, and result sealing.
 */
#pragma once

#include <libpkgbuild-exec/model.h>

namespace pkgbuild_exec {

/*! \brief Stage exact source bytes and seal the backend-neutral execution call. */
[[nodiscard]] prepared_execution prepare(
    const admitted_build_session& session);

/*! \brief Execute one admitted build and retain execution plus build evidence. */
[[nodiscard]] build_execution_result execute(
    const admitted_build_session& session,
    pkgexec::execution_backend& backend);

} // namespace pkgbuild_exec
