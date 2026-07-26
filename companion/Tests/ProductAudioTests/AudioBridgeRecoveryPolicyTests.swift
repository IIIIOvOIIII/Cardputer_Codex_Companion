import ProductAudio

func testAudioBridgeRecoveryEnablesLateDriverWithoutAutoRecording() {
    let policy = AudioBridgeRecoveryPolicy()

    assert(
        policy.bridgeReadinessChanged(true) == .none,
        "readiness before receiver startup must only be remembered"
    )
    assert(policy.receiverWillStart())

    assert(policy.bridgeReadinessChanged(false) == .suspendAudio)
    assert(policy.bridgeReadinessChanged(true) == .resumeAudio)
}

func testAudioBridgeRecoveryRestartsInitiallyUnicodeOnlyReceiver() {
    let policy = AudioBridgeRecoveryPolicy()

    assert(!policy.receiverWillStart())
    assert(
        policy.bridgeReadinessChanged(true) == .restartReceiverWithAudio
    )
    assert(policy.bridgeReadinessChanged(false) == .suspendAudio)
    assert(policy.bridgeReadinessChanged(true) == .resumeAudio)
}
