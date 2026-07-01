/*
 * Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cinttypes>
#include <cstdint>

#include "hsakmt/hsakmt.h"
#include "shared/include/d3dkmt_types.h"

namespace wsl {
namespace thunk {

class WDDMDevice;

/**
 * @brief HSA signal event backed by an eventfd-bound CPU_NOTIFICATION syncobject.
 *
 * In the WSL guest there is no Windows KEVENT to wait on. Each event owns an
 * eventfd plus a D3DDDI_CPU_NOTIFICATION syncobject created with SignalByKmd and
 * bound to that eventfd. Set() bumps the eventfd from the CPU; the host KMD
 * bumps it on GPU completion (through dxgkrnl's VMBus path). Wait() blocks in
 * poll() on the eventfd; Reset() drains it.
 */
class Event final {
 public:
  Event();
  ~Event();

  bool Init(const HsaEventDescriptor& event_desc);
  bool Set();
  bool Reset();
  bool Wait(uint32_t milliseconds);

  HsaEvent* AsHsaEvent() { return &event_; }
  static Event* FromHsaEvent(HsaEvent* event);

  int Efd() const { return efd_; }
  D3DKMT_HANDLE Syncobj() const { return syncobj_; }

 private:
  HsaEvent event_;
  WDDMDevice* device_;
  int efd_;                // eventfd backing this HSA event
  D3DKMT_HANDLE syncobj_;  // CPU_NOTIFICATION syncobj bound to efd_

  Event(const Event&) = delete;
  Event& operator=(const Event&) = delete;
};

}  // namespace thunk
}  // namespace wsl
