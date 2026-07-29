import XCTest
@testable import ProductGATT

final class ProductGATTRecoveryPolicyTests: XCTestCase {
    func testFailureBackoffIsBoundedAndReadyResetsIt() {
        var policy = ProductGATTRecoveryPolicy()
        XCTAssertEqual(policy.apply(.start).retryAfterMilliseconds, 0)
        XCTAssertEqual(
            policy.apply(.candidateSelected).watchdogMilliseconds,
            8_000
        )
        XCTAssertEqual(policy.apply(.failed).retryAfterMilliseconds, 500)
        XCTAssertEqual(policy.apply(.failed).retryAfterMilliseconds, 1_000)
        XCTAssertEqual(policy.apply(.failed).retryAfterMilliseconds, 2_000)
        XCTAssertEqual(policy.apply(.failed).retryAfterMilliseconds, 5_000)
        XCTAssertEqual(policy.apply(.failed).retryAfterMilliseconds, 5_000)
        XCTAssertEqual(policy.apply(.ready).phase, .ready)
        XCTAssertEqual(policy.apply(.failed).retryAfterMilliseconds, 500)
    }

    func testTimeoutUsesFailurePathAndAdvancesGeneration() {
        var policy = ProductGATTRecoveryPolicy()
        _ = policy.apply(.start)
        let connecting = policy.apply(.candidateSelected)
        let timedOut = policy.apply(.timedOut)
        XCTAssertTrue(timedOut.cancelPeripheral)
        XCTAssertEqual(timedOut.retryAfterMilliseconds, 500)
        XCTAssertGreaterThan(timedOut.generation, connecting.generation)
    }

    func testIntentionalStopNeverRetries() {
        var policy = ProductGATTRecoveryPolicy()
        _ = policy.apply(.start)
        let stopped = policy.apply(.stop)
        XCTAssertEqual(stopped.phase, .stopped)
        XCTAssertNil(stopped.retryAfterMilliseconds)
        XCTAssertNil(policy.apply(.failed).retryAfterMilliseconds)
        XCTAssertNil(policy.apply(.timedOut).retryAfterMilliseconds)
    }

    func testBluetoothUnavailableWaitsForPoweredOn() {
        var policy = ProductGATTRecoveryPolicy()
        _ = policy.apply(.start)
        let unavailable = policy.apply(.bluetoothUnavailable)
        XCTAssertEqual(unavailable.phase, .idle)
        XCTAssertNil(unavailable.retryAfterMilliseconds)
        XCTAssertEqual(
            policy.apply(.bluetoothPoweredOn).retryAfterMilliseconds,
            0
        )
    }

    func testConnectionPhasesArmEightSecondWatchdog() {
        var policy = ProductGATTRecoveryPolicy()
        _ = policy.apply(.start)
        XCTAssertEqual(policy.apply(.candidateSelected).phase, .connecting)
        XCTAssertEqual(policy.apply(.connected).phase, .discovering)
        XCTAssertEqual(policy.apply(.subscribing).phase, .subscribing)
        XCTAssertEqual(
            policy.apply(.subscribing).watchdogMilliseconds,
            8_000
        )
    }
}
