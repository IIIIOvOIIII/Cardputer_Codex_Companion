#pragma once

#include <array>
#include <string_view>

#include "probe/web_guard.hpp"

struct ProbeWebRoute {
  HttpMethod method;
  std::string_view path;
  bool websocket;
  bool operator==(const ProbeWebRoute&) const = default;
};

inline constexpr std::array<ProbeWebRoute, 6> kProbeWebRoutes{{
    {HttpMethod::get, "/healthz", false},
    {HttpMethod::post, "/api/v1/web-pairing/submit", false},
    {HttpMethod::get, "/api/v1/probe/session", false},
    {HttpMethod::post, "/api/v1/probe/echo", false},
    {HttpMethod::post, "/api/v1/config/import", false},
    {HttpMethod::get, "/api/v1/probe/ws", true},
}};
