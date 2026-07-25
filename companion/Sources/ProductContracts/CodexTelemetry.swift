import Foundation

public enum CodexLimitScope: String, Codable, Sendable {
    case codex
    case spark
}

public enum CodexLimitWindow: String, Codable, Sendable {
    case fiveHours = "5h"
    case weekly
}

public struct CodexLimitUsage: Codable, Equatable, Sendable {
    public let scope: CodexLimitScope
    public let window: CodexLimitWindow
    public let usedPercent: UInt8

    enum CodingKeys: String, CodingKey {
        case scope, window
        case usedPercent = "used_percent"
    }

    public init(
        scope: CodexLimitScope,
        window: CodexLimitWindow,
        usedPercent: UInt8
    ) {
        self.scope = scope
        self.window = window
        self.usedPercent = usedPercent
    }
}

public struct CodexTelemetry: Equatable, Sendable {
    public let model: String
    public let thinkingLevel: String
    public let fast: Bool
    public let limits: [CodexLimitUsage]

    public init(
        model: String,
        thinkingLevel: String,
        fast: Bool,
        limits: [CodexLimitUsage]
    ) {
        self.model = model
        self.thinkingLevel = thinkingLevel
        self.fast = fast
        self.limits = Array(limits.prefix(4))
    }
}
