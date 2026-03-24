/**
 * @file        system/function_dispatcher.h
 * @brief       Guest function dispatch coordinator for recompiled code
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 *
 * @remarks     Derived from Xenia's runtime::Processor (Ben Vanik, 2020).
 *              Stripped of emulation-era dead code and renamed to reflect its
 *              role as a function dispatch table rather than a CPU emulator.
 */

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <rex/memory.h>
#include <rex/memory/mapped_memory.h>
#include <rex/ppc/context.h>
#include <rex/system/export_resolver.h>
#include <rex/system/thread_state.h>
#include <rex/thread/mutex.h>

namespace rex::runtime {

// Forward declarations
class ExportResolver;
class ThreadState;

class FunctionDispatcher {
 public:
  /// Callback type for module registration functions.
  using RegisterFn = void (*)(FunctionDispatcher*);

  FunctionDispatcher(memory::Memory* memory, ExportResolver* export_resolver);
  ~FunctionDispatcher();

  memory::Memory* memory() const { return memory_; }
  ExportResolver* export_resolver() const { return export_resolver_; }

  uint64_t Execute(ThreadState* thread_state, uint32_t address, uint64_t args[], size_t arg_count);
  uint64_t ExecuteInterrupt(ThreadState* thread_state, uint32_t address, uint64_t args[],
                            size_t arg_count);

  // Shared constant: thunk region size per module. Used by both FunctionDispatcher
  // and Memory for dispatch table sizing. Defined once here, referenced by Memory.
  static constexpr uint32_t kThunkReserveSize = 0x10000;  // 64KB

  // rexglue function table management (per-module table at IMAGE_BASE + IMAGE_SIZE)
  bool InitializeFunctionTable(uint32_t code_base, uint32_t code_size, uint32_t image_base,
                               uint32_t image_size);
  void SetFunction(uint32_t guest_address, ::PPCFunc* func);
  ::PPCFunc* GetFunction(uint32_t guest_address);
  bool HasFunctionTable() const { return !module_tables_.empty(); }
  uint32_t AllocateThunk(::PPCFunc* func);
  uint32_t AllocateThunk(::PPCFunc* func, uint32_t caller_address);

  /// Register a module's functions. Calls register_func while recording all
  /// addresses written via SetFunction. Recorded addresses are stored under
  /// module_id for later UnregisterModule.
  void RegisterModule(const std::string& module_id, RegisterFn register_func);

  /// Unregister all functions previously registered under module_id.
  /// Overwrites their guest memory table slots with the trap function.
  void UnregisterModule(const std::string& module_id);

 private:
  bool Execute(ThreadState* thread_state, uint32_t address);

  /// Trap function for indirect calls to unloaded/unregistered addresses.
  static void UnloadedModuleTrap(PPCContext& ctx, uint8_t* base);

  struct ModuleTableInfo {
    uint32_t code_base;
    uint32_t code_size;
    uint32_t image_base;
    uint32_t image_size;
    uint32_t next_thunk_address;
    uint32_t thunk_limit;
  };

  ModuleTableInfo* FindModuleByAddress(uint32_t guest_address);

  memory::Memory* memory_ = nullptr;
  ExportResolver* export_resolver_ = nullptr;

  rex::thread::global_critical_region global_critical_region_;

  // C++ function lookup (for FunctionDispatcher::Execute/GetFunction from C++ code)
  std::unordered_map<uint32_t, ::PPCFunc*> function_table_;

  // Per-module function table metadata (small N, linear scan is fine)
  std::vector<ModuleTableInfo> module_tables_;

  // Module recording for RegisterModule/UnregisterModule
  bool recording_ = false;
  std::vector<uint32_t> recording_addresses_;

  // Recorded addresses per module (for UnregisterModule)
  std::unordered_map<std::string, std::vector<uint32_t>> module_addresses_;
};

}  // namespace rex::runtime
