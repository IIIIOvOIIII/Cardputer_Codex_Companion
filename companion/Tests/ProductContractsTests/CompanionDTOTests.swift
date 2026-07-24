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
    }
}
