import Dispatch
import Foundation
import ProductAudio
import ProductGATT

func testUnifiedDiscoveryAndBindingOrder() {
    assert(ProductGATTContract.centralManagerCount == 1)
    assert(ProductGATTContract.maximumPeripheralCount == 1)
    assert(ProductGATTContract.callbackQueueQoS == .userInteractive)
    assert(Set(ProductGATTContract.characteristics) == Set([
        .unicodeNotify,
        .unicodeControl,
        .audioData,
        .audioControl,
        .audioStatus
    ]))
    assert(
        ProductGATTContract.requiredCharacteristics(audioEnabled: false) ==
            [.unicodeNotify, .unicodeControl]
    )
    assert(
        ProductGATTContract.requiredCharacteristics(audioEnabled: true) ==
            ProductGATTContract.characteristics
    )

    var session = ProductGATTSessionState(audioEnabled: true)
    let discovered = session.didDiscoverAllCharacteristics()
    assert(discovered == [.subscribeUnicode, .writeBind])
    assert(session.characteristicsDiscovered)
    assert(!discovered.contains(.writeSinkReady))

    let bound = session.didWriteBind(succeeded: true)
    assert(bound == [.subscribeAudioData, .subscribeAudioStatus])
    assert(!bound.contains(.writeSinkReady))
    assert(session.didSetAudioNotification(.audioData, enabled: true).isEmpty)
    assert(
        session.didSetAudioNotification(.audioStatus, enabled: true) ==
            [.writeAudioHello]
    )
    assert(session.audioNotificationsEnabled)
    assert(session.didWriteAudioHello(succeeded: true) == [.writeSinkReady])
    assert(session.protocolNegotiated)
    session.didWriteSinkReady(succeeded: true)
    assert(session.audioReady)
    assert(session.beginAudioSuspension() == [.writeSinkNotReady])
    assert(!session.audioReady)
    assert(session.resumeAudio() == [.writeSinkReady])
    session.didWriteSinkReady(succeeded: true)
    assert(session.audioReady)
}

func testShutdownAndDisconnectClearAudioWithoutDisablingUnicode() {
    var session = ProductGATTSessionState(audioEnabled: true)
    _ = session.didDiscoverAllCharacteristics()
    _ = session.didWriteBind(succeeded: true)
    _ = session.didSetAudioNotification(.audioData, enabled: true)
    _ = session.didSetAudioNotification(.audioStatus, enabled: true)
    _ = session.didWriteAudioHello(succeeded: true)
    session.didWriteSinkReady(succeeded: true)
    assert(session.audioReady)
    assert(session.unicodeEnabled)
    assert(session.beginIntentionalShutdown() == [.writeSinkNotReady])

    session.didDisconnect()
    assert(!session.audioReady)
    assert(!session.unicodeEnabled)
    assert(!session.characteristicsDiscovered)
    assert(!session.audioNotificationsEnabled)
    assert(!session.protocolNegotiated)
    assert(session.reconnectCount == 1)
}

private final class TestSink: AudioSampleSink {
    func write(samples: UnsafeBufferPointer<Float>) -> Int {
        samples.count
    }

    func reset() {}
}

func testAudioParseFailureDoesNotDisableUnicodeContract() {
    let routes = ProductGATTValueRouter.route(
        characteristic: .audioData,
        value: Data([0xFF])
    )
    assert(routes == .invalidAudio)
    assert(ProductGATTValueRouter.route(
        characteristic: .unicodeNotify,
        value: Data([1, 1])
    ) == .unicode)
}

func testReleaseLinkStatusReportsTheFirmwareDefaultRate() {
    let status = ProductGATTAudioLinkStatus()
    assert(status.preferredSampleRateHertz == 16_000)
}

func testRecoveryPolicyIsBoundedAndReadyResetsBackoff() {
    var policy = ProductGATTRecoveryPolicy()
    assert(policy.apply(.start).retryAfterMilliseconds == 0)
    assert(policy.apply(.candidateSelected).watchdogMilliseconds == 8_000)
    assert(policy.apply(.failed).retryAfterMilliseconds == 500)
    assert(policy.apply(.failed).retryAfterMilliseconds == 1_000)
    assert(policy.apply(.failed).retryAfterMilliseconds == 2_000)
    assert(policy.apply(.failed).retryAfterMilliseconds == 5_000)
    assert(policy.apply(.failed).retryAfterMilliseconds == 5_000)
    assert(policy.apply(.ready).phase == .ready)
    assert(policy.apply(.failed).retryAfterMilliseconds == 500)
}

func testRecoveryPolicyTimeoutStopAndBluetoothState() {
    var policy = ProductGATTRecoveryPolicy()
    _ = policy.apply(.start)
    let connecting = policy.apply(.candidateSelected)
    let timedOut = policy.apply(.timedOut)
    assert(timedOut.cancelPeripheral)
    assert(timedOut.retryAfterMilliseconds == 500)
    assert(timedOut.generation > connecting.generation)

    let stopped = policy.apply(.stop)
    assert(stopped.phase == .stopped)
    assert(stopped.retryAfterMilliseconds == nil)
    assert(policy.apply(.failed).retryAfterMilliseconds == nil)
    assert(policy.apply(.timedOut).retryAfterMilliseconds == nil)

    assert(policy.apply(.start).retryAfterMilliseconds == 0)
    let unavailable = policy.apply(.bluetoothUnavailable)
    assert(unavailable.phase == .idle)
    assert(unavailable.retryAfterMilliseconds == nil)
    assert(policy.apply(.bluetoothPoweredOn).retryAfterMilliseconds == 0)
    assert(policy.apply(.scanStarted).watchdogMilliseconds == 8_000)
    assert(policy.apply(.candidateSelected).phase == .connecting)
    assert(policy.apply(.connected).phase == .discovering)
    assert(policy.apply(.subscribing).phase == .subscribing)
    assert(policy.apply(.subscribing).watchdogMilliseconds == 8_000)
}

testUnifiedDiscoveryAndBindingOrder()
testShutdownAndDisconnectClearAudioWithoutDisablingUnicode()
testAudioParseFailureDoesNotDisableUnicodeContract()
testReleaseLinkStatusReportsTheFirmwareDefaultRate()
testRecoveryPolicyIsBoundedAndReadyResetsBackoff()
testRecoveryPolicyTimeoutStopAndBluetoothState()
print("ProductGATT tests passed")
