/*
 * Copyright (c) Advanced Micro Devices, Inc., or its affiliates.
 *
 * SPDX-License-Identifier: MIT
 */

#include <cstring>
#include <cinttypes>
#include <cerrno>
#include <mutex>
#include <unordered_map>
#include <unistd.h>
#include <sys/eventfd.h>

#include "impl/wddm/device.h"
#include "impl/wddm/event.h"

namespace wsl {
namespace thunk {

namespace {
std::mutex g_events_mutex;
std::unordered_map<HsaEvent*, Event*> g_events;
}  // namespace

// Force device 0; KMD is responsible for handling multiple devices.
static WDDMDevice* EventDevice() {
  return get_wddmdev(rocdxg::wddm_index_to_gpu_node(0));
}

// ================================================================================================
Event::Event()
    : event_{}, device_(nullptr), efd_(-1), syncobj_(0) {
  event_.EventId = 0;
  memset(&event_.EventData, 0, sizeof(event_.EventData));
}

// ================================================================================================
Event::~Event() {
  {
    std::lock_guard<std::mutex> lock(g_events_mutex);
    g_events.erase(AsHsaEvent());
  }

  if (device_ != nullptr) {
    if (event_.EventId != 0) {
      device_->UnregisterEvent(event_.EventId, syncobj_);
    }
    if (syncobj_ != 0) {
      device_->DestroySyncobj(syncobj_);
    }
  }
  if (efd_ >= 0) {
    close(efd_);
  }
  efd_ = -1;
  syncobj_ = 0;
}

Event* Event::FromHsaEvent(HsaEvent* event) {
  std::lock_guard<std::mutex> lock(g_events_mutex);
  auto it = g_events.find(event);
  return it == g_events.end() ? nullptr : it->second;
}

// ================================================================================================
bool Event::Init(const HsaEventDescriptor& event_desc) {
  device_ = EventDevice();
  if (device_ == nullptr) {
    pr_err("couldn't obtain a device\n");
    return false;
  }

  // eventfd backs the HSA event: Set() and the host KMD write to it, Wait()
  // blocks on it via poll().
  efd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (efd_ < 0) {
    pr_err("eventfd failed %d\n", errno);
    return false;
  }

  // CPU_NOTIFICATION syncobj bound to the eventfd; the host KMD signals it on
  // GPU completion (SignalByKmd), waking a guest waiter through dxgkrnl.
  if (!device_->CreateCpuEventSyncobj(efd_, &syncobj_)) {
    pr_err("create cpu-notification syncobj failed\n");
    close(efd_);
    efd_ = -1;
    return false;
  }

  // Register the event id and bind it to the syncobj in the host KMD so the GPU
  // interrupt path can resolve a completion-signal event to this syncobj.
  event_.EventId = device_->RegisterEvent(event_desc.EventType, syncobj_, &event_.EventData.HWData2);
  if (event_.EventId == 0) {
    device_->DestroySyncobj(syncobj_);
    syncobj_ = 0;
    close(efd_);
    efd_ = -1;
    return false;
  }

  event_.EventData.EventType = event_desc.EventType;
  event_.EventData.HWData3 = event_desc.NodeId;
  event_.EventData.EventData.SyncVar.SyncVar.UserData = event_desc.SyncVar.SyncVar.UserData;
  event_.EventData.EventData.SyncVar.SyncVarSize = event_desc.SyncVar.SyncVarSize;

  {
    std::lock_guard<std::mutex> lock(g_events_mutex);
    g_events.emplace(AsHsaEvent(), this);
  }

  return true;
}

// ================================================================================================
bool Event::Set() {
  if (efd_ < 0) {
    return false;
  }
  // CPU-side signal: bump the eventfd counter to wake any poll() waiter.
  uint64_t one = 1;
  ssize_t n = write(efd_, &one, sizeof(one));
  return n == static_cast<ssize_t>(sizeof(one));
}

// ================================================================================================
bool Event::Reset() {
  if (efd_ < 0) {
    return false;
  }
  // Drain the eventfd counter so future waits block again.
  uint64_t val;
  while (read(efd_, &val, sizeof(val)) == static_cast<ssize_t>(sizeof(val)))
    ;
  return true;
}

// ================================================================================================
bool Event::Wait(uint32_t milliseconds) {
  HsaEvent* self = AsHsaEvent();
  return WDDMDevice::WaitOnMultipleEvents(&self, 1, true, milliseconds) ==
         HSAKMT_STATUS_SUCCESS;
}

}  // namespace thunk
}  // namespace wsl
