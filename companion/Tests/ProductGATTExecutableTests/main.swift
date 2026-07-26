import Foundation
import ProductAudio
import ProductGATT

func testUnifiedDiscoveryAndBindingOrder() {
    assert(ProductGATTContract.centralManagerCount == 1)
    assert(ProductGATTContract.maximumPeripheralCount == 1)
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

testUnifiedDiscoveryAndBindingOrder()
testShutdownAndDisconnectClearAudioWithoutDisablingUnicode()
testAudioParseFailureDoesNotDisableUnicodeContract()
print("ProductGATT tests passed")
