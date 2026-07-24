import XCTest
@testable import Phase0Ledger

final class LedgerModuleSurfaceTests: XCTestCase {
    func testLedgerProtocolIsVisible() {
        XCTAssertNotNil(TextOperationLedger.self)
    }
}
