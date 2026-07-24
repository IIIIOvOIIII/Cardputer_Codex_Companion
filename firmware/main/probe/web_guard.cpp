#include "probe/web_guard.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#ifndef ESP_PLATFORM
#include <openssl/sha.h>
#else
#include "mbedtls/sha256.h"
#endif

namespace {

constexpr uint64_t kPairingWindowMs = 300000;
constexpr uint64_t kPairingBackoffMs = 600000;
constexpr uint64_t kSessionIdleMs = 1800000;
constexpr uint32_t kHeaderLimit = 8192;
constexpr uint32_t kNormalBodyLimit = 16384;
constexpr uint32_t kImportBodyLimit = 131072;
constexpr std::string_view kHealthPath = "/healthz";
constexpr std::string_view kPairingPath = "/api/v1/web-pairing/submit";
constexpr std::string_view kImportPath = "/api/v1/config/import";

using Digest = std::array<uint8_t, 32>;

uint64_t saturating_add(uint64_t value, uint64_t increment) {
  if (increment > std::numeric_limits<uint64_t>::max() - value) {
    return std::numeric_limits<uint64_t>::max();
  }
  return value + increment;
}

Digest sha256(std::span<const uint8_t> value) {
  Digest digest{};
#ifndef ESP_PLATFORM
  SHA256(value.data(), value.size(), digest.data());
#else
  if (mbedtls_sha256(value.data(), value.size(), digest.data(), 0) != 0) {
    throw std::runtime_error("sha256 failed");
  }
#endif
  return digest;
}

Digest sha256(std::string_view value) {
  return sha256(std::span<const uint8_t>(
      reinterpret_cast<const uint8_t*>(value.data()), value.size()));
}

bool constant_time_equal(const Digest& left, const Digest& right) {
  uint8_t difference = 0;
  for (size_t index = 0; index < left.size(); ++index) {
    difference |= static_cast<uint8_t>(left[index] ^ right[index]);
  }
  return difference == 0;
}

bool is_eight_digit_code(std::string_view code) {
  return code.size() == 8 &&
         std::all_of(code.begin(), code.end(),
                     [](char value) { return value >= '0' && value <= '9'; });
}

Digest pairing_code_digest(std::string_view code) {
  std::array<uint8_t, 8> normalized{};
  const size_t copy_bytes = std::min(code.size(), normalized.size());
  std::copy_n(reinterpret_cast<const uint8_t*>(code.data()), copy_bytes,
              normalized.begin());
  return sha256(normalized);
}

std::string hex_token(std::span<const uint8_t> bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string output(bytes.size() * 2, '0');
  for (size_t index = 0; index < bytes.size(); ++index) {
    output[index * 2] = digits[bytes[index] >> 4];
    output[index * 2 + 1] = digits[bytes[index] & 0x0f];
  }
  return output;
}

size_t utf8_prefix_bytes(std::string_view value, size_t limit) {
  size_t length = std::min(value.size(), limit);
  while (length > 0 && length < value.size() &&
         (static_cast<uint8_t>(value[length]) & 0xc0) == 0x80) {
    --length;
  }
  return length;
}

bool is_write(HttpMethod method) {
  return method == HttpMethod::post || method == HttpMethod::put ||
         method == HttpMethod::delete_;
}

}  // namespace

RequestBudget RequestBudget::for_request(std::string_view path,
                                         uint32_t header_bytes,
                                         uint32_t content_length) {
  const uint32_t body_limit =
      path == kImportPath ? kImportBodyLimit : kNormalBodyLimit;
  if (header_bytes > kHeaderLimit) {
    return {
        .reason = WebReject::header_too_large,
        .limit = kHeaderLimit,
        .consumed = 0,
    };
  }
  if (content_length > body_limit) {
    return {
        .reason = WebReject::body_too_large,
        .limit = body_limit,
        .consumed = 0,
    };
  }
  return {
      .reason = WebReject::none,
      .limit = body_limit,
      .consumed = 0,
  };
}

