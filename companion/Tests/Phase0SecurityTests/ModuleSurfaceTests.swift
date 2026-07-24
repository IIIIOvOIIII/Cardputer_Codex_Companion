import XCTest
@testable import Phase0Security

final class SecurityModuleSurfaceTests: XCTestCase {
    func testProtocolVersionIsStable() {
        XCTAssertEqual(Phase0SecurityVersion.protocolVersion, "1.0")
    }
}
