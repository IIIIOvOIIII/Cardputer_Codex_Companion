#include "probe/resource_metrics.hpp"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {

constexpr uint32_t kMaxLatencyUs = 100000;
constexpr size_t kOverflowBucket = 1001;
constexpr size_t kMinStackBytes = 1024;

struct JsonWriter {
  explicit JsonWriter(std::span<char> target) : data(target) {}

  bool append(std::string_view text) {
    if (truncated) {
      return false;
    }
    if (offset + text.size() + 1 > data.size()) {
      truncated = true;
      return false;
    }
    std::copy(text.begin(), text.end(), data.begin() + static_cast<std::ptrdiff_t>(offset));
    offset += text.size();
    return true;
  }

  bool append_hex(std::span<const uint8_t> bytes) {
    if (truncated) {
      return false;
    }
    if (offset + (bytes.size() * 2u) + 1 > data.size()) {
      truncated = true;
      return false;
    }
    for (const uint8_t value : bytes) {
      const int wrote = std::snprintf(data.data() + offset,
                                      data.size() - offset, "%02x",
                                      static_cast<int>(value));
      if (wrote != 2) {
        truncated = true;
        return false;
      }
      offset += 2;
    }
    return true;
  }

  template <typename... Args>
  bool append_format(const char* format, Args... args) {
    if (truncated) {
      return false;
    }
    const int wrote = std::snprintf(data.data() + offset,
                                    data.size() - offset, format,
                                    args...);
    if (wrote < 0 || static_cast<size_t>(wrote) >= data.size() - offset) {
      truncated = true;
      return false;
    }
    offset += static_cast<size_t>(wrote);
    return true;
  }

  void append_null() {
    if (offset < data.size()) {
      data[offset] = '\0';
    }
  }

  std::span<char> data{};
  size_t offset = 0;
  bool truncated = false;
};

uint32_t latency_bucket(int64_t stable_at_us, int64_t queued_at_us) {
  if (queued_at_us < stable_at_us) {
    return 0;
  }
  const uint64_t delta_us = static_cast<uint64_t>(queued_at_us - stable_at_us);
  if (delta_us == 0 || delta_us > kMaxLatencyUs) {
    return delta_us == 0 ? 0u : kOverflowBucket;
  }
  return static_cast<uint32_t>((delta_us - 1u) / 100u + 1u);
}

}  // namespace

void HidLatencyMetrics::observe(int64_t stable_at_us, int64_t queued_at_us,
                               bool queued_ok) {
  ++generated;
  if (!queued_ok) {
    ++queue_failures;
    return;
  }
  ++queued;
  ++buckets[latency_bucket(stable_at_us, queued_at_us)];
}

uint32_t HidLatencyMetrics::p95_upper_bound_us() const {
  if (queued == 0) {
    return 0;
  }

  const uint32_t rank = static_cast<uint32_t>(
      (static_cast<uint64_t>(queued) * 95u + 99u) / 100u);
  uint32_t cumulative = 0;
  for (uint32_t bucket = 0; bucket < buckets.size(); ++bucket) {
    cumulative += buckets[bucket];
    if (cumulative >= rank) {
      return bucket == kOverflowBucket ? kMaxLatencyUs : bucket * 100u;
    }
  }
  return kMaxLatencyUs;
}

ResourceThresholdResult evaluate_resource_thresholds(
    const ResourceThresholdInput& input) {
  if (input.steady_free_internal < 65536) {
    return {false, ResourceThresholdError::steady_free_internal};
  }
  if (input.steady_largest_internal < 32768) {
    return {false, ResourceThresholdError::steady_largest_internal};
  }
  if (input.tls_burst_free_internal < 40960) {
    return {false, ResourceThresholdError::tls_burst_free_internal};
  }
  if (input.allocation_failures != 0) {
    return {false, ResourceThresholdError::allocation_failures};
  }
  return {true, ResourceThresholdError::none};
}