bool RequestBudget::consume(uint32_t chunk_bytes) {
  if (reason != WebReject::none || chunk_bytes > limit - consumed) {
    reason = WebReject::body_too_large;
    return false;
  }
  consumed += chunk_bytes;
  return true;
}

bool JsonDepthTracker::consume(std::string_view chunk) {
  if (!valid_) {
    return false;
  }
  for (char value : chunk) {
    if (in_string_) {
      if (escaped_) {
        escaped_ = false;
      } else if (value == '\\') {
        escaped_ = true;
      } else if (value == '"') {
        in_string_ = false;
      }
      continue;
    }

    if (value == '"') {
      in_string_ = true;
      continue;
    }
    if (value == '{' || value == '[') {
      if (depth_ >= 8) {
        valid_ = false;
        return false;
      }
      ++depth_;
      maximum_depth_ = std::max(maximum_depth_, depth_);
    } else if (value == '}' || value == ']') {
      if (depth_ == 0) {
        valid_ = false;
        return false;
      }
      --depth_;
    }
  }
  return true;
}

uint8_t JsonDepthTracker::maximum_depth() const {
  return maximum_depth_;
}

WebGuard::WebGuard(std::string expected_host, RandomSource& random)
    : expected_host_(std::move(expected_host)),
      expected_origin_("https://" + expected_host_),
      random_(random) {}

void WebGuard::open_pairing_window(std::string_view eight_digit_code,
                                   uint64_t now_ms) {
  if (now_ms < backoff_until_ms_ || !is_eight_digit_code(eight_digit_code)) {
    pairing_window_open_ = false;
    awaiting_physical_confirmation_ = false;
    pending_browser_name_.clear();
    pairing_code_digest_.fill(0);
    return;
  }

  pairing_code_digest_ = pairing_code_digest(eight_digit_code);
  pairing_window_expires_ms_ = saturating_add(now_ms, kPairingWindowMs);
  failed_pairing_attempts_ = 0;
  pairing_window_open_ = true;
  awaiting_physical_confirmation_ = false;
  pending_browser_name_.clear();
}

PairingResult WebGuard::submit_pairing_code(std::string_view code,
                                            std::string_view browser_name,
                                            uint64_t now_ms) {
  if (now_ms < backoff_until_ms_) {
    return PairingResult::in_backoff;
  }
  if (!pairing_window_open_ || now_ms > pairing_window_expires_ms_) {
    pairing_window_open_ = false;
    return PairingResult::window_closed;
  }

  const Digest submitted = pairing_code_digest(code);
  const bool matches =
      constant_time_equal(pairing_code_digest_, submitted) &&
      is_eight_digit_code(code);
  if (!matches) {
    ++failed_pairing_attempts_;
    if (failed_pairing_attempts_ >= 5) {
      pairing_window_open_ = false;
      pairing_code_digest_.fill(0);
      backoff_until_ms_ = saturating_add(now_ms, kPairingBackoffMs);
      return PairingResult::backoff_started;
    }
    return PairingResult::invalid_code;
  }

  pairing_window_open_ = false;
  pairing_code_digest_.fill(0);
  awaiting_physical_confirmation_ = true;
  pending_browser_name_.assign(
      browser_name.substr(0, utf8_prefix_bytes(browser_name, 64)));
  return PairingResult::awaiting_physical_confirmation;
}

