import ProductAudio

func testAudioProbeRequiresReadyHALBridge() {
    var gate = AudioProbeSinkGate(timeoutSeconds: 3)
    assert(gate.observe(bridgeReady: false) == .waiting)
    assert(gate.observe(bridgeReady: true) == .ready)

    gate = AudioProbeSinkGate(timeoutSeconds: 2)
    assert(gate.observe(bridgeReady: false) == .waiting)
    assert(gate.observe(bridgeReady: false) == .timedOut)
}
