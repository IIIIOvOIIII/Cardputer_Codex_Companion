import Foundation
import CodexAppServer
import ProductContracts

enum TestFailure: Error {
    case assertion(String)
}

func expect(_ condition: @autoclosure () -> Bool, _ message: String) throws {
    if !condition() {
        throw TestFailure.assertion(message)
    }
}

let snapshot = CompanionSnapshot(
    sequence: 7,
    sessionID: "thread-1",
    title: "Cardputer",
    cwd: "/tmp/project",
    state: "active",
    approvals: 0,
    inputs: 0,
    model: "gpt-5.6",
    thinkingLevel: "high",
    fast: true,
    limits: [
        CodexLimitUsage(
            scope: .codex,
            window: .fiveHours,
            usedPercent: 38
        )
    ]
)
let encoded = try JSONEncoder().encode(snapshot)
let object = try JSONSerialization.jsonObject(with: encoded) as! [String: Any]
try expect(object["thinking_level"] as? String == "high", "thinking_level")
try expect(object["fast"] as? Bool == true, "fast")
let limit = (object["limits"] as! [[String: Any]])[0]
try expect(limit["scope"] as? String == "codex", "scope")
try expect(limit["window"] as? String == "5h", "window")
try expect(limit["used_percent"] as? Int == 38, "used_percent")