HidThresholdResult evaluate_hid(const HidLatencyMetrics& hid) {
  if (hid.generated < 10000) {
    return {false, HidThresholdResult::Error::insufficient_generated};
  }
  if (hid.generated != hid.queued) {
    return {false, HidThresholdResult::Error::queue_imbalance};
  }
  if (hid.queue_failures != 0) {
    return {false, HidThresholdResult::Error::queue_failures};
  }
  if (hid.buckets[kOverflowBucket] != 0) {
    return {false, HidThresholdResult::Error::overflow_bucket};
  }
  if (hid.p95_upper_bound_us() > 20000) {
    return {false, HidThresholdResult::Error::p95_too_high};
  }
  return {true, HidThresholdResult::Error::none};
}

uint32_t required_stack_free(uint32_t configured_bytes) {
  const uint32_t one_fifth = configured_bytes / 5u;
  return one_fifth > kMinStackBytes ? one_fifth : kMinStackBytes;
}

StackConstraintResult evaluate_stack_free(const TaskStackMetric& metric) {
  if (metric.configured_bytes == 0) {
    return {false, StackConstraintError::unavailable, metric.name};
  }
  if (metric.high_water_free_bytes < required_stack_free(metric.configured_bytes)) {
    return {false, StackConstraintError::insufficient, metric.name};
  }
  return {true, StackConstraintError::none, metric.name};
}

StackConstraintResult evaluate_stack_constraints(
    std::span<const TaskStackMetric> metrics) {
  for (const auto& metric : metrics) {
    const auto result = evaluate_stack_free(metric);
    if (!result.passed) {
      return result;
    }
  }
  return {true, StackConstraintError::none, ""};
}

BurstError validate_transient_burst(const BurstMetrics& metrics) {
  if (metrics.window_us != kTransientBurstWindowUs) {
    return BurstError::window_duration;
  }
  if (metrics.wss_frames != 100u) {
    return BurstError::wss_frame_count;
  }
  if (metrics.wss_bytes != 100u * 16384u) {
    return BurstError::wss_byte_count;
  }
  if (metrics.import_bytes != 131072u) {
    return BurstError::import_byte_count;
  }
  if (metrics.session_items != 20u) {
    return BurstError::session_item_count;
  }
  if (metrics.approval_fragments != 4u) {
    return BurstError::approval_fragment_count;
  }
  if (metrics.approval_bytes != 65536u) {
    return BurstError::approval_byte_count;
  }
  return BurstError::none;
}

bool should_begin_transient_window(uint64_t start_us, uint64_t end_us,
                                   uint64_t observed_at_us) {
  return start_us == 0 || end_us <= start_us || observed_at_us >= end_us;
}