std::optional<AdminCredential> WebGuard::confirm_pairing(bool accepted,
                                                         uint64_t now_ms) {
  if (!awaiting_physical_confirmation_) {
    return std::nullopt;
  }
  awaiting_physical_confirmation_ = false;

  if (!accepted || now_ms > pairing_window_expires_ms_) {
    pending_browser_name_.clear();
    return std::nullopt;
  }

  Session* available = nullptr;
  for (Session& session : sessions_) {
    const bool expired =
        session.occupied && now_ms >= session.last_used_ms &&
        now_ms - session.last_used_ms > kSessionIdleMs;
    if (expired) {
      clear_session(session);
    }
    if (!session.occupied && available == nullptr) {
      available = &session;
    }
  }
  if (available == nullptr) {
    pending_browser_name_.clear();
    return std::nullopt;
  }

  std::array<uint8_t, 32> cookie_bytes{};
  std::array<uint8_t, 32> csrf_bytes{};
  random_.fill(cookie_bytes);
  random_.fill(csrf_bytes);
  AdminCredential credential{
      .cookie_token = hex_token(cookie_bytes),
      .csrf_token = hex_token(csrf_bytes),
  };

  available->cookie_digest = sha256(credential.cookie_token);
  available->csrf_digest = sha256(credential.csrf_token);
  const size_t browser_bytes =
      std::min(pending_browser_name_.size(), available->browser_name.size());
  std::copy_n(pending_browser_name_.begin(), browser_bytes,
              available->browser_name.begin());
  available->browser_name_bytes = static_cast<uint8_t>(browser_bytes);
  available->last_used_ms = now_ms;
  available->rate = {};
  available->occupied = true;
  pending_browser_name_.clear();
  return credential;
}

bool WebGuard::has_admin_session() const {
  return admin_session_count() != 0;
}

uint8_t WebGuard::admin_session_count() const {
  return static_cast<uint8_t>(
      std::count_if(sessions_.begin(), sessions_.end(),
                    [](const Session& session) { return session.occupied; }));
}

uint64_t WebGuard::backoff_remaining_ms(uint64_t now_ms) const {
  return now_ms < backoff_until_ms_ ? backoff_until_ms_ - now_ms : 0;
}

WebDecision WebGuard::authorize(const RequestMeta& request) {
  const RequestBudget budget = RequestBudget::for_request(
      request.path, request.header_bytes, request.content_length);
  if (!budget.accepted()) {
    return decision(budget.reason);
  }

  SourceRate* request_rate = nullptr;
  const bool credential_present = !request.cookie_token.empty();
  if (request.path == kHealthPath) {
    if (!health_rate_.consume(10, 10, request.now_ms)) {
      return decision(WebReject::rate_limited);
    }
  } else {
    request_rate = source_rate(request.source);
    if (request_rate == nullptr) {
      return decision(WebReject::rate_limited);
    }
    TokenBucket& bucket =
        credential_present ? request_rate->credential_present
                           : request_rate->unauthenticated;
    if (!bucket.consume(credential_present ? 10 : 1,
                        credential_present ? 20 : 4, request.now_ms)) {
      return decision(WebReject::rate_limited);
    }
  }

  if (request.host != expected_host_) {
    return decision(WebReject::host_mismatch);
  }
  if (request.path == kHealthPath) {
    return decision(WebReject::none);
  }

  if (request.path == kPairingPath) {
    if (request.method != HttpMethod::post ||
        request.origin != expected_origin_) {
      return decision(WebReject::origin_mismatch);
    }
    return decision(WebReject::none);
  }

  Session* session = find_session(request.cookie_token);
  if (session == nullptr) {
    if (credential_present &&
        !request_rate->unauthenticated.consume(1, 4, request.now_ms)) {
      return decision(WebReject::rate_limited);
    }
    return decision(WebReject::unauthenticated);
  }
  if (request.now_ms >= session->last_used_ms &&
      request.now_ms - session->last_used_ms > kSessionIdleMs) {
    clear_session(*session);
    return decision(WebReject::session_expired);
  }
  if (!session->rate.consume(10, 20, request.now_ms)) {
    return decision(WebReject::rate_limited);
  }

  if (is_write(request.method)) {
    if (request.origin != expected_origin_) {
      return decision(WebReject::origin_mismatch);
    }
    const Digest submitted_csrf = sha256(request.csrf_token);
    if (!constant_time_equal(session->csrf_digest, submitted_csrf)) {
      return decision(WebReject::csrf_mismatch);
    }
  }

  session->last_used_ms = request.now_ms;
  return decision(WebReject::none);
}

