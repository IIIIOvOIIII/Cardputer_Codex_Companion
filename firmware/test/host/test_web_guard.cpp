#include <array>
#include <cassert>
#include <cstdint>
#include <span>
#include <string>

#include "probe/web_guard.hpp"
#include "probe/web_route_manifest.hpp"

namespace {

constexpr char kHost[] = "cardputer-codex-3f2a.local";
constexpr char kOrigin[] = "https://cardputer-codex-3f2a.local";

class DeterministicRandom final : public RandomSource {
 public:
  explicit DeterministicRandom(uint8_t first) : next_(first) {}

  void fill(std::span<uint8_t> output) override {
    for (uint8_t& byte : output) {
      byte = next_++;
    }
  }

 private:
  uint8_t next_;
};

SourceKey source(uint8_t last) {
  SourceKey value{};
  value.bytes[15] = last;
  return value;
}

void expect_decision(WebDecision actual, WebReject reason,
                     uint16_t http_status_code) {
  assert(actual.reason == reason);
  assert(actual.http_status_code == http_status_code);
}

RequestMeta write_request(const AdminCredential& credential,
                          uint64_t now_ms = 3000) {
  return {
      .method = HttpMethod::post,
      .path = "/api/v1/probe/echo",
      .source = source(1),
      .host = kHost,
      .origin = kOrigin,
      .cookie_token = credential.cookie_token,
      .csrf_token = credential.csrf_token,
      .header_bytes = 512,
      .content_length = 16,
      .now_ms = now_ms,
  };
}

AdminCredential pair(WebGuard& guard, std::string_view code,
                     uint64_t now_ms) {
  guard.open_pairing_window(code, now_ms);
  assert(guard.submit_pairing_code(code, "Safari", now_ms + 1000) ==
         PairingResult::awaiting_physical_confirmation);
  const auto credential = guard.confirm_pairing(true, now_ms + 2000);
  assert(credential.has_value());
  return *credential;
}

void test_pairing_and_write_auth() {
  DeterministicRandom random(0x42);
  WebGuard guard(kHost, random);
  guard.open_pairing_window("12345678", 0);

  assert(guard.submit_pairing_code("12345678", "Safari", 1000) ==
         PairingResult::awaiting_physical_confirmation);
  assert(!guard.has_admin_session());

  const auto credential = guard.confirm_pairing(true, 2000);
  assert(credential.has_value());
  assert(guard.has_admin_session());

  RequestMeta valid = write_request(*credential);
  expect_decision(guard.authorize(valid), WebReject::none, 200);

  RequestMeta wrong_host = valid;
  wrong_host.host = "cardputer-codex-attacker.local";
  expect_decision(guard.authorize(wrong_host), WebReject::host_mismatch, 403);

  RequestMeta wrong_origin = valid;
  wrong_origin.origin = "https://attacker.invalid";
  expect_decision(guard.authorize(wrong_origin), WebReject::origin_mismatch,
                  403);

  RequestMeta wrong_csrf = valid;
  const std::string wrong_csrf_token(64, '0');
  wrong_csrf.csrf_token = wrong_csrf_token;
  expect_decision(guard.authorize(wrong_csrf), WebReject::csrf_mismatch, 403);

  RequestMeta missing_cookie = valid;
  missing_cookie.cookie_token = "";
  expect_decision(guard.authorize(missing_cookie), WebReject::unauthenticated,
                  401);

  RequestMeta session_bootstrap = valid;
  session_bootstrap.method = HttpMethod::get;
  session_bootstrap.path = "/api/v1/probe/session";
  session_bootstrap.origin = "";
  session_bootstrap.csrf_token = "";
  session_bootstrap.now_ms = 4000;
  expect_decision(guard.authorize(session_bootstrap), WebReject::none, 200);
  const auto rotated_csrf =
      guard.issue_csrf_token(credential->cookie_token, 4001);
  assert(rotated_csrf.has_value());
  assert(rotated_csrf->size() == 64);

  RequestMeta stale_csrf = valid;
  stale_csrf.now_ms = 5000;
  expect_decision(guard.authorize(stale_csrf), WebReject::csrf_mismatch, 403);
  RequestMeta rotated = valid;
  rotated.csrf_token = *rotated_csrf;
  rotated.now_ms = 5001;
  expect_decision(guard.authorize(rotated), WebReject::none, 200);

  RequestMeta expired = valid;
  expired.csrf_token = *rotated_csrf;
  expired.now_ms = 1805002;
  expect_decision(guard.authorize(expired), WebReject::session_expired, 401);
}

void test_pairing_window_backoff_and_capacity() {
  DeterministicRandom random(0x21);
  WebGuard brute_force(kHost, random);
  brute_force.open_pairing_window("87654321", 0);
  for (uint8_t attempt = 0; attempt < 4; ++attempt) {
    assert(brute_force.submit_pairing_code(
               "00000000", "Browser", attempt * 1000) ==
           PairingResult::invalid_code);
  }
  assert(brute_force.submit_pairing_code("00000000", "Browser", 4000) ==
         PairingResult::backoff_started);
  assert(brute_force.submit_pairing_code("87654321", "Browser", 5000) ==
         PairingResult::in_backoff);
  assert(brute_force.backoff_remaining_ms(5000) == 599000);

  WebGuard expired(kHost, random);
  expired.open_pairing_window("12345678", 0);
  assert(expired.submit_pairing_code("12345678", "Browser", 300001) ==
         PairingResult::window_closed);

  WebGuard denied(kHost, random);
  denied.open_pairing_window("12345678", 0);
  assert(denied.submit_pairing_code("12345678", "Browser", 1000) ==
         PairingResult::awaiting_physical_confirmation);
  assert(!denied.confirm_pairing(false, 2000).has_value());
  assert(!denied.has_admin_session());

  WebGuard cancelled(kHost, random);
  cancelled.open_pairing_window("12345678", 0);
  assert(cancelled.submit_pairing_code("12345678", "Browser", 1000) ==
         PairingResult::awaiting_physical_confirmation);
  cancelled.cancel_pairing_confirmation();
  assert(!cancelled.confirm_pairing(true, 2000).has_value());
  assert(!cancelled.has_admin_session());

  WebGuard capacity(kHost, random);
  for (uint8_t index = 0; index < 5; ++index) {
    const std::string code = std::to_string(11111111 + index);
    assert(pair(capacity, code, index * 10000).cookie_token.size() == 64);
  }
  assert(capacity.admin_session_count() == 5);
  capacity.open_pairing_window("22222222", 60000);
  assert(capacity.submit_pairing_code("22222222", "Sixth", 61000) ==
         PairingResult::awaiting_physical_confirmation);
  assert(!capacity.confirm_pairing(true, 62000).has_value());
  assert(capacity.admin_session_count() == 5);
}

void test_request_budgets_and_json_depth() {
  assert(RequestBudget::for_request("/api/v1/probe/echo", 8192, 16384)
             .accepted());
  assert(RequestBudget::for_request("/api/v1/probe/echo", 8193, 1).reason ==
         WebReject::header_too_large);
  assert(RequestBudget::for_request("/api/v1/probe/echo", 100, 16385).reason ==
         WebReject::body_too_large);
  auto import =
      RequestBudget::for_request("/api/v1/config/import", 100, 131072);
  assert(import.accepted());
  assert(import.consume(65536));
  assert(import.consume(65536));
  assert(!import.consume(1));
  assert(import.reason == WebReject::body_too_large);
  assert(RequestBudget::for_request("/api/v1/config/import", 100, 131073)
             .reason == WebReject::body_too_large);

  JsonDepthTracker depth;
  assert(depth.consume(
      "{\"ignored\":\"{[}\",\"a\":{\"b\":{\"c\":{\"d\":"));
  assert(depth.consume("{\"e\":{\"f\":{\"g\":{\"h\":1}}}}}}}"));
  assert(depth.maximum_depth() == 8);

  JsonDepthTracker too_deep;
  assert(!too_deep.consume(
      "{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":{\"f\":{\"g\":{\"h\":{\"i\":1}}}}}}}}}"));
  assert(too_deep.maximum_depth() == 8);

  JsonDepthTracker malformed;
  assert(!malformed.consume("}"));
}

void test_pairing_response_window() {
  PairingResponseWindow window;
  assert(window.begin(1000));
  assert(window.pending());
  assert(!window.begin(1001));
  assert(window.deadline_ms() == 301000);
  assert(window.remaining_ms(1001) == 299999);
  assert(!window.expired(301000));
  assert(window.expired(301001));
  window.finish();
  assert(!window.pending());
  assert(window.remaining_ms(301001) == 0);

  assert(window.begin(UINT64_MAX - 100));
  assert(window.deadline_ms() == UINT64_MAX);
  window.finish();
}

void test_exact_web_route_manifest() {
  constexpr std::array<ProbeWebRoute, 6> expected{{
      {HttpMethod::get, "/healthz", false},
      {HttpMethod::post, "/api/v1/web-pairing/submit", false},
      {HttpMethod::get, "/api/v1/probe/session", false},
      {HttpMethod::post, "/api/v1/probe/echo", false},
      {HttpMethod::post, "/api/v1/config/import", false},
      {HttpMethod::get, "/api/v1/probe/ws", true},
  }};
  assert(kProbeWebRoutes.size() == 6);
  assert(kProbeWebRoutes == expected);
}

void test_rate_limits_and_route_exceptions() {
  DeterministicRandom random(0x81);
  WebGuard health(kHost, random);
  RequestMeta health_request{
      .method = HttpMethod::get,
      .path = "/healthz",
      .source = source(1),
      .host = kHost,
      .now_ms = 0,
  };
  for (uint8_t index = 0; index < 10; ++index) {
    assert(health.authorize(health_request).reason == WebReject::none);
  }
  expect_decision(health.authorize(health_request), WebReject::rate_limited,
                  429);

  WebGuard unauthenticated(kHost, random);
  RequestMeta pairing_request{
      .method = HttpMethod::post,
      .path = "/api/v1/web-pairing/submit",
      .source = source(1),
      .host = kHost,
      .origin = kOrigin,
      .header_bytes = 100,
      .content_length = 32,
      .now_ms = 0,
  };
  for (uint8_t index = 0; index < 4; ++index) {
    assert(unauthenticated.authorize(pairing_request).reason ==
           WebReject::none);
  }
  expect_decision(unauthenticated.authorize(pairing_request),
                  WebReject::rate_limited, 429);

  WebGuard forged_cookie(kHost, random);
  RequestMeta forged_request{
      .method = HttpMethod::get,
      .path = "/api/v1/probe/session",
      .source = source(2),
      .host = kHost,
      .cookie_token = "forged-cookie",
      .now_ms = 0,
  };
  for (uint8_t index = 0; index < 4; ++index) {
    expect_decision(forged_cookie.authorize(forged_request),
                    WebReject::unauthenticated, 401);
  }
  expect_decision(forged_cookie.authorize(forged_request),
                  WebReject::rate_limited, 429);

  WebGuard source_capacity(kHost, random);
  for (uint8_t index = 1; index <= 16; ++index) {
    pairing_request.source = source(index);
    pairing_request.now_ms = index * 2000;
    assert(source_capacity.authorize(pairing_request).reason ==
           WebReject::none);
  }
  pairing_request.source = source(17);
  expect_decision(source_capacity.authorize(pairing_request),
                  WebReject::rate_limited, 429);

  WebGuard authenticated(kHost, random);
  const auto credential = pair(authenticated, "12345678", 0);
  RequestMeta request = write_request(credential, 3000);
  for (uint8_t index = 0; index < 20; ++index) {
    assert(authenticated.authorize(request).reason == WebReject::none);
  }
  expect_decision(authenticated.authorize(request), WebReject::rate_limited,
                  429);

  RequestMeta pairing_wrong_origin = pairing_request;
  pairing_wrong_origin.source = source(1);
  pairing_wrong_origin.now_ms = 100000;
  pairing_wrong_origin.origin = "https://attacker.invalid";
  WebGuard origin_guard(kHost, random);
  expect_decision(origin_guard.authorize(pairing_wrong_origin),
                  WebReject::origin_mismatch, 403);
}

}  // namespace

int main() {
  test_pairing_and_write_auth();
  test_pairing_window_backoff_and_capacity();
  test_request_budgets_and_json_depth();
  test_pairing_response_window();
  test_exact_web_route_manifest();
  test_rate_limits_and_route_exceptions();
  return 0;
}
