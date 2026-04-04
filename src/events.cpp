/*
 * Copyright © 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including
 * the next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

namespace {

std::atomic<HSA_EVENTID> g_next_event_id{1};

class EventState {
public:
  EventState(bool manual_reset, bool signaled)
      : signaled_(signaled), manual_reset_(manual_reset) {}

  void Set() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      signaled_ = true;
    }
    if (manual_reset_)
      condition_.notify_all();
    else
      condition_.notify_one();
  }

  void Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    signaled_ = false;
  }

  void ConsumeIfAutoReset() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!manual_reset_ && signaled_)
      signaled_ = false;
  }

  bool IsSignaled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return signaled_;
  }

  HSAKMT_STATUS Wait(HSAuint32 milliseconds) {
    return WaitInternal(milliseconds, true);
  }

  HSAKMT_STATUS WaitUntilSignaled(HSAuint32 milliseconds) {
    return WaitInternal(milliseconds, false);
  }

private:
  HSAKMT_STATUS WaitInternal(HSAuint32 milliseconds, bool consume) {
    std::unique_lock<std::mutex> lock(mutex_);

    if (!signaled_) {
      if (milliseconds == HSA_EVENTTIMEOUT_INFINITE) {
        condition_.wait(lock, [this] { return signaled_; });
      } else if (!condition_.wait_for(lock, std::chrono::milliseconds(milliseconds),
                                      [this] { return signaled_; })) {
        return HSAKMT_STATUS_WAIT_TIMEOUT;
      }
    }

    if (consume && !manual_reset_)
      signaled_ = false;

    return HSAKMT_STATUS_SUCCESS;
  }

  mutable std::mutex mutex_;
  std::condition_variable condition_;
  bool signaled_;
  bool manual_reset_;
};

bool IsSupportedEventType(HSA_EVENTTYPE event_type) {
  return event_type == HSA_EVENTTYPE_SIGNAL;
}

EventState *GetEventState(HsaEvent *event) {
  return reinterpret_cast<EventState *>(event->EventData.HWData1);
}

void SetSignaled(HsaEvent *event, bool signaled) {
  event->EventData.HWData3 = signaled ? 1 : 0;
}

HSAKMT_STATUS WaitOnMultipleEvents(EventState *states[], HSAuint32 num_events,
                                   bool wait_on_all, HSAuint32 milliseconds,
                                   uint64_t *event_age) {
  if (!states || !num_events)
    return HSAKMT_STATUS_INVALID_PARAMETER;

  if (event_age)
    *event_age = 0;

  auto remaining_ms = milliseconds;
  auto start = std::chrono::steady_clock::now();
  const bool infinite = milliseconds == HSA_EVENTTIMEOUT_INFINITE;

  if (wait_on_all) {
    for (HSAuint32 i = 0; i < num_events; i++) {
      if (!states[i])
        return HSAKMT_STATUS_INVALID_HANDLE;

      auto status =
          states[i]->WaitUntilSignaled(infinite ? HSA_EVENTTIMEOUT_INFINITE
                                                : remaining_ms);
      if (status != HSAKMT_STATUS_SUCCESS)
        return status;

      if (!infinite) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = static_cast<HSAuint32>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - start)
                .count());
        remaining_ms =
            elapsed_ms >= milliseconds ? 0 : milliseconds - elapsed_ms;
      }
    }

    for (HSAuint32 i = 0; i < num_events; i++)
      states[i]->ConsumeIfAutoReset();

    return HSAKMT_STATUS_SUCCESS;
  }

  while (true) {
    for (HSAuint32 i = 0; i < num_events; i++) {
      if (!states[i])
        return HSAKMT_STATUS_INVALID_HANDLE;

      auto status = states[i]->Wait(0);
      if (status == HSAKMT_STATUS_SUCCESS)
        return status;
      if (status != HSAKMT_STATUS_WAIT_TIMEOUT)
        return status;
    }

    if (!infinite) {
      auto now = std::chrono::steady_clock::now();
      auto elapsed_ms = static_cast<HSAuint32>(
          std::chrono::duration_cast<std::chrono::milliseconds>(now - start)
              .count());
      if (elapsed_ms >= milliseconds)
        return HSAKMT_STATUS_WAIT_TIMEOUT;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

} // namespace

HSAKMT_STATUS HSAKMTAPI hsaKmtCreateEvent(HsaEventDescriptor *EventDesc,
                                          bool ManualReset, bool IsSignaled,
                                          HsaEvent **Event) {
  CHECK_DXG_OPEN();

  if (!EventDesc || !Event)
    return HSAKMT_STATUS_INVALID_PARAMETER;

  if (!IsSupportedEventType(EventDesc->EventType))
    return HSAKMT_STATUS_NOT_SUPPORTED;

  auto *event = reinterpret_cast<HsaEvent *>(calloc(1, sizeof(HsaEvent)));
  if (!event)
    return HSAKMT_STATUS_NO_MEMORY;

  auto *state = new EventState(ManualReset, IsSignaled);
  if (!state) {
    free(event);
    return HSAKMT_STATUS_NO_MEMORY;
  }

  event->EventId = g_next_event_id.fetch_add(1, std::memory_order_relaxed);
  event->EventData.EventType = EventDesc->EventType;
  event->EventData.EventData.SyncVar = EventDesc->SyncVar;
  event->EventData.HWData1 = reinterpret_cast<HSAuint64>(state);
  event->EventData.HWData2 = ManualReset ? 1 : 0;
  SetSignaled(event, IsSignaled);

  *Event = event;
  return HSAKMT_STATUS_SUCCESS;
}

HSAKMT_STATUS HSAKMTAPI hsaKmtDestroyEvent(HsaEvent *Event) {
  CHECK_DXG_OPEN();
  if (!Event)
    return HSAKMT_STATUS_SUCCESS;

  delete GetEventState(Event);

  free(Event);
  return HSAKMT_STATUS_SUCCESS;
}

HSAKMT_STATUS HSAKMTAPI hsaKmtSetEvent(HsaEvent *Event) {
  CHECK_DXG_OPEN();
  if (!Event)
    return HSAKMT_STATUS_INVALID_HANDLE;

  auto *state = GetEventState(Event);
  if (!state)
    return HSAKMT_STATUS_INVALID_HANDLE;

  state->Set();
  SetSignaled(Event, true);
  return HSAKMT_STATUS_SUCCESS;
}

HSAKMT_STATUS HSAKMTAPI hsaKmtResetEvent(HsaEvent *Event) {
  CHECK_DXG_OPEN();
  if (!Event)
    return HSAKMT_STATUS_INVALID_HANDLE;

  auto *state = GetEventState(Event);
  if (!state)
    return HSAKMT_STATUS_INVALID_HANDLE;

  state->Reset();
  SetSignaled(Event, state->IsSignaled());
  return HSAKMT_STATUS_SUCCESS;
}

HSAKMT_STATUS HSAKMTAPI hsaKmtQueryEventState(HsaEvent *Event) {
  CHECK_DXG_OPEN();
  if (!Event)
    return HSAKMT_STATUS_INVALID_HANDLE;

  auto *state = GetEventState(Event);
  if (!state)
    return HSAKMT_STATUS_INVALID_HANDLE;

  SetSignaled(Event, state->IsSignaled());
  return HSAKMT_STATUS_SUCCESS;
}

HSAKMT_STATUS HSAKMTAPI hsaKmtWaitOnEvent(HsaEvent *Event,
                                          HSAuint32 Milliseconds) {
  return hsaKmtWaitOnEvent_Ext(Event, Milliseconds, NULL);
}

HSAKMT_STATUS HSAKMTAPI hsaKmtWaitOnEvent_Ext(HsaEvent *Event,
                                              HSAuint32 Milliseconds,
                                              uint64_t *event_age) {
  if (!Event)
    return HSAKMT_STATUS_INVALID_HANDLE;

  return hsaKmtWaitOnMultipleEvents_Ext(&Event, 1, true, Milliseconds,
                                        event_age);
}

HSAKMT_STATUS HSAKMTAPI hsaKmtWaitOnMultipleEvents(HsaEvent *Events[],
                                                   HSAuint32 NumEvents,
                                                   bool WaitOnAll,
                                                   HSAuint32 Milliseconds) {
  return hsaKmtWaitOnMultipleEvents_Ext(Events, NumEvents, WaitOnAll,
                                        Milliseconds, NULL);
}

HSAKMT_STATUS HSAKMTAPI hsaKmtWaitOnMultipleEvents_Ext(HsaEvent *Events[],
                                                       HSAuint32 NumEvents,
                                                       bool WaitOnAll,
                                                       HSAuint32 Milliseconds,
                                                       uint64_t *event_age) {
  CHECK_DXG_OPEN();

  if (!Events || !NumEvents)
    return HSAKMT_STATUS_INVALID_PARAMETER;

  if (event_age)
    *event_age = 0;

  if (NumEvents == 1 && Events[0] == nullptr) {
    std::this_thread::sleep_for(std::chrono::microseconds(20));
    return HSAKMT_STATUS_SUCCESS;
  }

  std::vector<EventState *> states(NumEvents);
  for (HSAuint32 i = 0; i < NumEvents; i++) {
    if (!Events[i] || !GetEventState(Events[i]))
      return HSAKMT_STATUS_INVALID_HANDLE;
    states[i] = GetEventState(Events[i]);
  }

  auto status = WaitOnMultipleEvents(states.data(), NumEvents, WaitOnAll,
                                     Milliseconds, event_age);
  if (status == HSAKMT_STATUS_SUCCESS) {
    for (HSAuint32 i = 0; i < NumEvents; i++)
      SetSignaled(Events[i], states[i]->IsSignaled());
  }

  return status;
}

HSAKMT_STATUS HSAKMTAPI hsaKmtOpenSMI(HSAuint32 NodeId, int *fd) {
  CHECK_DXG_OPEN();
  pr_debug("node id %d\n", NodeId);
  assert(false);
  return HSAKMT_STATUS_SUCCESS;
}
