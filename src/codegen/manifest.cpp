/**
 * @file        codegen/manifest.cpp
 * @brief       Manifest TOML parser for multi-binary projects
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <rex/codegen/manifest.h>

#include <toml++/toml.hpp>

#include <rex/logging.h>

namespace rex::codegen {

std::optional<ManifestConfig> ManifestConfig::Load(const std::filesystem::path& path) {
  toml::table tbl;
  try {
    tbl = toml::parse_file(path.string());
  } catch (const toml::parse_error& err) {
    REXLOG_ERROR("Failed to parse manifest {}: {}", path.string(), err.what());
    return std::nullopt;
  }

  ManifestConfig manifest;
  manifest.manifestDir = path.parent_path();

  // [project]
  if (auto project = tbl["project"].as_table()) {
    manifest.projectName = (*project)["projectName"].value_or<std::string>("");
  }
  if (manifest.projectName.empty()) {
    REXLOG_ERROR("Manifest missing [project].projectName");
    return std::nullopt;
  }

  // [entrypoint]
  if (auto ep = tbl["entrypoint"].as_table()) {
    auto configPath = (*ep)["config"].value_or<std::string>("");
    if (configPath.empty()) {
      REXLOG_ERROR("Manifest missing [entrypoint].config");
      return std::nullopt;
    }
    manifest.entrypointConfig = manifest.manifestDir / configPath;
  } else {
    REXLOG_ERROR("Manifest missing [entrypoint] section");
    return std::nullopt;
  }

  // [[modules]]
  if (auto modules = tbl["modules"].as_array()) {
    for (const auto& mod : *modules) {
      auto* modTbl = mod.as_table();
      if (!modTbl)
        continue;

      ManifestModuleEntry entry;
      entry.config = manifest.manifestDir / (*modTbl)["config"].value_or<std::string>("");
      entry.guestPath = (*modTbl)["guestPath"].value_or<std::string>("");

      if (entry.config.filename().empty() || entry.guestPath.empty()) {
        REXLOG_ERROR("Manifest [[modules]] entry missing config or guestPath");
        return std::nullopt;
      }
      manifest.modules.push_back(std::move(entry));
    }
  }

  return manifest;
}

bool ManifestConfig::IsManifest(const std::filesystem::path& path) {
  try {
    auto tbl = toml::parse_file(path.string());
    return tbl.contains("entrypoint");
  } catch (...) {
    return false;
  }
}

}  // namespace rex::codegen
