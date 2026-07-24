#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "probe_types.hpp"

inline constexpr size_t kHidQueueDepth = 32;
inline constexpr size_t kNetworkQueueDepth = 16;
inline constexpr size_t kDisplayQueueDepth = 8;
inline constexpr uint64_t kTransientBurstWindowUs = 5000000;

struct HidLatencyMetrics {
  uint32_t generated = 0;
  uint32_t queued = 0;
  uint32_t queue_failures = 0;
  std::array<uint32_t, 1002> buckets{};

  void observe(int64_t stable_at_us, int64_t queued_at_us, bool queued_ok);
  [[nodiscard]] uint32_t p95_upper_bound_us() const;
};

struct TaskStackMetric {
  const char* name;
  uint32_t configured_bytes;
  uint32_t high_water_free_bytes;
};

struct ResourceThresholdInput {
  uint32_t steady_free_internal = 0;
  uint32_t steady_largest_internal = 0;
  uint32_t tls_burst_free_internal = 0;
  uint32_t allocation_failures = 0;
};

enum class ResourceThresholdError {
  none,
  steady_free_internal,
  steady_largest_internal,
  tls_burst_free_internal,
  allocation_failures,
};

struct ResourceThresholdResult {
  bool passed = false;
  ResourceThresholdError error = ResourceThresholdError::none;
};

struct HidThresholdResult {
  bool passed = false;
  enum class Error {
    none,
    insufficient_generated,
    queue_imbalance,
    queue_failures,
    overflow_bucket,
    p95_too_high,
  } error = Error::none;
};

enum class StackConstraintError {
  none,
  unavailable,
  insufficient,
};

struct StackConstraintResult {
  bool passed = false;
  StackConstraintError error = StackConstraintError::none;
  const char* task_name = "";
};

struct BurstMetrics {
  uint64_t window_us = 0;
  uint32_t wss_frames = 0;
  uint32_t wss_bytes = 0;
  uint32_t import_bytes = 0;
  uint16_t session_items = 0;
  uint16_t approval_fragments = 0;
  uint32_t approval_bytes = 0;
};

enum class BurstError {
  none,
  window_duration,
  wss_frame_count,
  wss_byte_count,
  import_byte_count,
  session_item_count,
  approval_fragment_count,
  approval_byte_count,
};

enum class ResourceEncodeError {
  none,
  no_buffer,
  truncated,
};

struct ResourceSample {
  ProbeIdentity identity;
  std::array<uint8_t, 32> device_id_sha256{};
  uint64_t monotonic_us = 0;
  uint32_t free_internal_heap = 0;
  uint32_t largest_internal_block = 0;
  uint32_t allocation_failures = 0;
  uint8_t https_established = 0;
  uint8_t https_pending_handshakes = 0;
  uint32_t hid_queue_failures = 0;
  uint32_t network_queue_failures = 0;
  uint32_t display_queue_failures = 0;
  uint32_t metrics_encode_failures = 0;
  HidLatencyMetrics hid;
  BurstMetrics burst;
  std::array<TaskStackMetric, 7> tasks{};
};

ResourceThresholdResult evaluate_resource_thresholds(
    const ResourceThresholdInput& input);
HidThresholdResult evaluate_hid(const HidLatencyMetrics& hid);
ResourceEncodeError emit_resource_sample_line(
    const ResourceSample& sample,
    std::string_view scenario,
    std::span<char, 4096> line);
[[nodiscard]] uint32_t required_stack_free(uint32_t configured_bytes);
[[nodiscard]] StackConstraintResult evaluate_stack_free(
    const TaskStackMetric& metric);
StackConstraintResult evaluate_stack_constraints(
    std::span<const TaskStackMetric> metrics);
BurstError validate_transient_burst(const BurstMetrics& metrics);
[[nodiscard]] bool should_begin_transient_window(
    uint64_t start_us, uint64_t end_us, uint64_t observed_at_us);
