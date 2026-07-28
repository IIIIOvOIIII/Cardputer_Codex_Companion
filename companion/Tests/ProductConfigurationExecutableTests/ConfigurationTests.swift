import Foundation
import ProductContracts

func testProductCommandParser() throws {
    let doctor = try CompanionCommand.parse(["doctor"])
    let doctorAudio = try CompanionCommand.parse(["doctor", "audio"])
    let audioDeviceStatus = try CompanionCommand.parse([
        "audio-device-status"
    ])
    let install = try CompanionCommand.parse(["install-audio-driver"])
    let uninstall = try CompanionCommand.parse(["uninstall-audio-driver"])
    let run = try CompanionCommand.parse(["run"])
    assert(doctor == .doctor)
    assert(doctorAudio == .doctorAudio)
    assert(audioDeviceStatus == .audioDeviceStatus)
    assert(install == .installAudioDriver)
    assert(uninstall == .uninstallAudioDriver)
    assert(run == .run)
    do {
        _ = try CompanionCommand.parse(["doctor", "other"])
        assertionFailure("unknown doctor target accepted")
    } catch CompanionCommandError.usage {
    }
}

try testProductCommandParser()
print("ProductConfiguration tests passed")