ResourceEncodeError emit_resource_sample_line(
    const ResourceSample& sample,
    std::string_view scenario,
    std::span<char, 4096> line) {
  if (line.size() == 0 || scenario.empty()) {
    return ResourceEncodeError::no_buffer;
  }

  JsonWriter writer(line);

  if (!writer.append("{\"identity\":{"
                     "\"run_id\":\"")
      || !writer.append_hex(std::span<const uint8_t>(sample.identity.run_id.data(),
                                                     sample.identity.run_id.size()))
      || !writer.append("\",\"boot_id\":\"")
      || !writer.append_hex(std::span<const uint8_t>(sample.identity.boot_id.data(),
                                                     sample.identity.boot_id.size()))
      || !writer.append("\",\"app_elf_sha256\":\"")
      || !writer.append_hex(std::span<const uint8_t>(
                             sample.identity.app_elf_sha256.data(),
                             sample.identity.app_elf_sha256.size()))
      || !writer.append("\",\"firmware_image_sha256\":\"")
      || !writer.append_hex(std::span<const uint8_t>(
                             sample.identity.firmware_image_sha256.data(),
                             sample.identity.firmware_image_sha256.size()))
      || !writer.append("\",\"device_id_sha256\":\"")
      || !writer.append_hex(std::span<const uint8_t>(
                             sample.device_id_sha256.data(),
                             sample.device_id_sha256.size()))
      || !writer.append("\"},\"scenario\":\"")
      || !writer.append(scenario)
      || !writer.append("\",\"monotonic_us\":")
      || !writer.append_format("%" PRIu64, sample.monotonic_us)
      || !writer.append(",\"https_established\":")
      || !writer.append_format("%" PRIu8, sample.https_established)
      || !writer.append(",\"https_pending_handshakes\":")
      || !writer.append_format("%" PRIu8, sample.https_pending_handshakes)
      || !writer.append(",\"free_internal_heap\":")
      || !writer.append_format("%" PRIu32, sample.free_internal_heap)
      || !writer.append(",\"largest_internal_block\":")
      || !writer.append_format("%" PRIu32, sample.largest_internal_block)
      || !writer.append(",\"allocation_failures\":")
      || !writer.append_format("%" PRIu32, sample.allocation_failures)
      || !writer.append(",\"metrics_encode_failures\":")
      || !writer.append_format("%" PRIu32, sample.metrics_encode_failures)
      || !writer.append(",\"hid\":{\"generated\":")
      || !writer.append_format("%" PRIu32, sample.hid.generated)
      || !writer.append(",\"queued\":")
      || !writer.append_format("%" PRIu32, sample.hid.queued)
      || !writer.append(",\"queue_failures\":")
      || !writer.append_format("%" PRIu32, sample.hid.queue_failures)
      || !writer.append(",\"p95_upper_bound_us\":")
      || !writer.append_format("%" PRIu32, sample.hid.p95_upper_bound_us())
      || !writer.append("},\"audio\":{\"captured_frames\":")
      || !writer.append_format("%" PRIu32, sample.audio.captured_frames)
      || !writer.append(",\"source_overruns\":")
      || !writer.append_format("%" PRIu32, sample.audio.source_overruns)
      || !writer.append(",\"transport_drops\":")
      || !writer.append_format("%" PRIu32, sample.audio.transport_drops)
      || !writer.append(",\"fallback_count\":")
      || !writer.append_format("%" PRIu32, sample.audio.fallback_count)
      || !writer.append("},\"overflows\":{\"hid\":")
      || !writer.append_format("%" PRIu32, sample.hid_queue_failures)
      || !writer.append(",\"network\":")
      || !writer.append_format("%" PRIu32, sample.network_queue_failures)
      || !writer.append(",\"display\":")
      || !writer.append_format("%" PRIu32, sample.display_queue_failures)
      || !writer.append("},\"burst\":{\"window_us\":")
      || !writer.append_format("%" PRIu64, sample.burst.window_us)
      || !writer.append(",\"wss_frames\":")
      || !writer.append_format("%" PRIu32, sample.burst.wss_frames)
      || !writer.append(",\"wss_bytes\":")
      || !writer.append_format("%" PRIu32, sample.burst.wss_bytes)
      || !writer.append(",\"import_bytes\":")
      || !writer.append_format("%" PRIu32, sample.burst.import_bytes)
      || !writer.append(",\"session_items\":")
      || !writer.append_format("%" PRIu16, sample.burst.session_items)
      || !writer.append(",\"approval_fragments\":")
      || !writer.append_format("%" PRIu16, sample.burst.approval_fragments)
      || !writer.append(",\"approval_bytes\":")
      || !writer.append_format("%" PRIu32, sample.burst.approval_bytes)
      || !writer.append("},\"tasks\":[")) {
    return ResourceEncodeError::truncated;
  }

  for (size_t index = 0; index < sample.tasks.size(); ++index) {
    const auto& metric = sample.tasks[index];
    if (index > 0 && !writer.append(",")) {
      return ResourceEncodeError::truncated;
    }
    if (!writer.append("{\"name\":\"") ||
        !writer.append(metric.name == nullptr ? "" : metric.name) ||
        !writer.append("\",\"configured\":") ||
        !writer.append_format("%" PRIu32, metric.configured_bytes) ||
        !writer.append(",\"high_water_free_bytes\":") ||
        !writer.append_format("%" PRIu32, metric.high_water_free_bytes) ||
        !writer.append("}")) {
      return ResourceEncodeError::truncated;
    }
  }

  if (!writer.append("]}") || !writer.append("\n")) {
    return ResourceEncodeError::truncated;
  }

  writer.append_null();
  if (writer.truncated) {
    return ResourceEncodeError::truncated;
  }
  return ResourceEncodeError::none;
}
