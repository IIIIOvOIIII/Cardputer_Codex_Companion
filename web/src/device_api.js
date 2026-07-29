class DeviceApiError extends Error {
  constructor(status, code, responseText) {
    super(code || `HTTP ${status}`);
    this.name = "DeviceApiError";
    this.status = status;
    this.code = code || "";
    this.responseText = responseText || "";
  }
}

async function requestDevice(fetchImpl, path, options, pairing) {
  const request = { ...(options || {}) };
  request.headers = {
    ...(request.headers || {}),
    "X-Cardputer-Pairing": pairing,
    "Content-Type": "application/json",
  };
  let response;
  try {
    response = await fetchImpl(path, request);
  } catch (error) {
    throw new DeviceApiError(
      0,
      "network_unreachable",
      String(error),
    );
  }
  const text = await response.text();
  let payload = {};
  try {
    payload = text ? JSON.parse(text) : {};
  } catch (_) {
    payload = {};
  }
  if (!response.ok) {
    throw new DeviceApiError(
      response.status,
      payload.error || "",
      text,
    );
  }
  return payload;
}

function loginErrorMessage(error) {
  if (error && error.status === 0) {
    return "设备不可达，请检查 IP、Wi-Fi 和证书访问";
  }
  if (error && (error.status === 401 || error.status === 403)) {
    return "PIN 错误";
  }
  if (
    error &&
    error.status === 503 &&
    error.code === "partition_incompatible"
  ) {
    return "设备分区不兼容，请重新安装官方 Factory 固件或 Launcher 2.8+ 兼容固件";
  }
  return "设备服务暂不可用，请稍后重试";
}

function g0ChordPayload(enabled, draft) {
  return {
    enabled: Boolean(enabled),
    modifiers: draft.modifiers,
    usages: draft.usages.slice(0, 1),
  };
}

if (typeof module !== "undefined" && module.exports) {
  module.exports = {
    DeviceApiError,
    g0ChordPayload,
    loginErrorMessage,
    requestDevice,
  };
}
