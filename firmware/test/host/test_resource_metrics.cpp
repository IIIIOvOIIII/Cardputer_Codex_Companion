#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "probe/resource_metrics.hpp"

namespace {

bool structurally_complete_json_object(std::string_view line) {
  if (!line.ends_with("]}\n")) {
    return false;
  }
  int object_depth = 0;
  int array_depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (const char value : line) {
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (value == '\\') {
        escaped = true;
      } else if (value == '"') {
        in_string = false;
      }
      continue;
    }
    if (value == '"') {
      in_string = true;
    } else if (value == '{') {
      ++object_depth;
    } else if (value == '}') {
      --object_depth;
    } else if (value == '[') {
      ++array_depth;
    } else if (value == ']') {
      --array_depth;
    }
    if (object_depth < 0 || array_depth < 0) {
      return false;
    }
  }
  return !in_string && object_depth == 0 && array_depth == 0;
}

}  // namespace

int main() {
  HidLatencyMetrics edge;
  edge.observe(1, 1, true);
  assert(edge.buckets[0] == 1);
  edge.observe(1, 99, true);
  assert(edge.buckets[1] == 1);
  edge.observe(1, 100, true);
  assert(edge.buckets[1] == 2);
  edge.observe(1, 102, true);
  assert(edge.buckets[2] == 1);
  assert(edge.generated == 4);
  assert(edge.queued == 4);
  assert(edge.queue_failures == 0);
  assert(edge.p95_upper_bound_us() <= 20000);

  edge.observe(1, 100000, true);
  assert(edge.buckets[1000] == 1);
  edge.observe(1, 100002, true);
  assert(edge.buckets[1001] == 1);
  assert(edge.generated == 6);
  assert(edge.queued == 6);

  edge.observe(0, 100, false);
  assert(edge.queue_failures == 1);
  assert(edge.generated == 7);
  assert(edge.queued == 6);
  assert(!evaluate_hid(edge).passed);

  HidLatencyMetrics threshold;
  for (uint32_t index = 0; index < 10000; ++index) {
    threshold.observe(1000000, 1019900, true);
  }
  assert(threshold.generated == 10000);
  assert(threshold.queued == 10000);
  assert(threshold.queue_failures == 0);
  assert(threshold.p95_upper_bound_us() == 19900);
  assert(evaluate_hid(threshold).passed);
  threshold.observe(1, 2000000, true);
  assert(!evaluate_hid(threshold).passed);
  threshold = {};
  threshold.observe(1, 0, false);
  assert(!evaluate_hid(threshold).passed);

  ResourceThresholdInput thresholds{
      .steady_free_internal = 65536,
      .steady_largest_internal = 32768,
      .tls_burst_free_internal = 40960,
      .allocation_failures = 0,
  };
  assert(evaluate_resource_thresholds(thresholds).passed);
  for (auto& field : {&(thresholds.steady_free_internal), &(thresholds.steady_largest_internal),
                      &(thresholds.tls_burst_free_internal)}) {
    const uint32_t saved = *field;
    *field = saved - 1;
    assert(!evaluate_resource_thresholds(thresholds).passed);
    *field = saved;
  }
  thresholds.allocation_failures = 1;
  assert(!evaluate_resource_thresholds(thresholds).passed);
  thresholds.allocation_failures = 0;
  assert(evaluate_resource_thresholds(thresholds).passed);

  assert(required_stack_free(4096) == 1024);
  assert(required_stack_free(8192) == 1638);
  assert(evaluate_stack_free({.name = "ok", .configured_bytes = 8192,
                              .high_water_free_bytes = 2000})
             .passed);
  assert(!evaluate_stack_free({.name = "none", .configured_bytes = 0, .high_water_free_bytes = 0})
              .passed);
  assert(!evaluate_stack_free(
      {.name = "tight", .configured_bytes = 5000, .high_water_free_bytes = 1000})
             .passed);
  std::array<TaskStackMetric, 2> stack_metrics{{
      {.name = "first", .configured_bytes = 4096, .high_water_free_bytes = 1024},
      {.name = "second", .configured_bytes = 8192, .high_water_free_bytes = 1638},
  }};
  assert(evaluate_stack_constraints(stack_metrics).passed);
  stack_metrics[1].high_water_free_bytes = 1637;
  const StackConstraintResult stack_failure =
      evaluate_stack_constraints(stack_metrics);
  assert(!stack_failure.passed);
  assert(stack_failure.error == StackConstraintError::insufficient);
  assert(std::string_view(stack_failure.task_name) == "second");

  BurstMetrics burst{
      .window_us = 5000000,
      .wss_frames = 100,
      .wss_bytes = 100u * 16384u,
      .import_bytes = 131072u,
      .session_items = 20,
      .approval_fragments = 4,
      .approval_bytes = 65536,
  };
  assert(validate_transient_burst(burst) == BurstError::none);

  auto expect_burst_error = [&](BurstMetrics metrics,
                                BurstError expected) {
    assert(validate_transient_burst(metrics) == expected);
  };
  expect_burst_error(BurstMetrics{
                         .window_us = 4999999,
                         .wss_frames = burst.wss_frames,
                         .wss_bytes = burst.wss_bytes,
                         .import_bytes = burst.import_bytes,
                         .session_items = burst.session_items,
                         .approval_fragments = burst.approval_fragments,
                         .approval_bytes = burst.approval_bytes,
                     },
                     BurstError::window_duration);
  expect_burst_error(BurstMetrics{
                         .window_us = burst.window_us,
                         .wss_frames = 99,
                         .wss_bytes = burst.wss_bytes,
                         .import_bytes = burst.import_bytes,
                         .session_items = burst.session_items,
                         .approval_fragments = burst.approval_fragments,
                         .approval_bytes = burst.approval_bytes,
                     },
                     BurstError::wss_frame_count);
  expect_burst_error(BurstMetrics{
                         .window_us = burst.window_us,
                         .wss_frames = burst.wss_frames,
                         .wss_bytes = (100u * 16384u) - 1u,
                         .import_bytes = burst.import_bytes,
                         .session_items = burst.session_items,
                         .approval_fragments = burst.approval_fragments,
                         .approval_bytes = burst.approval_bytes,
                     },
                     BurstError::wss_byte_count);
  expect_burst_error(BurstMetrics{
                         .window_us = burst.window_us,
                         .wss_frames = burst.wss_frames,
                         .wss_bytes = burst.wss_bytes,
                         .import_bytes = burst.import_bytes - 1u,
                         .session_items = burst.session_items,
                         .approval_fragments = burst.approval_fragments,
                         .approval_bytes = burst.approval_bytes,
                     },
                     BurstError::import_byte_count);
  expect_burst_error(BurstMetrics{
                         .window_us = burst.window_us,
                         .wss_frames = burst.wss_frames,
                         .wss_bytes = burst.wss_bytes,
                         .import_bytes = burst.import_bytes,
                         .session_items = static_cast<uint16_t>(burst.session_items - 1u),
                         .approval_fragments = burst.approval_fragments,
                         .approval_bytes = burst.approval_bytes,
                     },
                     BurstError::session_item_count);
  expect_burst_error(BurstMetrics{
                         .window_us = burst.window_us,
                         .wss_frames = burst.wss_frames,
                         .wss_bytes = burst.wss_bytes,
                         .import_bytes = burst.import_bytes,
                         .session_items = burst.session_items,
                         .approval_fragments = static_cast<uint16_t>(burst.approval_fragments - 1u),
                         .approval_bytes = burst.approval_bytes,
                     },
                     BurstError::approval_fragment_count);
  expect_burst_error(BurstMetrics{
                         .window_us = burst.window_us,
                         .wss_frames = burst.wss_frames,
                         .wss_bytes = burst.wss_bytes,
                         .import_bytes = burst.import_bytes,
                         .session_items = burst.session_items,
                         .approval_fragments = burst.approval_fragments,
                         .approval_bytes = burst.approval_bytes - 1u,
                     },
                     BurstError::approval_byte_count);

  ResourceSample sample{};
  sample.identity = {
      .run_id = {},
      .boot_id = {},
      .app_elf_sha256 = {},
      .firmware_image_sha256 = {},
      .device_id = {},
  };
  std::fill(sample.identity.run_id.begin(), sample.identity.run_id.end(), 0xAB);
  std::fill(sample.identity.boot_id.begin(), sample.identity.boot_id.end(), 0xBC);
  std::fill(sample.identity.app_elf_sha256.begin(),
            sample.identity.app_elf_sha256.end(), 0xCD);
  std::fill(sample.identity.firmware_image_sha256.begin(),
            sample.identity.firmware_image_sha256.end(), 0xEF);
  std::fill(sample.identity.device_id.begin(), sample.identity.device_id.end(), 0x42);
  std::fill(sample.device_id_sha256.begin(), sample.device_id_sha256.end(), 0xA5);
  sample.monotonic_us = 1234567;
  sample.free_internal_heap = 11111;
  sample.largest_internal_block = 2222;
  sample.allocation_failures = 3;
  sample.https_established = 1;
  sample.https_pending_handshakes = 2;
  sample.hid.generated = 100;
  sample.hid.queued = 100;
  sample.hid.queue_failures = 0;
  sample.hid.buckets.fill(0);
  sample.hid.buckets[1001] = 0;
  sample.hid_queue_failures = 4;
  sample.network_queue_failures = 5;
  sample.display_queue_failures = 6;
  sample.burst = burst;
  sample.tasks[0] = {"scanner", 8192, 2000};
  sample.tasks[1] = {"nimble", 10000, 2500};
  sample.tasks[2] = {"https", 12000, 3000};
  sample.tasks[3] = {"wss", 14000, 3500};
  sample.tasks[4] = {"display", 16000, 4000};
  sample.tasks[5] = {"metrics", 18000, 4500};
  sample.tasks[6] = {"unused", 0, 0};

  std::array<char, 4096> encoded{};
  assert(emit_resource_sample_line(sample, "baseline", encoded) == ResourceEncodeError::none);
  const char* encoded_text = encoded.data();
  assert(std::strstr(encoded_text, "\"run_id\":\"") != nullptr);
  assert(std::strstr(encoded_text, "\"boot_id\":\"") != nullptr);
  assert(std::strstr(encoded_text, "\"app_elf_sha256\":\"") != nullptr);
  assert(std::strstr(encoded_text, "\"firmware_image_sha256\":\"") != nullptr);
  assert(std::strstr(encoded_text, "\"device_id_sha256\":\"") != nullptr);
  assert(std::strstr(encoded_text,
                     "\"device_id_sha256\":\"a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5"
                     "a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5\"},\"scenario\":") !=
         nullptr);
  assert(std::strstr(encoded_text, "\"device_id\":\"") == nullptr);
  assert(std::strstr(encoded_text, "42424242424242424242424242424242") ==
         nullptr);
  assert(std::strstr(encoded_text, "\"scenario\":\"baseline\"") != nullptr);
  assert(std::strstr(encoded_text, "\"tasks\":[") != nullptr);
  const size_t encoded_length = std::strlen(encoded_text);
  assert(encoded_length > 0);
  assert(encoded[encoded_length - 1] == '\n');
  assert(encoded[encoded_length] == '\0');
  assert(structurally_complete_json_object(
      std::string_view(encoded_text, encoded_length)));

  std::string oversized(4500, 'x');
  std::array<char, 4096> tiny{};
  assert(emit_resource_sample_line(sample, oversized, tiny) == ResourceEncodeError::truncated);

  assert(emit_resource_sample_line(sample, "", encoded) == ResourceEncodeError::no_buffer);

  return 0;
}
