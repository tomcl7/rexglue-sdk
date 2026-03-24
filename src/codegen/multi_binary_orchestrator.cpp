/**
 * @file        codegen/multi_binary_orchestrator.cpp
 * @brief       Orchestrator for manifest-based multi-binary codegen
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <rex/codegen/multi_binary_orchestrator.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <rex/codegen/codegen.h>
#include <rex/codegen/template_registry.h>
#include <rex/logging.h>

#include "template_registry_internal.h"

namespace rex::codegen {

namespace {

std::string DeriveTargetName(const std::filesystem::path& configPath) {
  auto stem = configPath.stem().string();
  std::replace(stem.begin(), stem.end(), '.', '_');
  std::replace(stem.begin(), stem.end(), ' ', '_');
  return stem;
}

}  // namespace

MultiBinaryOrchestrator::MultiBinaryOrchestrator(ManifestConfig manifest)
    : manifest_(std::move(manifest)) {}

Result<void> MultiBinaryOrchestrator::Run(const OrchestratorOptions& opts) {
  REXLOG_INFO("Multi-binary orchestrator: project '{}', {} DLL module(s)", manifest_.projectName,
              manifest_.modules.size());

  // Collect all configs: entrypoint + DLL modules
  struct ModuleEntry {
    std::filesystem::path configPath;
    std::string targetName;
    std::string guestPath;
    bool isDll;
  };

  std::vector<ModuleEntry> modules;
  modules.push_back({manifest_.entrypointConfig, "default_xex", "", false});

  for (const auto& mod : manifest_.modules) {
    auto name = DeriveTargetName(mod.config);
    modules.push_back({mod.config, name, mod.guestPath, true});
  }

  // Filter by --target (entrypoint always included)
  std::vector<ModuleEntry> targeted;
  targeted.push_back(modules[0]);  // entrypoint always included
  for (size_t i = 1; i < modules.size(); ++i) {
    if (opts.targets.empty() || std::find(opts.targets.begin(), opts.targets.end(),
                                          modules[i].targetName) != opts.targets.end()) {
      targeted.push_back(modules[i]);
    }
  }

  // Phase 1: Create pipelines and run analysis for all targeted modules
  struct PipelineEntry {
    CodegenPipeline pipeline;
    const ModuleEntry* module;
  };
  std::vector<PipelineEntry> pipelines;

  for (auto& mod : targeted) {
    REXLOG_INFO("Creating pipeline for '{}'", mod.targetName);
    auto pipeline = CodegenPipeline::Create(mod.configPath);
    if (!pipeline) {
      return Err<void>(pipeline.error());
    }

    if (opts.enableExceptionHandlers) {
      pipeline->context().Config().generateExceptionHandlers = true;
    }

    auto result = pipeline->RunAnalyze();
    if (!result) {
      REXLOG_ERROR("Analysis failed for '{}'", mod.targetName);
      return result;
    }

    pipelines.push_back({std::move(*pipeline), &mod});
  }

  // Phase 2: Verify non-overlapping module address ranges
  for (size_t i = 0; i < pipelines.size(); ++i) {
    auto& a = pipelines[i];
    uint32_t a_base = a.pipeline.context().binary().baseAddress();
    uint32_t a_end = a_base + a.pipeline.context().binary().imageSize();
    for (size_t j = i + 1; j < pipelines.size(); ++j) {
      auto& b = pipelines[j];
      uint32_t b_base = b.pipeline.context().binary().baseAddress();
      uint32_t b_end = b_base + b.pipeline.context().binary().imageSize();
      if (a_base < b_end && b_base < a_end) {
        return Err<void>(
            ErrorCategory::Validation,
            fmt::format("Module '{}' [{:08X}, {:08X}) overlaps '{}' [{:08X}, {:08X})",
                        a.module->targetName, a_base, a_end, b.module->targetName, b_base, b_end));
      }
    }
  }

  // Phase 3: Write output for each module
  for (auto& entry : pipelines) {
    if (entry.module->isDll) {
      entry.pipeline.context().setDllModule(true);
    }
    REXLOG_INFO("Writing output for '{}'", entry.module->targetName);
    auto result = entry.pipeline.RunWrite(opts.force);
    if (!result) {
      REXLOG_ERROR("Write failed for '{}'", entry.module->targetName);
      return result;
    }
  }

  // Phase 4: Emit module_registry.cpp
  if (!manifest_.modules.empty()) {
    auto& entrypointCtx = pipelines[0].pipeline.context();
    auto outputPath = entrypointCtx.configDir() / entrypointCtx.Config().outDirectoryPath;

    nlohmann::json registryData;
    registryData["entrypoint_pe_name"] = "default.xex";
    registryData["entrypoint_register_func"] = manifest_.projectName + "_RegisterFunctions";

    auto& dllArray = registryData["dll_modules"];
    dllArray = nlohmann::json::array();
    for (const auto& mod : manifest_.modules) {
      auto targetName = DeriveTargetName(mod.config);
      nlohmann::json dllEntry;
      dllEntry["pe_name"] = mod.config.stem().string();
      dllEntry["guest_path"] = mod.guestPath;
      dllEntry["shared_lib_name"] = manifest_.projectName + "_" + targetName;
      dllArray.push_back(dllEntry);
    }

    TemplateRegistry registry;
    auto registryContent = renderWithJson(registry, "codegen/module_registry_cpp", registryData);

    auto registryPath = outputPath / "module_registry.cpp";
    std::filesystem::create_directories(outputPath);
    std::ofstream f(registryPath);
    f << registryContent;
    REXLOG_INFO("Wrote {}", registryPath.string());
  }

  REXLOG_INFO("Multi-binary orchestrator complete");
  return Ok();
}

}  // namespace rex::codegen
