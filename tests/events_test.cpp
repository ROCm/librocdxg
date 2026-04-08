#include <cstdio>
#include <cstdlib>

#include "librocdxg.h"

hsakmtRuntime *dxg_runtime = nullptr;

namespace {

void Check(HSAKMT_STATUS actual, HSAKMT_STATUS expected, const char *message) {
  if (actual != expected) {
    std::fprintf(stderr, "%s: got %d expected %d\n", message, actual, expected);
    std::exit(1);
  }
}

void CheckValue(uint64_t actual, uint64_t expected, const char *message) {
  if (actual != expected) {
    std::fprintf(stderr, "%s: got %llu expected %llu\n", message,
                 static_cast<unsigned long long>(actual),
                 static_cast<unsigned long long>(expected));
    std::exit(1);
  }
}

void CheckState(HsaEvent *event, HSAuint64 expected, const char *message) {
  if (event->EventData.HWData3 != expected) {
    std::fprintf(stderr, "%s: got %llu expected %llu\n", message,
                 static_cast<unsigned long long>(event->EventData.HWData3),
                 static_cast<unsigned long long>(expected));
    std::exit(1);
  }
}

struct RuntimeGuard {
  RuntimeGuard() {
    runtime = new hsakmtRuntime();
    runtime->dxg_open_count = 1;
    runtime->is_forked = false;
    dxg_runtime = runtime;
  }

  ~RuntimeGuard() { dxg_runtime = nullptr; }

  hsakmtRuntime *runtime;
};

HsaEvent *CreateSignalEvent(bool manual_reset, bool is_signaled) {
  HsaEventDescriptor descriptor = {};
  descriptor.EventType = HSA_EVENTTYPE_SIGNAL;

  HsaEvent *event = nullptr;
  Check(hsaKmtCreateEvent(&descriptor, manual_reset, is_signaled, &event),
        HSAKMT_STATUS_SUCCESS, "failed to create signal event");
  return event;
}

} // namespace

