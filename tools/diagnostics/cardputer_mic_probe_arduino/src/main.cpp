#include <M5Unified.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace {

constexpr uint32_t kSampleRate = 17000;
constexpr std::size_t kSamples = 512;
std::array<int16_t, kSamples> g_samples{};

void print_frame(uint32_t frame) {
  const auto [minimum, maximum] =
      std::minmax_element(g_samples.begin(), g_samples.end());
  uint32_t peak = 0;
  uint64_t absolute_sum = 0;
  uint32_t changes = 0;
  for (std::size_t index = 0; index < g_samples.size(); ++index) {
    const int32_t value = g_samples[index];
    const uint32_t magnitude =
        static_cast<uint32_t>(value < 0 ? -value : value);
    peak = std::max(peak, magnitude);
    absolute_sum += magnitude;
    if (index != 0 && g_samples[index] != g_samples[index - 1]) {
      ++changes;
    }
  }
  Serial.printf(
      "ARDUINO_MIC frame=%lu min=%d max=%d peak=%lu mean=%lu "
      "changes=%lu first=%d\n",
      static_cast<unsigned long>(frame), static_cast<int>(*minimum),
      static_cast<int>(*maximum), static_cast<unsigned long>(peak),
      static_cast<unsigned long>(absolute_sum / g_samples.size()),
      static_cast<unsigned long>(changes), static_cast<int>(g_samples.front()));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  auto config = M5.config();
  config.clear_display = false;
  M5.begin(config);
  M5.Speaker.end();
  const auto mic = M5.Mic.config();
  Serial.printf(
      "ARDUINO_MIC board=%u data=%d clock=%d bck=%d channel=%u "
      "oversampling=%u gain=%u port=%u\n",
      static_cast<unsigned>(M5.getBoard()), mic.pin_data_in, mic.pin_ws,
      mic.pin_bck, static_cast<unsigned>(mic.input_channel),
      static_cast<unsigned>(mic.over_sampling),
      static_cast<unsigned>(mic.magnification),
      static_cast<unsigned>(mic.i2s_port));
  if (!M5.Mic.begin()) {
    Serial.println("ARDUINO_MIC begin_failed");
  }
}

void loop() {
  static uint32_t frame = 0;
  if (!M5.Mic.record(g_samples.data(), g_samples.size(), kSampleRate, false)) {
    Serial.println("ARDUINO_MIC record_failed");
    delay(1000);
    return;
  }
  while (M5.Mic.isRecording() != 0) {
    delay(1);
  }
  print_frame(frame++);
}
