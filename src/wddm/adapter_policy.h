#ifndef LIBROCDXG_WDDM_ADAPTER_POLICY_H
#define LIBROCDXG_WDDM_ADAPTER_POLICY_H

#include <cstdint>

#include "impl/wddm/types.h"
#include "impl/thunk_proxy/thunk_proxy.h"

namespace wsl {
namespace thunk {
namespace adapter_policy {

struct AdapterInfoFallback {
  uint32_t device_id;
  int major;
  int minor;
  int stepping;
  uint32_t compute_unit_count;
};

const AdapterInfoFallback *FindAdapterInfoFallback(uint32_t device_id);

bool HasValidGfxOverrideValue(const char *value);

bool IsEnabledValue(const char *value);

bool ShouldAllowUnsupportedAdapter(uint32_t vendor_id, uint32_t device_id,
                                   bool has_gfx_override,
                                   bool enable_unsupported_adapters);

const char *UnsupportedAdapterReason(uint32_t device_id, bool has_gfx_override,
                                     bool enable_unsupported_adapters);

bool ApplyAdapterInfoFallback(thunk_proxy::DeviceInfo &device_info);

} // namespace adapter_policy
} // namespace thunk
} // namespace wsl

#endif
