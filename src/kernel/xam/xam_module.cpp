/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <atomic>
#include <chrono>
#include <vector>

#include <rex/kernel/xam/module.h>
#include <rex/kernel/xam/private.h>
#include <rex/math.h>
#include <rex/hook.h>
#include <rex/system/kernel_state.h>

namespace rex {
namespace kernel {
namespace xam {
using namespace rex::system;
using namespace rex::system::xam;

std::atomic<int> xam_dialogs_shown_ = {0};

bool xeXamIsUIActive() {
  return xam_dialogs_shown_ > 0;
}

// Set as a system dialog closes, and cleared by the first poll that finds the
// pad let go (xam_input.cpp): the key or button that closed the dialog --
// Enter on the keyboard's A, say -- must not reach the game as well.
std::atomic<bool> xam_input_held_after_ui_ = {false};
std::atomic<int64_t> xam_input_held_since_ns_ = {0};

void xeXamHoldInputUntilReleased() {
  xam_input_held_since_ns_.store(
      std::chrono::steady_clock::now().time_since_epoch().count(), std::memory_order_relaxed);
  xam_input_held_after_ui_.store(true, std::memory_order_release);
}

bool xeXamInputBlocked() {
  return xeXamIsUIActive() || xam_input_held_after_ui_.load(std::memory_order_acquire);
}

void xeXamNoteInputReleased(bool released) {
  if (!xam_input_held_after_ui_.load(std::memory_order_acquire)) {
    return;
  }
  // A pad left leaning on a stick or a stuck key must not hold the game off
  // for good: the hold gives up after a second either way.
  const int64_t since = xam_input_held_since_ns_.load(std::memory_order_relaxed);
  const int64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
  const bool expired =
      now - since > std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                        std::chrono::seconds(1))
                        .count();
  if (released || expired) {
    xam_input_held_after_ui_.store(false, std::memory_order_release);
  }
}

XamModule::XamModule(Runtime* emulator, KernelState* kernel_state)
    : KernelModule(kernel_state, "xe:\\xam.xex"), loader_data_() {
  RegisterExportTable(export_resolver_);

  // Register all exported functions.
  // #define XE_MODULE_EXPORT_GROUP(m, n) \
//  Register##n##Exports(export_resolver_, kernel_state_);
  // #include <rex/kernel/xam/module_export_groups.inc>
  // #undef XE_MODULE_EXPORT_GROUP
}

std::vector<rex::runtime::Export*> xam_exports(4096);

rex::runtime::Export* RegisterExport_xam(rex::runtime::Export* export_entry) {
  assert_true(export_entry->ordinal < xam_exports.size());
  xam_exports[export_entry->ordinal] = export_entry;
  return export_entry;
}

void XamModule::RegisterExportTable(rex::runtime::ExportResolver* export_resolver) {
  assert_not_null(export_resolver);

// Build the export table used for resolution.
#include "../export_table_pre.inc"
  static rex::runtime::Export xam_export_table[] = {
#include "export_table.inc"
  };
#include "../export_table_post.inc"
  for (size_t i = 0; i < rex::countof(xam_export_table); ++i) {
    auto& export_entry = xam_export_table[i];
    assert_true(export_entry.ordinal < xam_exports.size());
    if (!xam_exports[export_entry.ordinal]) {
      xam_exports[export_entry.ordinal] = &export_entry;
    }
  }
  export_resolver->RegisterTable("xam.xex", &xam_exports);
}

XamModule::~XamModule() {}

}  // namespace xam
}  // namespace kernel
}  // namespace rex
