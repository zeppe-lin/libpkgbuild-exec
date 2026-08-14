// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include <sys/types.h>

namespace pkgbuild_exec::detail {

class source_archive_backend {
public:
  virtual ~source_archive_backend() = default;
  virtual void unpack(int source_fd, int destination_fd,
                      mode_t file_creation_mask) const = 0;
};

[[nodiscard]] std::unique_ptr<source_archive_backend>
make_libarchive_source_archive_backend();

} // namespace pkgbuild_exec::detail