std::optional<std::string> WebGuard::issue_csrf_token(
    std::string_view cookie_token, uint64_t now_ms) {
  Session* session = find_session(cookie_token);
  if (session == nullptr) {
    return std::nullopt;
  }
  if (now_ms >= session->last_used_ms &&
      now_ms - session->last_used_ms > kSessionIdleMs) {
    clear_session(*session);
    return std::nullopt;
  }

  std::array<uint8_t, 32> csrf_bytes{};
  random_.fill(csrf_bytes);
  std::string csrf_token = hex_token(csrf_bytes);
  session->csrf_digest = sha256(csrf_token);
  session->last_used_ms = now_ms;
  return csrf_token;
}

bool WebGuard::TokenBucket::consume(uint32_t rate_per_second, uint32_t burst,
                                    uint64_t now_ms) {
  const uint64_t capacity = static_cast<uint64_t>(burst) * 1000;
  if (!initialized) {
    tokens_milli = static_cast<uint32_t>(capacity);
    updated_ms = now_ms;
    initialized = true;
  } else if (now_ms >= updated_ms) {
    const uint64_t elapsed = now_ms - updated_ms;
    const uint64_t refill =
        elapsed > std::numeric_limits<uint64_t>::max() / rate_per_second
            ? capacity
            : elapsed * static_cast<uint64_t>(rate_per_second);
    tokens_milli = static_cast<uint32_t>(
        std::min<uint64_t>(capacity, tokens_milli + refill));
    updated_ms = now_ms;
  }
  if (tokens_milli < 1000) {
    return false;
  }
  tokens_milli -= 1000;
  return true;
}

WebGuard::SourceRate* WebGuard::source_rate(const SourceKey& source) {
  SourceRate* available = nullptr;
  for (SourceRate& rate : source_rates_) {
    if (rate.occupied && rate.source == source) {
      return &rate;
    }
    if (!rate.occupied && available == nullptr) {
      available = &rate;
    }
  }
  if (available != nullptr) {
    available->source = source;
    available->occupied = true;
  }
  return available;
}

WebGuard::Session* WebGuard::find_session(std::string_view cookie_token) {
  const Digest submitted = sha256(cookie_token);
  Session* match = nullptr;
  for (Session& session : sessions_) {
    const bool equal =
        constant_time_equal(session.cookie_digest, submitted) &&
        session.occupied;
    if (equal && match == nullptr) {
      match = &session;
    }
  }
  return match;
}

void WebGuard::clear_session(Session& session) {
  session.cookie_digest.fill(0);
  session.csrf_digest.fill(0);
  session.browser_name.fill(0);
  session.browser_name_bytes = 0;
  session.last_used_ms = 0;
  session.rate = {};
  session.occupied = false;
}

WebDecision WebGuard::decision(WebReject reason) {
  switch (reason) {
    case WebReject::none:
      return {.reason = reason, .http_status_code = 200};
    case WebReject::unauthenticated:
    case WebReject::session_expired:
      return {.reason = reason, .http_status_code = 401};
    case WebReject::host_mismatch:
    case WebReject::origin_mismatch:
    case WebReject::csrf_mismatch:
    case WebReject::pairing_required:
      return {.reason = reason, .http_status_code = 403};
    case WebReject::header_too_large:
    case WebReject::body_too_large:
    case WebReject::json_too_deep:
    case WebReject::frame_too_large:
      return {.reason = reason, .http_status_code = 413};
    case WebReject::rate_limited:
      return {.reason = reason, .http_status_code = 429};
  }
  return {.reason = reason, .http_status_code = 403};
}
