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

#include <rex/codegen/analyze.h>
#include <rex/codegen/binary_view.h>
#include <rex/codegen/codegen.h>
#include <rex/codegen/codegen_context.h>
#include <rex/codegen/codegen_writer.h>
#include <rex/codegen/config.h>
#include <rex/codegen/template_registry.h>
#include <rex/kernel/init.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/user_module.h>

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
  namespace fs = std::filesystem;

  REXLOG_INFO("Multi-binary orchestrator: project '{}', {} DLL module(s)", manifest_.projectName,
              manifest_.modules.size());

  // Collect all module entries
  struct ModuleEntry {
    fs::path configPath;
    std::string targetName;
    std::string guestPath;
    bool isDll;
  };

  std::vector<ModuleEntry> allModules;
  allModules.push_back({manifest_.entrypointConfig, "default_xex", "", false});
  for (const auto& mod : manifest_.modules) {
    allModules.push_back({mod.config, DeriveTargetName(mod.config), mod.guestPath, true});
  }

  // Filter by --target (entrypoint always included)
  std::vector<ModuleEntry> targeted;
  targeted.push_back(allModules[0]);
  for (size_t i = 1; i < allModules.size(); ++i) {
    if (opts.targets.empty() || std::find(opts.targets.begin(), opts.targets.end(),
                                          allModules[i].targetName) != opts.targets.end()) {
      targeted.push_back(allModules[i]);
    }
  }

  // ---- Phase 0: Create one Runtime and load ALL XEXs upfront ----

  // Resolve entrypoint XEX path (needed to set up the Runtime content root)
  RecompilerConfig entryConfig;
  if (!entryConfig.Load(targeted[0].configPath.string())) {
    return Err<void>(ErrorCategory::Config, "Failed to load entrypoint config");
  }
  auto configDir = targeted[0].configPath.parent_path();
  fs::path entryXexPath = configDir / entryConfig.filePath;
  if (!entryConfig.patchedFilePath.empty()) {
    auto patched = configDir / entryConfig.patchedFilePath;
    if (fs::exists(patched))
      entryXexPath = patched;
  }
  if (!fs::exists(entryXexPath)) {
    return Err<void>(ErrorCategory::IO,
                     fmt::format("Entrypoint XEX not found: {}", entryXexPath.string()));
  }
  entryXexPath = fs::canonical(entryXexPath);

  auto runtime = std::make_unique<Runtime>(entryXexPath.parent_path().string());
  auto rtStatus = runtime->Setup(rex::RuntimeConfig{
      .kernel_init = rex::kernel::InitializeKernel,
      .tool_mode = true,
  });
  if (rtStatus != X_STATUS_SUCCESS) {
    return Err<void>(ErrorCategory::IO,
                     fmt::format("Failed to initialize Runtime: {:#x}", rtStatus));
  }

  // Load entrypoint as executable module
  auto entryVfsPath = "game:\\" + entryXexPath.filename().string();
  rtStatus = runtime->LoadXexImage(entryVfsPath);
  if (rtStatus != X_STATUS_SUCCESS) {
    return Err<void>(ErrorCategory::IO,
                     fmt::format("Failed to load entrypoint XEX: {:#x}", rtStatus));
  }

  // Load DLL modules as user modules, keeping refs for context creation
  auto gameRoot = fs::canonical(entryXexPath.parent_path());
  std::vector<rex::system::object_ref<rex::system::UserModule>> dllModules;
  for (size_t i = 1; i < targeted.size(); ++i) {
    RecompilerConfig dllConfig;
    if (!dllConfig.Load(targeted[i].configPath.string())) {
      return Err<void>(ErrorCategory::Config,
                       fmt::format("Failed to load config: {}", targeted[i].configPath.string()));
    }
    fs::path dllXexPath = configDir / dllConfig.filePath;
    if (!dllConfig.patchedFilePath.empty()) {
      auto patched = configDir / dllConfig.patchedFilePath;
      if (fs::exists(patched))
        dllXexPath = patched;
    }
    if (!fs::exists(dllXexPath)) {
      return Err<void>(ErrorCategory::IO,
                       fmt::format("DLL XEX not found: {}", dllXexPath.string()));
    }
    dllXexPath = fs::canonical(dllXexPath);

    // Compute VFS path relative to game data root
    auto relPath = fs::relative(dllXexPath, gameRoot);
    std::string relStr = relPath.string();
    std::replace(relStr.begin(), relStr.end(), '/', '\\');
    auto dllVfsPath = "game:\\" + relStr;
    auto userMod = runtime->kernel_state()->LoadUserModule(dllVfsPath, false);
    if (!userMod) {
      return Err<void>(ErrorCategory::IO,
                       fmt::format("Failed to load DLL module: {}", targeted[i].targetName));
    }
    REXLOG_INFO("Loaded DLL module '{}' at base 0x{:08X}", targeted[i].targetName,
                userMod->xex_module()->base_address());
    dllModules.push_back(std::move(userMod));
  }

  // ---- Phase 1: Create contexts from loaded modules and analyze ----

  struct ContextEntry {
    CodegenContext ctx;
    const ModuleEntry* module;
  };
  std::vector<ContextEntry> contexts;

  auto* resolver = runtime->export_resolver();

  // Entrypoint context from executable module
  {
    auto execMod = runtime->kernel_state()->GetExecutableModule();
    auto bv = BinaryView::fromModule(*execMod->xex_module());

    RecompilerConfig cfg;
    cfg.Load(targeted[0].configPath.string());
    if (opts.enableExceptionHandlers)
      cfg.generateExceptionHandlers = true;

    auto ctx = CodegenContext::Create(std::move(bv), std::move(cfg));
    ctx.setResolver(resolver);
    ctx.setConfigDir(targeted[0].configPath.parent_path());
    ctx.analysisState().format = "xex";
    ctx.analysisState().loadAddress = ctx.binary().baseAddress();
    ctx.analysisState().entryPoint = ctx.binary().entryPoint();
    ctx.analysisState().imageSize = ctx.binary().imageSize();
    ctx.setHasDllModules(!manifest_.modules.empty());

    contexts.push_back({std::move(ctx), &targeted[0]});
  }

  // DLL contexts from stored user module refs
  for (size_t i = 0; i < dllModules.size(); ++i) {
    auto& userMod = dllModules[i];
    auto bv = BinaryView::fromModule(*userMod->xex_module());

    RecompilerConfig cfg;
    cfg.Load(targeted[i + 1].configPath.string());
    if (opts.enableExceptionHandlers)
      cfg.generateExceptionHandlers = true;

    auto ctx = CodegenContext::Create(std::move(bv), std::move(cfg));
    ctx.setResolver(resolver);
    ctx.setConfigDir(targeted[i + 1].configPath.parent_path());
    ctx.analysisState().format = "xex";
    ctx.analysisState().loadAddress = ctx.binary().baseAddress();
    ctx.analysisState().entryPoint = ctx.binary().entryPoint();
    ctx.analysisState().imageSize = ctx.binary().imageSize();
    ctx.setDllModule(true);
    ctx.setHasDllModules(true);

    contexts.push_back({std::move(ctx), &targeted[i + 1]});
  }

  // Run analysis on each context
  for (auto& entry : contexts) {
    REXLOG_INFO("Analyzing '{}'...", entry.module->targetName);
    auto result = Analyze(entry.ctx);
    if (!result) {
      REXLOG_ERROR("Analysis failed for '{}'", entry.module->targetName);
      return result;
    }
  }

  // ---- Phase 2: Verify non-overlapping address ranges ----

  for (size_t i = 0; i < contexts.size(); ++i) {
    uint32_t a_base = contexts[i].ctx.binary().baseAddress();
    uint32_t a_end = a_base + contexts[i].ctx.binary().imageSize();
    for (size_t j = i + 1; j < contexts.size(); ++j) {
      uint32_t b_base = contexts[j].ctx.binary().baseAddress();
      uint32_t b_end = b_base + contexts[j].ctx.binary().imageSize();
      if (a_base < b_end && b_base < a_end) {
        return Err<void>(ErrorCategory::Validation,
                         fmt::format("Module '{}' [{:08X}, {:08X}) overlaps '{}' [{:08X}, {:08X})",
                                     contexts[i].module->targetName, a_base, a_end,
                                     contexts[j].module->targetName, b_base, b_end));
      }
    }
  }

  // ---- Phase 3: Write output ----

  for (auto& entry : contexts) {
    REXLOG_INFO("Writing output for '{}'...", entry.module->targetName);
    CodegenWriter writer(entry.ctx, runtime.get());
    if (!writer.write(opts.force)) {
      return Err<void>(ErrorCategory::Validation,
                       fmt::format("Write failed for '{}'", entry.module->targetName));
    }
  }

  // ---- Phase 4: Emit module_registry.cpp ----

  if (!manifest_.modules.empty()) {
    auto outputPath = contexts[0].ctx.configDir() / contexts[0].ctx.Config().outDirectoryPath;

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
    fs::create_directories(outputPath);
    std::ofstream f(registryPath);
    f << registryContent;
    REXLOG_INFO("Wrote {}", registryPath.string());
  }

  // ---- Phase 5: Emit dll_targets.cmake ----

  if (!manifest_.modules.empty()) {
    auto entryOutputPath = contexts[0].ctx.configDir() / contexts[0].ctx.Config().outDirectoryPath;

    nlohmann::json dllTargetsData;
    auto& dllTargetsArray = dllTargetsData["dll_modules"];
    dllTargetsArray = nlohmann::json::array();
    for (size_t i = 0; i < dllModules.size(); ++i) {
      auto& mod = targeted[i + 1];
      auto& dllCtx = contexts[i + 1].ctx;
      nlohmann::json entry;
      entry["target_name"] = mod.targetName;
      entry["lib_name"] = manifest_.projectName + "_" + mod.targetName;
      entry["output_dir"] = dllCtx.Config().outDirectoryPath;
      dllTargetsArray.push_back(entry);
    }

    TemplateRegistry registry;
    auto dllCmakeContent = renderWithJson(registry, "codegen/dll_targets_cmake", dllTargetsData);

    auto dllCmakePath = entryOutputPath / "dll_targets.cmake";
    std::ofstream cf(dllCmakePath);
    cf << dllCmakeContent;
    REXLOG_INFO("Wrote {}", dllCmakePath.string());
  }

  REXLOG_INFO("Multi-binary orchestrator complete");
  return Ok();
}

}  // namespace rex::codegen
