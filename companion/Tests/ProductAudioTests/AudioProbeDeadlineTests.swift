import ProductAudio

func testAudioProbeDeadlineUsesAbsoluteElapsedTime() {
    let start: UInt64 = 10_000_000_000
    let deadline = AudioProbeDeadline(
        durationSeconds: 1_800,
        startedAtNanoseconds: start
    )

    assert(
        deadline.remainingNanoseconds(at: start)
            == 1_800_000_000_000
    )
    assert(
        deadline.remainingNanoseconds(
            at: start + 1_799_750_000_000
        ) == 250_000_000
    )
    assert(
        deadline.remainingNanoseconds(
            at: start + 1_800_000_000_000
        ) == 0
    )
    assert(
        deadline.remainingNanoseconds(
            at: start + 1_801_000_000_000
        ) == 0
    )
}
