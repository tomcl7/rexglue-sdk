#pragma once
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

#include <rex/system/export_resolver.h>
#include <rex/system/kernel_state.h>

namespace rex {
namespace kernel {
namespace xam {

bool xeXamIsUIActive();

/// Whether the game's input is held off: a system dialog (keyboard, message
/// box) is up, or one has just closed and the input that closed it has not
/// been let go yet. The pad the game polls reads neutral meanwhile, as it does
/// on the console while the system UI has the controller.
bool xeXamInputBlocked();
/// Called as a system dialog closes (xam_ui.cpp).
void xeXamHoldInputUntilReleased();
/// Called by each pad poll with whether the real state was at rest.
void xeXamNoteInputReleased(bool released);

rex::runtime::Export* RegisterExport_xam(rex::runtime::Export* export_entry);

// Registration functions, one per file.
#define XE_MODULE_EXPORT_GROUP(m, n)                                       \
  void Register##n##Exports(rex::runtime::ExportResolver* export_resolver, \
                            system::KernelState* kernel_state);
#include "module_export_groups.inc"
#undef XE_MODULE_EXPORT_GROUP

}  // namespace xam
}  // namespace kernel
}  // namespace rex
