// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <libpkgbuild-exec/error.h>

namespace pkgbuild_exec_test {

inline void require(bool condition, std::string_view message)
{
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

template<typename Function>
void expect_error(pkgbuild_exec::error_code code, Function&& function)
{
  try {
    std::forward<Function>(function)();
  } catch (const pkgbuild_exec::error& value) {
    require(value.code() == code, "adapter threw the wrong error code");
    return;
  }
  throw std::runtime_error("adapter did not reject an invalid operation");
}

} // namespace pkgbuild_exec_test
