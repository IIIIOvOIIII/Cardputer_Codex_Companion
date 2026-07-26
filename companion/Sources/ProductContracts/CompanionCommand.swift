public enum CompanionCommandError: Error, Equatable {
    case usage
}

public enum CompanionCommand: Equatable, Sendable {
    case version
    case doctor
    case doctorAudio
    case installAudioDriver
    case uninstallAudioDriver
    case run
    case audioProbe

    public static func parse(_ arguments: [String]) throws -> CompanionCommand {
        switch arguments {
        case ["--version"]:
            return .version
        case ["doctor"]:
            return .doctor
        case ["doctor", "audio"]:
            return .doctorAudio
        case ["install-audio-driver"]:
            return .installAudioDriver
        case ["uninstall-audio-driver"]:
            return .uninstallAudioDriver
        default:
            if arguments.first == "run" {
                return .run
            }
            if arguments.first == "audio-probe" {
                return .audioProbe
            }
            throw CompanionCommandError.usage
        }
    }
}

