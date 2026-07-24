import XCTest
@testable import Phase0Unicode

final class UnicodeModuleSurfaceTests: XCTestCase {
    func testProbeLimitIsStable() {
        XCTAssertEqual(Phase0UnicodeConstants.maximumUTF8Bytes, 1024)
    }
}
