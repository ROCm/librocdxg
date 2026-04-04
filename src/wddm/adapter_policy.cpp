#include <cstdio>
#include <strings.h>

#include "wddm/adapter_policy.h"

namespace wsl {
namespace thunk {
namespace adapter_policy {

const AdapterInfoFallback *FindAdapterInfoFallback(uint32_t device_id) {
  static const AdapterInfoFallback kFallbacks[] = {
      {0x73E3, 10, 3, 2, 28},
      {0x73EF, 10, 3, 2, 28},
  };

  for (const auto &fallback : kFallbacks) {
    if (fallback.device_id == device_id)
      return &fallback;
  }

  return nullptr;
}

bool HasValidGfxOverrideValue(const char *value) {
  if (!value || !value[0])
    return false;

  char dummy = '\0';
  uint32_t major = 0, minor = 0, step = 0;
  return (std::sscanf(value, "%u.%u.%u%c", &major, &minor, &step, &dummy) ==
          3) &&
         (major <= 63 && minor <= 255 && step <= 255);
}

bool IsEnabledValue(const char *value) {
  if (!value || !value[0])
    return false;

  return !strcasecmp(value, "1") || !strcasecmp(value, "true") ||
         !strcasecmp(value, "yes") || !strcasecmp(value, "on");
}

bool ShouldAllowUnsupportedAdapter(uint32_t vendor_id, uint32_t device_id,
                                   bool has_gfx_override,
                                   bool enable_unsupported_adapters) {
  if (vendor_id != 0x1002)
    return false;

  if (FindAdapterInfoFallback(device_id) == nullptr)
    return false;

  if (enable_unsupported_adapters)
    return true;

  return has_gfx_override;
}

const char *UnsupportedAdapterReason(uint32_t device_id, bool has_gfx_override,
                                     bool enable_unsupported_adapters) {
  if (has_gfx_override && FindAdapterInfoFallback(device_id) != nullptr)
    return "HSA_OVERRIDE_GFX_VERSION";
  if (enable_unsupported_adapters)
    return "LIBROCDXG_ENABLE_UNSUPPORTED_ADAPTERS";
  return nullptr;
}

bool ApplyAdapterInfoFallback(thunk_proxy::DeviceInfo &device_info) {
  const auto *fallback = FindAdapterInfoFallback(device_info.device_id);
  if (!fallback)
    return false;

  bool used_fallback = false;

  if (device_info.major == 0) {
    device_info.major = fallback->major;
    used_fallback = true;
  }
  if (device_info.minor == 0) {
    device_info.minor = fallback->minor;
    used_fallback = true;
  }
  if (device_info.stepping == 0) {
    device_info.stepping = fallback->stepping;
    used_fallback = true;
  }
  if (device_info.compute_unit_count == 0) {
    device_info.compute_unit_count = fallback->compute_unit_count;
    used_fallback = true;
  }
  return used_fallback;
}

} // namespace adapter_policy
} // namespace thunk
} // namespace wsl
