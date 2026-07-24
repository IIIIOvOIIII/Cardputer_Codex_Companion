#include "probe/hardware_probe.hpp"

#include <cinttypes>
#include <cstdio>

#include "esp_log.h"

namespace {
constexpr char kTag[] = "cardputer-codex-phase0";
constexpr char kEventType[] = "hardware_runtime";
}  // namespace

extern "C" void app_main() {
  ESP_LOGI(kTag, "PHASE 0 / NOT FOR RELEASE");

  const HardwareRuntime runtime = probe_hardware();
  std::printf(
      "{\"type\":\"%s\",\"chip_model\":\"%s\",\"chip_revision\":%" PRIu32
      ",\"flash_jedec_id\":\"%06" PRIx32
      "\",\"flash_bytes\":%" PRIu32 ",\"psram_bytes\":%" PRIu32 "}\n",
      kEventType,
      runtime.chip_model,
      runtime.chip_revision,
      runtime.flash_jedec_id,
      runtime.flash_bytes,
      runtime.psram_bytes);
}
