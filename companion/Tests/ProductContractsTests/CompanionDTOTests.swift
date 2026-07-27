import XCTest
@testable import ProductContracts

final class CompanionDTOTests: XCTestCase {
    func testSnapshotUsesStableSnakeCaseContract() throws {
        let value = CompanionSnapshot(
            sequence: 7,
            sessionID: "thread-1",
            title: "Cardputer",
            cwd: "/tmp/project",
            state: "active",
            approvals: 1,
            inputs: 2
        )
        let data = try JSONEncoder().encode(value)
        let json = try XCTUnwrap(
            JSONSerialization.jsonObject(with: data) as? [String: Any]
        )
        XCTAssertEqual(json["type"] as? String, "snapshot")
        XCTAssertEqual(json["session_id"] as? String, "thread-1")
    }

    func testSnapshotContentComparisonIgnoresWireSequence() {
        let first = CompanionSnapshot(
            sequence: 1,
            sessionID: "thread-1",
            title: "Cardputer",
            cwd: "/tmp/project",
            state: "active",
            approvals: 1,
            inputs: 2
        )
        let sameContent = CompanionSnapshot(
            sequence: 99,
            sessionID: "thread-1",
            title: "Cardputer",
            cwd: "/tmp/project",
            state: "active",
            approvals: 1,
            inputs: 2
        )
        let changed = CompanionSnapshot(
            sequence: 100,
            sessionID: "thread-1",
            title: "Cardputer",
            cwd: "/tmp/project",
            state: "waiting",
            approvals: 1,
            inputs: 2
        )
        XCTAssertTrue(first.hasSameContent(as: sameContent))
        XCTAssertFalse(first.hasSameContent(as: changed))
    }

    func testRemoteActionDecodesSnapshotResyncSignal() throws {
        let data = Data(
            #"{"sequence":3,"action":"none","needs_snapshot":true}"#.utf8
        )
        let value = try JSONDecoder().decode(
            RemoteActionEnvelope.self,
            from: data
        )
        XCTAssertTrue(value.needsSnapshot)
        XCTAssertNil(value.pairingMigration)
    }

    func testRemoteActionDecodesPairingMigration() throws {
        let data = Data(
            """
            {"sequence":8,"action":"none","needs_snapshot":false,
             "next_pairing":"87654321","pin_revision":8}
            """.utf8
        )
        let value = try JSONDecoder().decode(
            RemoteActionEnvelope.self,
            from: data
        )
        XCTAssertEqual(
            value.pairingMigration,
            PairingMigration(nextPairing: "87654321", pinRevision: 8)
        )
    }

    func testSnapshotEncodesCodexTelemetryWithStableKeys() throws {
        let value = CompanionSnapshot(
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
        let data = try JSONEncoder().encode(value)
        let json = try XCTUnwrap(
            JSONSerialization.jsonObject(with: data) as? [String: Any]
        )
        XCTAssertEqual(json["thinking_level"] as? String, "high")
        XCTAssertEqual(json["fast"] as? Bool, true)
        let limit = try XCTUnwrap((json["limits"] as? [[String: Any]])?.first)
        XCTAssertEqual(limit["scope"] as? String, "codex")
        XCTAssertEqual(limit["window"] as? String, "5h")
        XCTAssertEqual(limit["used_percent"] as? Int, 38)
    }

    func testSnapshotContentComparisonIncludesTelemetry() {
        let base = CompanionSnapshot(
            sequence: 1,
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
        let changedFast = CompanionSnapshot(
            sequence: 2,
            sessionID: "thread-1",
            title: "Cardputer",
            cwd: "/tmp/project",
            state: "active",
            approvals: 0,
            inputs: 0,
            model: "gpt-5.6",
            thinkingLevel: "high",
            fast: false,
            limits: base.limits
        )
        let changedLimit = CompanionSnapshot(
            sequence: 3,
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
                    usedPercent: 39
                )
            ]
        )
        XCTAssertFalse(base.hasSameContent(as: changedFast))
        XCTAssertFalse(base.hasSameContent(as: changedLimit))
    }

    func testSnapshotDecodingClampsLimitsToFourRows() throws {
        let rows = (0..<6).map {
            #"{"scope":"codex","window":"5h","used_percent":\#($0)}"#
        }.joined(separator: ",")
        let data = Data(
            """
            {"type":"snapshot","sequence":1,"session_id":"thread","title":"t",
            "cwd":"/tmp","state":"active","approvals":0,"inputs":0,
            "pet_id":"","pet_digest":"","pet_state":"idle","limits":[\(rows)]}
            """.utf8
        )
        let value = try JSONDecoder().decode(CompanionSnapshot.self, from: data)
        XCTAssertEqual(value.limits?.count, 4)
    }

    func testSnapshotOmitsUnavailableTelemetryInsteadOfEncodingNull() throws {
        let value = CompanionSnapshot(
            sequence: 1,
            sessionID: "",
            title: "NO ACTIVE CODEX",
            cwd: "-",
            state: "offline",
            approvals: 0,
            inputs: 0
        )
        let data = try JSONEncoder().encode(value)
        let json = try XCTUnwrap(
            JSONSerialization.jsonObject(with: data) as? [String: Any]
        )
        for key in ["model", "thinking_level", "fast", "limits"] {
            XCTAssertNil(json[key])
        }
    }
}
