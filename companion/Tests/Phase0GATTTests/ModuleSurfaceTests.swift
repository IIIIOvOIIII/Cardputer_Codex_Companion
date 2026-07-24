import XCTest
@testable import Phase0GATT

final class GATTModuleSurfaceTests: XCTestCase {
    func testProbeLimitsAreStable() {
        XCTAssertEqual(Phase0GATTConstants.protocolVersion, 1)
        XCTAssertEqual(Phase0GATTConstants.maximumMessageBytes, 1024)
    }
}