int main() {
  RuntimeGuard runtime_guard;

  {
    HsaEventDescriptor descriptor = {};
    descriptor.EventType = HSA_EVENTTYPE_SIGNAL;
    HsaEvent *event = nullptr;
    HsaEvent invalid_event = {};
    HsaEvent *events[1] = {&invalid_event};

    Check(hsaKmtCreateEvent(nullptr, false, false, &event),
          HSAKMT_STATUS_INVALID_PARAMETER,
          "null event descriptor should be rejected");
    Check(hsaKmtCreateEvent(&descriptor, false, false, nullptr),
          HSAKMT_STATUS_INVALID_PARAMETER,
          "null event output should be rejected");
    Check(hsaKmtDestroyEvent(nullptr), HSAKMT_STATUS_SUCCESS,
          "destroying a null event should succeed");
    Check(hsaKmtSetEvent(nullptr), HSAKMT_STATUS_INVALID_HANDLE,
          "null event set should be rejected");
    Check(hsaKmtSetEvent(&invalid_event), HSAKMT_STATUS_INVALID_HANDLE,
          "event without backing state should be rejected on set");
    Check(hsaKmtResetEvent(nullptr), HSAKMT_STATUS_INVALID_HANDLE,
          "null event reset should be rejected");
    Check(hsaKmtResetEvent(&invalid_event), HSAKMT_STATUS_INVALID_HANDLE,
          "event without backing state should be rejected on reset");
    Check(hsaKmtQueryEventState(nullptr), HSAKMT_STATUS_INVALID_HANDLE,
          "null event query should be rejected");
    Check(hsaKmtQueryEventState(&invalid_event), HSAKMT_STATUS_INVALID_HANDLE,
          "event without backing state should be rejected on query");
    Check(hsaKmtWaitOnEvent(nullptr, 0), HSAKMT_STATUS_INVALID_HANDLE,
          "null event wait should be rejected");
    Check(hsaKmtWaitOnMultipleEvents_Ext(nullptr, 1, true, 0, nullptr),
          HSAKMT_STATUS_INVALID_PARAMETER,
          "null event array should be rejected");
    Check(hsaKmtWaitOnMultipleEvents_Ext(events, 0, true, 0, nullptr),
          HSAKMT_STATUS_INVALID_PARAMETER,
          "zero event count should be rejected");
    Check(hsaKmtWaitOnMultipleEvents_Ext(events, 1, true, 0, nullptr),
          HSAKMT_STATUS_INVALID_HANDLE,
          "event without backing state should be rejected in multi wait");
  }

  {
    HsaEventDescriptor descriptor = {};
    descriptor.EventType = HSA_EVENTTYPE_MEMORY;
    HsaEvent *event = nullptr;
    Check(hsaKmtCreateEvent(&descriptor, false, false, &event),
          HSAKMT_STATUS_NOT_SUPPORTED,
          "unsupported event type should be rejected");
  }

  {
    HsaEvent *event = CreateSignalEvent(true, false);
    Check(hsaKmtSetEvent(event), HSAKMT_STATUS_SUCCESS,
          "manual reset set should succeed");
    Check(hsaKmtQueryEventState(event), HSAKMT_STATUS_SUCCESS,
          "manual reset query should succeed");
    CheckState(event, 1, "manual reset event should be signaled");
    Check(hsaKmtWaitOnEvent(event, 0), HSAKMT_STATUS_SUCCESS,
          "manual reset first wait should succeed");
    Check(hsaKmtWaitOnEvent(event, 0), HSAKMT_STATUS_SUCCESS,
          "manual reset second wait should succeed");
    Check(hsaKmtResetEvent(event), HSAKMT_STATUS_SUCCESS,
          "manual reset reset should succeed");
    Check(hsaKmtWaitOnEvent(event, 0), HSAKMT_STATUS_WAIT_TIMEOUT,
          "manual reset should time out after reset");
    Check(hsaKmtDestroyEvent(event), HSAKMT_STATUS_SUCCESS,
          "manual reset destroy should succeed");
  }

  {
    HsaEvent *event = CreateSignalEvent(false, false);
    Check(hsaKmtSetEvent(event), HSAKMT_STATUS_SUCCESS,
          "auto reset set should succeed");
    Check(hsaKmtWaitOnEvent(event, 0), HSAKMT_STATUS_SUCCESS,
          "auto reset first wait should succeed");
    Check(hsaKmtQueryEventState(event), HSAKMT_STATUS_SUCCESS,
          "auto reset query should succeed");
    CheckState(event, 0, "auto reset event should be consumed after wait");
    Check(hsaKmtWaitOnEvent(event, 0), HSAKMT_STATUS_WAIT_TIMEOUT,
          "auto reset second wait should time out");
    Check(hsaKmtDestroyEvent(event), HSAKMT_STATUS_SUCCESS,
          "auto reset destroy should succeed");
  }

  {
    HsaEvent *event = CreateSignalEvent(false, true);
    uint64_t event_age = 99;

    Check(hsaKmtWaitOnEvent_Ext(event, 0, &event_age), HSAKMT_STATUS_SUCCESS,
          "pre signaled auto reset wait should succeed");
    CheckState(event, 0, "pre signaled auto reset event should be consumed");
    CheckValue(event_age, 0, "event age should be cleared on wait success");
    Check(hsaKmtDestroyEvent(event), HSAKMT_STATUS_SUCCESS,
          "pre signaled auto reset destroy should succeed");
  }

  {
    HsaEvent *events[2] = {CreateSignalEvent(false, false),
                           CreateSignalEvent(false, false)};
    uint64_t event_age = 99;

    Check(hsaKmtWaitOnMultipleEvents_Ext(events, 2, false, 0, &event_age),
          HSAKMT_STATUS_WAIT_TIMEOUT,
          "wait any should time out when no events are signaled");
    CheckValue(event_age, 0, "event age should be cleared on wait any timeout");

    event_age = 99;
    Check(hsaKmtSetEvent(events[1]), HSAKMT_STATUS_SUCCESS,
          "wait any set second event should succeed");
    Check(hsaKmtWaitOnMultipleEvents(events, 2, false, 0),
          HSAKMT_STATUS_SUCCESS,
          "wait any should succeed when one event is signaled");
    Check(hsaKmtWaitOnEvent(events[1], 0), HSAKMT_STATUS_WAIT_TIMEOUT,
          "wait any should consume the winning auto reset event");
    Check(hsaKmtDestroyEvent(events[0]), HSAKMT_STATUS_SUCCESS,
          "destroy first wait any event should succeed");
    Check(hsaKmtDestroyEvent(events[1]), HSAKMT_STATUS_SUCCESS,
          "destroy second wait any event should succeed");
  }

  {
    HsaEvent *events[2] = {CreateSignalEvent(false, false),
                           CreateSignalEvent(false, false)};
    uint64_t event_age = 99;

    Check(hsaKmtSetEvent(events[0]), HSAKMT_STATUS_SUCCESS,
          "wait all set first event should succeed");
    Check(hsaKmtWaitOnMultipleEvents_Ext(events, 2, true, 0, &event_age),
          HSAKMT_STATUS_WAIT_TIMEOUT,
          "wait all should time out when one event is unsignaled");
    CheckValue(event_age, 0, "event age should be cleared on wait all timeout");
    Check(hsaKmtWaitOnEvent(events[0], 0), HSAKMT_STATUS_SUCCESS,
          "timed out wait all should preserve auto reset event state");

    event_age = 99;
    Check(hsaKmtSetEvent(events[0]), HSAKMT_STATUS_SUCCESS,
          "wait all re set first event should succeed");
    Check(hsaKmtSetEvent(events[1]), HSAKMT_STATUS_SUCCESS,
          "wait all set second event should succeed");
    Check(hsaKmtWaitOnMultipleEvents_Ext(events, 2, true, 0, &event_age),
          HSAKMT_STATUS_SUCCESS, "wait all should succeed when both are set");
    CheckValue(event_age, 0, "event age should be cleared on wait all success");
    Check(hsaKmtWaitOnEvent(events[0], 0), HSAKMT_STATUS_WAIT_TIMEOUT,
          "successful wait all should consume first auto reset event");
    Check(hsaKmtWaitOnEvent(events[1], 0), HSAKMT_STATUS_WAIT_TIMEOUT,
          "successful wait all should consume second auto reset event");

    Check(hsaKmtDestroyEvent(events[0]), HSAKMT_STATUS_SUCCESS,
          "destroy first wait all event should succeed");
    Check(hsaKmtDestroyEvent(events[1]), HSAKMT_STATUS_SUCCESS,
          "destroy second wait all event should succeed");
  }

  {
    HsaEvent *null_event = nullptr;
    Check(hsaKmtWaitOnMultipleEvents_Ext(&null_event, 1, true, 0, nullptr),
          HSAKMT_STATUS_SUCCESS,
          "single null event should preserve existing sleep behavior");
  }

  return 0;
}