let repositoryRoot = URL(fileURLWithPath: #filePath)
    .deletingLastPathComponent()
    .deletingLastPathComponent()
    .deletingLastPathComponent()
    .deletingLastPathComponent()
let fixtureRoot = repositoryRoot
    .appending(path: "protocol/product-v1/fixtures")
let statusFixture = try JSONSerialization.jsonObject(
    with: Data(contentsOf: fixtureRoot.appending(path: "status.json"))
) as! [String: Any]
let fixtureSnapshotData = try JSONSerialization.data(
    withJSONObject: statusFixture["snapshot_with_limits"] as! [String: Any]
)
let fixtureSnapshot = try JSONDecoder().decode(
    CompanionSnapshot.self,
    from: fixtureSnapshotData
)
try expect(fixtureSnapshot.sessionID == "thread-1", "shared fixture session")
try expect(fixtureSnapshot.fast == true, "shared fixture fast")
let absentSnapshotData = try JSONSerialization.data(
    withJSONObject:
        statusFixture["snapshot_without_optional_telemetry"] as! [String: Any]
)
let absentSnapshot = try JSONDecoder().decode(
    CompanionSnapshot.self,
    from: absentSnapshotData
)
try expect(absentSnapshot.model == nil, "missing model omitted")
try expect(absentSnapshot.limits == nil, "missing limits omitted")

let changedFast = CompanionSnapshot(
    sequence: 8,
    sessionID: snapshot.sessionID,
    title: snapshot.title,
    cwd: snapshot.cwd,
    state: snapshot.state,
    approvals: snapshot.approvals,
    inputs: snapshot.inputs,
    model: snapshot.model,
    thinkingLevel: snapshot.thinkingLevel,
    fast: false,
    limits: snapshot.limits
)
try expect(!snapshot.hasSameContent(as: changedFast), "fast affects content")

let rows = (0..<6).map {
    #"{"scope":"codex","window":"5h","used_percent":\#($0)}"#
}.joined(separator: ",")
let oversized = Data(
    """
    {"type":"snapshot","sequence":1,"session_id":"thread","title":"t",
    "cwd":"/tmp","state":"active","approvals":0,"inputs":0,
    "pet_id":"","pet_digest":"","pet_state":"idle","limits":[\(rows)]}
    """.utf8
)
let decoded = try JSONDecoder().decode(CompanionSnapshot.self, from: oversized)
try expect(decoded.limits?.count == 4, "limit clamp")

print("product telemetry contract tests passed")

final class FakeRPCClient: CodexRPCClient {
    var rateLimitRequests = 0
    var failRateLimits = false
    var rateLimits: [String: Any]

    init(rateLimits: [String: Any]) {
        self.rateLimits = rateLimits
    }

    func start() throws {}

    func request(
        method: String,
        params: [String: Any]
    ) throws -> [String: Any] {
        switch method {
        case "config/read":
            return [
                "config": [
                    "model": "config-fallback",
                    "model_reasoning_effort": "medium",
                    "service_tier": "priority"
                ]
            ]
        case "account/rateLimits/read":
            rateLimitRequests += 1
            if failRateLimits {
                throw TestFailure.assertion("rate limit unavailable")
            }
            return ["rateLimitsByLimitId": rateLimits]
        default:
            throw TestFailure.assertion("unexpected RPC \(method)")
        }
    }

    func respondToPendingApproval(approved: Bool) throws {}
}

let validLimits: [String: Any] = [
    "codex": [
        "limitId": "codex",
        "limitName": "Codex",
        "primary": ["usedPercent": 38, "windowDurationMins": 300],
        "secondary": ["usedPercent": 61, "windowDurationMins": 10080]
    ],
    "spark": [
        "limitId": "gpt-5.3-codex-spark",
        "limitName": "GPT-5.3-Codex-Spark",
        "primary": ["usedPercent": 17, "windowDurationMins": 300],
        "secondary": ["usedPercent": 22, "windowDurationMins": 10080]
    ]
]
let fake = FakeRPCClient(rateLimits: validLimits)
let reader = CodexTelemetryReader(rpc: fake)
let epoch = Date(timeIntervalSince1970: 1_000)
let contextURL = FileManager.default.temporaryDirectory
    .appending(path: "cardputer-telemetry-\(UUID().uuidString).jsonl")
try Data(
    """
    {"type":"turn_context","payload":{"model":"old","effort":"low"}}
    {"type":"event_msg","payload":{"type":"other"}}
    {"type":"turn_context","payload":{"model":"gpt-5.6","effort":"high"}}
    {"type":"event_msg","payload":{"type":"large","data":"\(String(repeating: "x", count: 600_000))"}}

    """.utf8
).write(to: contextURL)
defer { try? FileManager.default.removeItem(at: contextURL) }
let thread: [String: Any] = ["path": contextURL.path]
let firstTelemetry = try reader.read(thread: thread, now: epoch)
try expect(firstTelemetry.model == "gpt-5.6", "model")
try expect(firstTelemetry.thinkingLevel == "high", "thinking")
try expect(firstTelemetry.fast, "fast")
try expect(
    firstTelemetry.limits == [
        CodexLimitUsage(scope: .codex, window: .fiveHours, usedPercent: 38),
        CodexLimitUsage(scope: .codex, window: .weekly, usedPercent: 61),
        CodexLimitUsage(scope: .spark, window: .fiveHours, usedPercent: 17),
        CodexLimitUsage(scope: .spark, window: .weekly, usedPercent: 22)
    ],
    "ordered limits"
)
_ = try reader.read(
    thread: thread,
    now: epoch.addingTimeInterval(59)
)
try expect(fake.rateLimitRequests == 1, "no refresh before 60 seconds")
_ = try reader.read(
    thread: thread,
    now: epoch.addingTimeInterval(60)
)
try expect(fake.rateLimitRequests == 2, "refresh at 60 seconds")

fake.rateLimits = [
    "wrong-window": [
        "limitId": "codex",
        "limitName": "Codex",
        "primary": ["usedPercent": 1, "windowDurationMins": 301]
    ],
    "unnamed-spark": [
        "limitId": "spark",
        "limitName": "Fast model",
        "primary": ["usedPercent": 2, "windowDurationMins": 300]
    ],
    "conflict-a": [
        "limitId": "codex",
        "limitName": "Codex",
        "primary": ["usedPercent": 3, "windowDurationMins": 300]
    ],
    "conflict-b": [
        "limitId": "codex-secondary",
        "limitName": "Codex",
        "primary": ["usedPercent": 4, "windowDurationMins": 300]
    ],
    "missing": [
        "limitId": "codex",
        "limitName": "Codex",
        "primary": ["windowDurationMins": 10080]
    ]
]
let invalidReader = CodexTelemetryReader(rpc: fake)
let invalidTelemetry = try invalidReader.read(thread: thread, now: epoch)
try expect(invalidTelemetry.limits.isEmpty, "ambiguous limits omitted")

let staleFake = FakeRPCClient(rateLimits: validLimits)
let staleReader = CodexTelemetryReader(rpc: staleFake)
_ = try staleReader.read(thread: thread, now: epoch)
staleFake.failRateLimits = true
let freshCache = try staleReader.read(
    thread: thread,
    now: epoch.addingTimeInterval(60)
)
try expect(freshCache.limits.count == 4, "fresh cache retained")
let staleCache = try staleReader.read(
    thread: thread,
    now: epoch.addingTimeInterval(121)
)
try expect(staleCache.limits.isEmpty, "stale cache hidden")

print("Codex telemetry reader tests passed")

let actionFixture = try JSONSerialization.jsonObject(
    with: Data(contentsOf: fixtureRoot.appending(path: "actions.json"))
) as! [String: Any]
let actionWithMigration = try JSONDecoder().decode(
    RemoteActionEnvelope.self,
    from: try JSONSerialization.data(
        withJSONObject: actionFixture["migration_response"] as! [String: Any]
    )
)
try expect(
    actionWithMigration.pairingMigration == PairingMigration(
        nextPairing: "87654321",
        pinRevision: 8
    ),
    "action migration"
)
let actionWithoutMigration = try JSONDecoder().decode(
    RemoteActionEnvelope.self,
    from: Data(
        #"{"sequence":9,"action":"none","needs_snapshot":false}"#.utf8
    )
)
try expect(actionWithoutMigration.pairingMigration == nil, "optional migration")

let configDirectory = FileManager.default.temporaryDirectory
    .appending(path: "cardputer-config-\(UUID().uuidString)")
try FileManager.default.createDirectory(
    at: configDirectory,
    withIntermediateDirectories: false
)
defer { try? FileManager.default.removeItem(at: configDirectory) }
let configURL = configDirectory.appending(path: "config.json")
try Data(
    """
    {"device":"https://192.168.1.2","pairing":"12345678",
     "pin_revision":7,"unrelated":{"kept":true}}
    """.utf8
).write(to: configURL)
try PairingConfigWriter.persist(
    PairingMigration(nextPairing: "87654321", pinRevision: 8),
    to: configURL
)
var configObject = try JSONSerialization.jsonObject(
    with: Data(contentsOf: configURL)
) as! [String: Any]
try expect(configObject["pairing"] as? String == "87654321", "pairing update")
try expect(configObject["pin_revision"] as? Int == 8, "revision update")
try expect(
    (configObject["unrelated"] as? [String: Bool])?["kept"] == true,
    "unrelated config retained"
)
let permissions = try FileManager.default.attributesOfItem(
    atPath: configURL.path
)[.posixPermissions] as! NSNumber
try expect(permissions.intValue == 0o600, "config mode")
try PairingConfigWriter.persist(
    PairingMigration(nextPairing: "11111111", pinRevision: 8),
    to: configURL
)
configObject = try JSONSerialization.jsonObject(
    with: Data(contentsOf: configURL)
) as! [String: Any]
try expect(configObject["pairing"] as? String == "87654321", "old revision ignored")
do {
    try PairingConfigWriter.persist(
        PairingMigration(nextPairing: "１２３４５６７８", pinRevision: 9),
        to: configURL
    )
    throw TestFailure.assertion("unicode pairing accepted")
} catch PairingConfigWriterError.invalidPairing {
}
let siblings = try FileManager.default.contentsOfDirectory(
    atPath: configDirectory.path
)
try expect(siblings == ["config.json"], "temporary file cleaned")

print("pairing migration tests passed")
