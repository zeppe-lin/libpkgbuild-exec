#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
root=$1
api=$root/include/libpkgbuild-exec/result_codec.h
codec=$root/src/result_codec.cpp
model=$root/include/libpkgbuild-exec/model.h
meson=$root/src/meson.build

# The adapter retains its owner evidence and delegates subordinate execution.
grep -q 'encode_build_execution_result' "$api"
grep -q 'decode_build_execution_result' "$api"
grep -q 'pkgbuild::build_request build_request' "$api"
grep -q 'pkgexec::execution_request execution_request' "$api"
grep -q 'pkgexec::backend_capability_profile backend' "$api"
grep -q 'pkgexec::encode_execution_result' "$codec"
grep -q 'pkgexec::decode_execution_result' "$codec"
grep -q 'class codec_access' "$model"

# Durable decoding is evidence-only and never prepares or executes a session.
! grep -q 'admitted_build_session' "$api" "$codec"
! grep -q 'execution_backend' "$api" "$codec"
! grep -q 'filesystem::path' "$api" "$codec"
! grep -E -q 'prepare\(|execute\(' "$codec"

# Corruption, authority mismatch, and canonical re-encoding are distinct gates.
grep -q 'checksum mismatch' "$codec"
grep -q 'authority_mismatch' "$codec"
grep -q 'is not canonical' "$codec"
grep -q 'require_receipt_binding' "$codec"

# Installation and dependency metadata expose the exact new boundary.
grep -q "result_codec.cpp" "$meson"
grep -q "result_codec.h" "$meson"
grep -q "libpkgexec >= 2.0.0" "$meson"
