/**
 * @file        rexglue/commands/init_command.cpp
 * @brief       Project initialization command implementation
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include "init_command.h"
#include "template_utils.h"

#include <filesystem>
#include <fstream>

#include <rex/codegen/manifest.h>
#include <rex/codegen/template_registry.h>
#include <rex/logging.h>
#include <rex/result.h>
#include <rex/version.h>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace rexglue::cli {

using rex::Err;
using rex::Error;
using rex::ErrorCategory;
using rex::Ok;

Result<void> InitProject(const InitOptions& opts, const CliContext& ctx) {
  (void)ctx;  // Currently unused

  // Validate required options
  if (opts.app_name.empty()) {
    return Err<void>(ErrorCategory::Config, "--app_name is required");
  }
  if (opts.app_root.empty()) {
    return Err<void>(ErrorCategory::Config, "--app_root is required");
  }

  // Validate and parse app name
  std::string validation_error;
  if (!validate_app_name(opts.app_name, validation_error)) {
    return Err<void>(ErrorCategory::Config, validation_error);
  }
  auto names = parse_app_name(opts.app_name);

  rex::codegen::TemplateRegistry registry;
  if (!opts.template_dir.empty())
    registry.loadOverrides(opts.template_dir);

  nlohmann::json data = {{"names", names_to_json(names)}, {"sdk_version", REXGLUE_VERSION_NUMERIC}};
  std::string jsonStr = data.dump();

  fs::path root = fs::absolute(opts.app_root);

  REXLOG_INFO("Initializing project '{}' at: {}", names.snake_case, root.string());
  REXLOG_INFO("Mode: {}", opts.sdk_example ? "SDK example" : "standalone");

  // Check if directory exists and has contents
  if (fs::exists(root)) {
    if (!fs::is_directory(root)) {
      return Err<void>(ErrorCategory::IO, "Path exists but is not a directory: " + root.string());
    }

    bool has_contents = false;
    for (const auto& entry : fs::directory_iterator(root)) {
      (void)entry;
      has_contents = true;
      break;
    }

    if (has_contents && !opts.force) {
      return Err<void>(ErrorCategory::IO,
                       "Directory is not empty. Use --force to overwrite: " + root.string());
    }
  }

  // Create directory structure
  REXLOG_INFO("Creating directory structure...");

  std::error_code ec;
  fs::create_directories(root, ec);
  if (ec) {
    return Err<void>(ErrorCategory::IO, "Failed to create root directory: " + ec.message());
  }

  fs::create_directories(root / "src", ec);
  if (ec) {
    return Err<void>(ErrorCategory::IO, "Failed to create src directory: " + ec.message());
  }

  fs::create_directories(root / "generated", ec);
  if (ec) {
    return Err<void>(ErrorCategory::IO, "Failed to create generated directory: " + ec.message());
  }

  // Generate files
  REXLOG_INFO("Generating project files...");

  if (!write_file(root / "CMakeLists.txt", registry.render("init/cmakelists", jsonStr))) {
    return Err<void>(ErrorCategory::IO, "Failed to write CMakeLists.txt");
  }
  REXLOG_DEBUG("  Created CMakeLists.txt");

  // generated/rexglue.cmake (SDK-managed)
  if (!write_file(root / "generated" / "rexglue.cmake",
                  registry.render("init/rexglue_cmake", jsonStr))) {
    return Err<void>(ErrorCategory::IO, "Failed to write generated/rexglue.cmake");
  }
  REXLOG_DEBUG("  Created generated/rexglue.cmake");

  if (!write_file(root / "src" / "main.cpp", registry.render("init/main_cpp", jsonStr))) {
    return Err<void>(ErrorCategory::IO, "Failed to write main.cpp");
  }
  REXLOG_DEBUG("  Created src/main.cpp");

  // src/{name}_app.h (user-owned)
  std::string app_header_filename = names.snake_case + "_app.h";
  if (!write_file(root / "src" / app_header_filename,
                  registry.render("init/app_header", jsonStr))) {
    return Err<void>(ErrorCategory::IO, "Failed to write src/" + app_header_filename);
  }
  REXLOG_DEBUG("  Created src/{}", app_header_filename);

  std::string config_filename = names.snake_case + "_config.toml";
  if (!write_file(root / config_filename, registry.render("init/config_toml", jsonStr))) {
    return Err<void>(ErrorCategory::IO, "Failed to write config.toml");
  }
  REXLOG_DEBUG("  Created {}", config_filename);

  if (!write_file(root / "CMakePresets.json", registry.render("init/cmake_presets", jsonStr))) {
    return Err<void>(ErrorCategory::IO, "Failed to write CMakePresets.json");
  }
  REXLOG_DEBUG("  Created CMakePresets.json");

  // Print success message with next steps
  REXLOG_INFO("Project '{}' initialized in '{}' successfully!", names.snake_case, opts.app_root);

  return Ok();
}

Result<void> InitModule(const InitModuleOptions& opts, const CliContext& ctx) {
  namespace fs = std::filesystem;

  // Find manifest in project root
  fs::path root(opts.app_root);
  fs::path manifestPath;
  for (const auto& entry : fs::directory_iterator(root)) {
    if (entry.path().extension() == ".toml" &&
        rex::codegen::ManifestConfig::IsManifest(entry.path())) {
      manifestPath = entry.path();
      break;
    }
  }
  if (manifestPath.empty()) {
    return Err<void>(rex::ErrorCategory::Config, "No manifest found in project root");
  }

  auto manifest = rex::codegen::ManifestConfig::Load(manifestPath);
  if (!manifest) {
    return Err<void>(rex::ErrorCategory::Config, "Failed to load manifest");
  }

  // Derive module name from XEX filename
  fs::path xexFile(opts.xex_path);
  std::string moduleName = xexFile.stem().string();
  std::replace(moduleName.begin(), moduleName.end(), '.', '_');
  std::replace(moduleName.begin(), moduleName.end(), ' ', '_');

  // Create per-binary config
  std::string configName = manifest->projectName + "_" + moduleName + ".toml";
  fs::path configPath = manifestPath.parent_path() / configName;

  if (fs::exists(configPath) && !ctx.force) {
    return Err<void>(rex::ErrorCategory::Config,
                     fmt::format("Config already exists: {}", configPath.string()));
  }

  std::ofstream configFile(configPath);
  configFile << "project_name = \"" << manifest->projectName << "\"\n";
  configFile << "file_path = \"" << opts.xex_path << "\"\n";
  configFile << "out_directory_path = \"generated/" << moduleName << "\"\n";
  configFile.close();

  // Append [[modules]] entry to manifest
  std::ofstream manifestFile(manifestPath, std::ios::app);
  manifestFile << "\n[[modules]]\n";
  manifestFile << "config = \"" << configName << "\"\n";
  manifestFile << "guestPath = \"" << opts.guest_path << "\"\n";
  manifestFile.close();

  REXLOG_INFO("Module '{}' added to project (config: {}, guest: {})", moduleName, configName,
              opts.guest_path);
  return Ok();
}

}  // namespace rexglue::cli
