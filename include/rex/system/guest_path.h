/**
 * @file        rex/system/guest_path.h
 * @brief       Guest path normalization for Xbox 360 VFS paths
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#pragma once

#include <algorithm>
#include <string>
#include <string_view>

namespace rex::system {

inline std::string NormalizeGuestPath(std::string_view path) {
  std::string result(path);
  // Strip device prefix (e.g., "game:\")
  auto colon = result.find(':');
  if (colon != std::string::npos && colon + 1 < result.size() &&
      (result[colon + 1] == '\\' || result[colon + 1] == '/')) {
    result = result.substr(colon + 2);
  }
  // Backslash to forward slash
  std::replace(result.begin(), result.end(), '\\', '/');
  // Lowercase
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

}  // namespace rex::system
