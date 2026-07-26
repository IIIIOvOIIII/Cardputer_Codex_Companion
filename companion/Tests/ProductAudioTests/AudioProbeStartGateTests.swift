import ProductAudio

func testAudioProbeWaitsForFirstFrameAndTimesOut() {
    var gate = AudioProbeStartGate(timeoutSeconds: 3)
    assert(gate.observe(receivedFrames: 0) == .waiting)
    assert(gate.observe(receivedFrames: 1) == .started)

    gate = AudioProbeStartGate(timeoutSeconds: 3)
    assert(gate.observe(receivedFrames: 0) == .waiting)
    assert(gate.observe(receivedFrames: 0) == .waiting)
    assert(gate.observe(receivedFrames: 0) == .timedOut)
}
