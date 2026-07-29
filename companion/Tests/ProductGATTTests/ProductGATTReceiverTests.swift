import XCTest
@testable import ProductGATT

final class ProductGATTReceiverTests: XCTestCase {
    func testConnectedPeripheralRecoveryIncludesHIDService() {
        XCTAssertEqual(
            ProductGATTContract.connectedPeripheralServiceUUIDs,
            [
                ProductGATTContract.serviceUUID,
                ProductGATTContract.hidServiceUUID,
            ]
        )
    }

    func testDeviceNameFilterAcceptsCurrentAndLegacyNames() {
        XCTAssertTrue(
            ProductGATTDeviceIdentity.accepts(
                peripheralName: "Cardputer Codex",
                advertisedName: nil
            )
        )
        XCTAssertTrue(
            ProductGATTDeviceIdentity.accepts(
                peripheralName: nil,
                advertisedName: "Cardputer Codex"
            )
        )
        XCTAssertTrue(
            ProductGATTDeviceIdentity.accepts(
                peripheralName: "Cardputer Companion",
                advertisedName: nil
            )
        )
        XCTAssertFalse(
            ProductGATTDeviceIdentity.accepts(
                peripheralName: "nimble",
                advertisedName: nil
            )
        )
    }
}
