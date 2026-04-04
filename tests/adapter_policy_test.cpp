#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "wddm/adapter_policy.h"

namespace {

void Check(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
  }
}

thunk_proxy::DeviceInfo MakeDeviceInfo(uint32_t device_id) {
  thunk_proxy::DeviceInfo device_info = {};
  device_info.device_id = device_id;
  return device_info;
}

} // namespace

int main() {
  using namespace wsl::thunk::adapter_policy;

  Check(FindAdapterInfoFallback(0x73EF) != nullptr,
        "expected fallback entry for known device id");
  Check(FindAdapterInfoFallback(0xFFFF) == nullptr,
        "unexpected fallback entry for unknown device id");

  Check(HasValidGfxOverrideValue("10.3.0"),
        "valid gfx override should be accepted");
  Check(!HasValidGfxOverrideValue("10.3"),
        "short gfx override should be rejected");
  Check(!HasValidGfxOverrideValue("bad"),
        "non numeric gfx override should be rejected");
  Check(!HasValidGfxOverrideValue("64.3.0"),
        "out of range gfx override should be rejected");

  Check(IsEnabledValue("1"), "1 should enable flag");
  Check(IsEnabledValue("true"), "true should enable flag");
  Check(IsEnabledValue("YES"), "YES should enable flag");
  Check(!IsEnabledValue("0"), "0 should not enable flag");
  Check(!IsEnabledValue(nullptr), "null should not enable flag");

  Check(ShouldAllowUnsupportedAdapter(0x1002, 0x73EF, true, false),
        "valid override should admit known AMD fallback device");
  Check(ShouldAllowUnsupportedAdapter(0x1002, 0x73EF, false, true),
        "explicit opt in should admit known AMD fallback device");
  Check(!ShouldAllowUnsupportedAdapter(0x1002, 0xFFFF, true, true),
        "unknown device should not be admitted");
  Check(!ShouldAllowUnsupportedAdapter(0x10DE, 0x73EF, true, true),
        "non AMD device should not be admitted");
  Check(!ShouldAllowUnsupportedAdapter(0x1002, 0x73EF, false, false),
        "known device should stay blocked without explicit opt in");

  Check(std::strcmp(UnsupportedAdapterReason(0x73EF, true, false),
                    "HSA_OVERRIDE_GFX_VERSION") == 0,
        "override reason mismatch");
  Check(std::strcmp(UnsupportedAdapterReason(0x73EF, false, true),
                    "LIBROCDXG_ENABLE_UNSUPPORTED_ADAPTERS") == 0,
        "opt in reason mismatch");

  auto empty_device = MakeDeviceInfo(0x73EF);
  Check(ApplyAdapterInfoFallback(empty_device),
        "expected fallback metadata to populate missing fields");
  Check(empty_device.major == 10 && empty_device.minor == 3 &&
            empty_device.stepping == 2,
        "unexpected fallback engine version");
  Check(empty_device.compute_unit_count == 28,
        "unexpected fallback compute unit count");

  auto populated_device = MakeDeviceInfo(0x73EF);
  populated_device.major = 9;
  populated_device.minor = 9;
  populated_device.stepping = 9;
  populated_device.compute_unit_count = 99;
  Check(!ApplyAdapterInfoFallback(populated_device),
        "fallback should not overwrite existing metadata");
  Check(populated_device.major == 9 && populated_device.minor == 9 &&
            populated_device.stepping == 9,
        "existing engine version should be preserved");
  Check(populated_device.compute_unit_count == 99,
        "existing compute unit count should be preserved");

  auto unknown_device = MakeDeviceInfo(0xFFFF);
  Check(!ApplyAdapterInfoFallback(unknown_device),
        "unknown device should not receive fallback metadata");

  return 0;
}
