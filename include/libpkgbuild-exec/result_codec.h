// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

/*! \file result_codec.h
 *  \brief Versioned durable encoding for retained build-execution evidence.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <libpkgbuild-exec/model.h>

namespace pkgbuild_exec {

/*! \brief Current canonical build-execution-result encoding. */
inline constexpr std::uint16_t build_execution_result_encoding_version = 1;
/*! \brief Hard refusal bound for one durable build-execution record. */
inline constexpr std::size_t maximum_build_execution_result_encoding_size =
    256U * 1024U * 1024U;

using build_execution_result_encoding = std::vector<std::uint8_t>;

/*! \brief Encode exact adapter-owned build evidence into canonical bytes. */
[[nodiscard]] build_execution_result_encoding encode_build_execution_result(
    const build_execution_result& result);

/*! \brief Decode evidence under exact caller-supplied semantic authorities.
 *
 * The build request, execution request, and backend profile bodies are not
 * reconstructed from identities. The decoder requires those exact authorities,
 * delegates execution evidence to libpkgexec's durable codec, rebuilds the
 * build result through public invariant-enforcing factories, and verifies every
 * retained identity and canonical byte.
 */
[[nodiscard]] build_execution_result decode_build_execution_result(
    const build_execution_result_encoding& encoding,
    pkgbuild::build_request build_request,
    pkgexec::execution_request execution_request,
    pkgexec::backend_capability_profile backend);

} // namespace pkgbuild_exec
