#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "probe/pre_tls_limiter.hpp"

class RandomSource {
 public:
  virtual ~RandomSource() = default;
  virtual void fill(std::span<uint8_t> output) = 0;
};

class WebPairingPhysicalSink {
 public:
  virtual ~WebPairingPhysicalSink() = default;
  virtual void open_pairing_window(std::string_view eight_digit_code,
                                   uint64_t now_ms) = 0;
  virtual void confirm_pairing(bool accepted, uint64_t now_ms) = 0;
};

enum class PairingResult {
  invalid_code,
  awaiting_physical_confirmation,
  backoff_started,
  in_backoff,
  window_closed,
};

enum class HttpMethod { get, post, put, delete_ };

enum class WebReject {
  none,
  rate_limited,
  header_too_large,
  body_too_large,
  json_too_deep,
  frame_too_large,
  unauthenticated,
  session_expired,
  host_mismatch,
  origin_mismatch,
  csrf_mismatch,
  pairing_required,
};

struct AdminCredential {
  std::string cookie_token;
  std::string csrf_token;
};

struct RequestMeta {
  HttpMethod method = HttpMethod::get;
  std::string_view path;
  SourceKey source{};
  std::string_view host;
  std::string_view origin;
  std::string_view cookie_token;
  std::string_view csrf_token;
  uint32_t header_bytes = 0;
  uint32_t content_length = 0;
  uint64_t now_ms = 0;
};

struct WebDecision {
  WebReject reason = WebReject::none;
  uint16_t http_status_code = 200;
  bool operator==(const WebDecision&) const = default;
};

struct RequestBudget {
  WebReject reason = WebReject::none;
  uint32_t limit = 0;
  uint32_t consumed = 0;

  static RequestBudget for_request(std::string_view path,
                                   uint32_t header_bytes,
                                   uint32_t content_length);
  [[nodiscard]] bool accepted() const {
    return reason == WebReject::none;
  }
  bool consume(uint32_t chunk_bytes);
};

class JsonDepthTracker {
 public:
  bool consume(std::string_view chunk);
  [[nodiscard]] uint8_t maximum_depth() const;

 private:
  uint8_t depth_ = 0;
  uint8_t maximum_depth_ = 0;
  bool in_string_ = false;
  bool escaped_ = false;
  bool valid_ = true;
};

class PairingResponseWindow {
 public:
  bool begin(uint64_t now_ms);
  void finish();
  [[nodiscard]] bool pending() const;
  [[nodiscard]] bool expired(uint64_t now_ms) const;
  [[nodiscard]] uint64_t deadline_ms() const;
  [[nodiscard]] uint64_t remaining_ms(uint64_t now_ms) const;

 private:
  uint64_t deadline_ms_ = 0;
  bool pending_ = false;
};

class WebGuard {
 public:
  WebGuard(std::string expected_host, RandomSource& random);
  void open_pairing_window(std::string_view eight_digit_code,
                           uint64_t now_ms);
  PairingResult submit_pairing_code(std::string_view code,
                                    std::string_view browser_name,
                                    uint64_t now_ms);
  std::optional<AdminCredential> confirm_pairing(bool accepted,
                                                  uint64_t now_ms);
  void cancel_pairing_confirmation();
  [[nodiscard]] bool has_admin_session() const;
  [[nodiscard]] uint8_t admin_session_count() const;
  [[nodiscard]] uint64_t backoff_remaining_ms(uint64_t now_ms) const;
  WebDecision authorize(const RequestMeta& request);
  std::optional<std::string> issue_csrf_token(
      std::string_view cookie_token, uint64_t now_ms);

 private:
  using Digest = std::array<uint8_t, 32>;

  struct TokenBucket {
    uint32_t tokens_milli = 0;
    uint64_t updated_ms = 0;
    bool initialized = false;

    bool consume(uint32_t rate_per_second, uint32_t burst, uint64_t now_ms);
  };

  struct SourceRate {
    SourceKey source{};
    TokenBucket unauthenticated;
    TokenBucket credential_present;
    bool occupied = false;
  };

  struct Session {
    Digest cookie_digest{};
    Digest csrf_digest{};
    std::array<char, 64> browser_name{};
    uint8_t browser_name_bytes = 0;
    uint64_t last_used_ms = 0;
    TokenBucket rate;
    bool occupied = false;
  };

  SourceRate* source_rate(const SourceKey& source);
  Session* find_session(std::string_view cookie_token);
  static void clear_session(Session& session);
  static WebDecision decision(WebReject reason);

  std::string expected_host_;
  std::string expected_origin_;
  RandomSource& random_;
  Digest pairing_code_digest_{};
  std::string pending_browser_name_;
  uint64_t pairing_window_expires_ms_ = 0;
  uint64_t backoff_until_ms_ = 0;
  uint8_t failed_pairing_attempts_ = 0;
  bool pairing_window_open_ = false;
  bool awaiting_physical_confirmation_ = false;
  std::array<Session, 5> sessions_{};
  std::array<SourceRate, 16> source_rates_{};
  TokenBucket health_rate_{};
};
