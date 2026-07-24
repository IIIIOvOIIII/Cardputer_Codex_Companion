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
}
