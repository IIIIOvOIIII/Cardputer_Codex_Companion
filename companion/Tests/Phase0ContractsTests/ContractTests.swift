import XCTest
@testable import Phase0Contracts

final class ContractTests: XCTestCase {
    func testTerminalAndReplaySemanticsAreStable() throws {
        XCTAssertTrue(OperationStatus.completed.isTerminal)
        XCTAssertTrue(OperationStatus.failed.isTerminal)
        XCTAssertTrue(OperationStatus.partial.isTerminal)
        XCTAssertTrue(OperationStatus.indeterminate.isTerminal)
        XCTAssertFalse(OperationStatus.intent.isTerminal)
        XCTAssertFalse(OperationStatus.accepted.isTerminal)
        XCTAssertEqual(StableErrorCode.secureInputActive.rawValue, "secure_input_active")
        XCTAssertEqual(StableErrorCode.permissionDenied.rawValue, "permission_denied")
        XCTAssertEqual(StableErrorCode.invalidRequest.rawValue, "invalid_request")
    }

    func testTextRecordRejectsImpossiblePrefixLengths() throws {
        XCTAssertThrowsError(
            try TextOperationRecord(
                pairedDeviceID: "device-a",
                operationID: UUID(),
                payloadSHA256: Data(repeating: 1, count: 32),
                targetPID: 42,
                targetElementFingerprint: "ax:field",
                totalUTF8Length: 12,
                postedPrefixLength: 8,
                verifiedPrefixLength: 9,
                status: .intent,
                errorCode: nil,
                createdAt: Date(timeIntervalSince1970: 1),
                updatedAt: Date(timeIntervalSince1970: 1),
                expiresAt: Date(timeIntervalSince1970: 601)
            )
        )
    }

    func testEvidenceCannotPassWithBlockedChecks() throws {
        let evidence = HILEvidence(
            schemaVersion: "1.0",
            runID: UUID(),
            startedAt: Date(timeIntervalSince1970: 1),
            completedAt: Date(timeIntervalSince1970: 2),
            checks: [
                GateCheck(id: "signed_app", status: .pass, detail: "valid"),
                GateCheck(id: "cardputer", status: .blocked, detail: "not connected")
            ]
        )
        XCTAssertEqual(evidence.overallStatus, .blocked)
    }
}
