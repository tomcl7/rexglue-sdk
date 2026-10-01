/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#pragma once

#include <atomic>
#include <cstdint>

#include <rex/kernel.h>
#include <rex/memory.h>

namespace rex::audio {

class AudioDriver {
 public:
  explicit AudioDriver(memory::Memory* memory);
  virtual ~AudioDriver();

  virtual void SubmitFrame(uint32_t samples_ptr) = 0;

  // Blocks the device has taken since the driver was made: those it was given
  // by the guest, and those it had to fill with silence because nothing was
  // queued. An underrun delays everything the guest renders after it, while
  // the guest's own clocks run on, so it is worth counting.
  uint64_t played_blocks() const { return played_blocks_.load(std::memory_order_relaxed); }
  uint64_t underrun_blocks() const { return underrun_blocks_.load(std::memory_order_relaxed); }

 protected:
  std::atomic<uint64_t> played_blocks_{0};
  std::atomic<uint64_t> underrun_blocks_{0};

  inline uint8_t* TranslatePhysical(uint32_t guest_address) const {
    return memory_->TranslatePhysical(guest_address);
  }

  memory::Memory* memory_ = nullptr;
};

}  // namespace rex::audio
