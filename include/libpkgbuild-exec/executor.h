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

/*! \brief Project the attempt-private sealed-artifact path without effects. */
[[nodiscard]] std::filesystem::path project_sealed_artifact_path(
    const admitted_build_session& session);

/*! \brief Stage exact source bytes and bind call-scoped host resources. */
[[nodiscard]] prepared_execution prepare(
    const admitted_build_session& session);

/*! \brief Execute and seal one build beneath its private session root.
 *
 * Successful return retains the exact verified package archive at
 * `project_sealed_artifact_path(session)`.  The caller-visible artifact path
 * is not mutated.  This is the restartable execution primitive for durable
 * controllers: terminal build evidence can be persisted before publication.
 */
[[nodiscard]] build_execution_result execute_sealed(
    const admitted_build_session& session,
    pkgexec::execution_backend& backend);

/*! \brief Publish the exact retained artifact authorized by terminal result.
 *
 * Publication never replaces existing bytes.  If the final path already
 * exists, it is accepted only when present observation proves the same exact
 * byte count and SHA-256 retained by `result`.  The function performs no
 * build execution and therefore remains usable during evidence-backed
 * restart even when execution authority is unavailable.
 */
void publish_sealed_artifact(
    const admitted_build_session& session,
    const build_execution_result& result);

/*! \brief Execute, seal, publish, and retain one admitted build result.
 *
 * This one-shot convenience preserves the ordinary adapter contract. Durable
 * controllers that must survive process death between execution and public
 * publication should use `execute_sealed()` followed by
 * `publish_sealed_artifact()` only after terminal evidence is durable.
 */
[[nodiscard]] build_execution_result execute(
    const admitted_build_session& session,
    pkgexec::execution_backend& backend);

} // namespace pkgbuild_exec
