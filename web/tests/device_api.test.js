const assert = require("node:assert/strict");
const test = require("node:test");

const {
  DeviceApiError,
  g0ChordPayload,
  loginErrorMessage,
  requestDevice,
} = require("../src/device_api.js");

function response(status, body) {
  return {
    ok: status >= 200 && status < 300,
    status,
    text: async () => body,
  };
}

test("maps rejected fetch to device unreachable", async () => {
  const fetchFailure = async () => {
    throw new TypeError("Failed to fetch");
  };
  await assert.rejects(
    requestDevice(
      fetchFailure,
      "/api/v1/status",
      {},
      "12345678",
    ),
    (error) =>
      error.status === 0 &&
      loginErrorMessage(error) ===
        "设备不可达，请检查 IP、Wi-Fi 和证书访问",
  );
});

test("maps HTTP 401 and 403 to PIN error", () => {
  assert.equal(
    loginErrorMessage(new DeviceApiError(401, "pairing_required", "")),
    "PIN 错误",
  );
  assert.equal(
    loginErrorMessage(new DeviceApiError(403, "pairing_required", "")),
    "PIN 错误",
  );
});

test("maps authenticated partition 503 without blaming the PIN", () => {
  const error = new DeviceApiError(
    503,
    "partition_incompatible",
    "{\"reason\":\"missing\"}",
  );
  assert.equal(
    loginErrorMessage(error),
    "设备分区不兼容，请重新安装官方 Factory 固件或 Launcher 2.8+ 兼容固件",
  );
});

test("maps unrelated server failure to service unavailable", () => {
  assert.equal(
    loginErrorMessage(new DeviceApiError(500, "internal_error", "")),
    "设备服务暂不可用，请稍后重试",
  );
});

test("preserves status, error code, body, and pairing header", async () => {
  let captured;
  const fetchSuccess = async (path, options) => {
    captured = { path, options };
    return response(200, "{\"version\":\"1.3.0\"}");
  };
  const payload = await requestDevice(
    fetchSuccess,
    "/api/v1/status",
    { headers: { Accept: "application/json" } },
    "12345678",
  );
  assert.deepEqual(payload, { version: "1.3.0" });
  assert.equal(captured.path, "/api/v1/status");
  assert.equal(
    captured.options.headers["X-Cardputer-Pairing"],
    "12345678",
  );
  assert.equal(captured.options.headers.Accept, "application/json");

  await assert.rejects(
    requestDevice(
      async () =>
        response(
          503,
          "{\"error\":\"partition_incompatible\","
            + "\"reason\":\"too_small\"}",
        ),
      "/api/v1/profiles",
      {},
      "12345678",
    ),
    (error) =>
      error.status === 503 &&
      error.code === "partition_incompatible" &&
      error.responseText.includes("too_small"),
  );
});

test("builds a bounded enabled G0 chord payload", () => {
  assert.deepEqual(
    g0ChordPayload(true, {
      modifiers: 4,
      usages: [25, 26],
    }),
    {
      enabled: true,
      modifiers: 4,
      usages: [25],
    },
  );
});

test("disabling G0 chord preserves its configured chord", () => {
  assert.deepEqual(
    g0ChordPayload(false, {
      modifiers: 9,
      usages: [40],
    }),
    {
      enabled: false,
      modifiers: 9,
      usages: [40],
    },
  );
});
