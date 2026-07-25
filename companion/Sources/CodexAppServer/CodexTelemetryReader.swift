import Foundation
import ProductContracts

public final class CodexTelemetryReader {
    private let rpc: CodexRPCClient
    private var lastRateAttempt: Date?
    private var lastRateSuccess: Date?
    private var cachedLimits: [CodexLimitUsage] = []

    public init(rpc: CodexRPCClient) {
        self.rpc = rpc
    }

    public func read(
        thread: [String: Any],
        now: Date
    ) throws -> CodexTelemetry {
        let configResult = try rpc.request(method: "config/read", params: [:])
        let config = configResult["config"] as? [String: Any] ?? configResult
        let context = readLatestTurnContext(path: thread["path"] as? String)
        let model = context.model
            ?? config["model"] as? String
            ?? ""
        let thinking = context.effort
            ?? config["model_reasoning_effort"] as? String
            ?? ""
        let tier = (config["service_tier"] as? String)?.lowercased() ?? ""

        if shouldRefresh(now: now) {
            lastRateAttempt = now
            do {
                let result = try rpc.request(
                    method: "account/rateLimits/read",
                    params: [:]
                )
                cachedLimits = classify(result)
                lastRateSuccess = now
            } catch {
                // Session telemetry remains useful when the optional account
                // quota endpoint is temporarily unavailable.
            }
        }

        let limits: [CodexLimitUsage]
        if let success = lastRateSuccess,
           now.timeIntervalSince(success) <= 120 {
            limits = cachedLimits
        } else {
            limits = []
        }
        return CodexTelemetry(
            model: model,
            thinkingLevel: thinking,
            fast: tier == "priority" || tier == "fast",
            limits: limits
        )
    }

    private func shouldRefresh(now: Date) -> Bool {
        guard let lastRateAttempt else { return true }
        return now.timeIntervalSince(lastRateAttempt) >= 60
    }

    private func classify(_ result: [String: Any]) -> [CodexLimitUsage] {
        let raw = result["rateLimitsByLimitId"] as? [String: Any]
            ?? result["rateLimitsByLimitID"] as? [String: Any]
            ?? [:]
        var candidates: [Slot: [UInt8]] = [:]
        for value in raw.values {
            guard let bucket = value as? [String: Any],
                  let scope = classifyScope(bucket) else {
                continue
            }
            for key in ["primary", "secondary"] {
                guard let window = bucket[key] as? [String: Any],
                      let minutes = integer(window["windowDurationMins"]),
                      let limitWindow = classifyWindow(minutes),
                      let percent = integer(window["usedPercent"]),
                      (0...100).contains(percent) else {
                    continue
                }
                candidates[Slot(scope: scope, window: limitWindow), default: []]
                    .append(UInt8(percent))
            }
        }
        let order: [Slot] = [
            Slot(scope: .codex, window: .fiveHours),
            Slot(scope: .codex, window: .weekly),
            Slot(scope: .spark, window: .fiveHours),
            Slot(scope: .spark, window: .weekly)
        ]
        return order.compactMap { slot in
            guard let values = candidates[slot], values.count == 1,
                  let value = values.first else {
                return nil
            }
            return CodexLimitUsage(
                scope: slot.scope,
                window: slot.window,
                usedPercent: value
            )
        }
    }

    private func classifyScope(
        _ bucket: [String: Any]
    ) -> CodexLimitScope? {
        let identity = [
            bucket["limitId"] as? String,
            bucket["limitName"] as? String
        ]
        .compactMap { $0 }
        .map(normalizeIdentity)
        .joined(separator: " ")
        if identity.contains("gpt53codexspark") {
            return .spark
        }
        if identity.contains("codex") && !identity.contains("spark") {
            return .codex
        }
        return nil
    }

    private func classifyWindow(_ minutes: Int) -> CodexLimitWindow? {
        switch minutes {
        case 300:
            return .fiveHours
        case 10080:
            return .weekly
        default:
            return nil
        }
    }

    private func normalizeIdentity(_ value: String) -> String {
        String(
            value.lowercased().unicodeScalars.filter {
                CharacterSet.alphanumerics.contains($0)
            }
        )
    }

    private func integer(_ value: Any?) -> Int? {
        if let value = value as? Int {
            return value
        }
        if let value = value as? NSNumber {
            return value.intValue
        }
        return nil
    }

    private func readLatestTurnContext(
        path: String?
    ) -> (model: String?, effort: String?) {
        guard let path, !path.isEmpty,
              let handle = FileHandle(forReadingAtPath: path) else {
            return (nil, nil)
        }
        defer { try? handle.close() }
        do {
            let end = try handle.seekToEnd()
            let maximum: UInt64 = 8 * 1024 * 1024
            let start = end > maximum ? end - maximum : 0
            try handle.seek(toOffset: start)
            let data = try handle.readToEnd() ?? Data()
            guard var text = String(data: data, encoding: .utf8) else {
                return (nil, nil)
            }
            if start > 0, let newline = text.firstIndex(of: "\n") {
                text.removeSubrange(...newline)
            }
            for line in text.split(separator: "\n").reversed() {
                guard let data = line.data(using: .utf8),
                      let object = try JSONSerialization.jsonObject(with: data)
                        as? [String: Any],
                      object["type"] as? String == "turn_context",
                      let payload = object["payload"] as? [String: Any] else {
                    continue
                }
                return (
                    payload["model"] as? String,
                    payload["effort"] as? String
                )
            }
        } catch {
            return (nil, nil)
        }
        return (nil, nil)
    }

    private struct Slot: Hashable {
        let scope: CodexLimitScope
        let window: CodexLimitWindow
    }
}
