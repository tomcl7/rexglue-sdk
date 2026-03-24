/**
 * @file        rex/codegen/multi_binary_orchestrator.h
 * @brief       Orchestrator for manifest-based multi-binary codegen
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#pragma once

#include <string>
#include <vector>

#include <rex/codegen/manifest.h>
#include <rex/result.h>

namespace rex::codegen {

struct OrchestratorOptions {
  std::vector<std::string> targets;  // empty = all
  bool force = false;
  bool enableExceptionHandlers = false;
};

class MultiBinaryOrchestrator {
 public:
  explicit MultiBinaryOrchestrator(ManifestConfig manifest);
  Result<void> Run(const OrchestratorOptions& opts);

 private:
  ManifestConfig manifest_;
};

}  // namespace rex::codegen
