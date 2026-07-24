import Foundation

enum ConfigurationError: Error {
    case usage
    case invalidDeviceURL
    case invalidPairingCode
}

struct Configuration {
    enum Command {
        case version
        case doctor
        case run
    }

    let command: Command
    let deviceURL: URL?
    let pairingCode: String?

    static func parse(_ arguments: [String]) throws -> Configuration {
        if arguments == ["--version"] {
            return Configuration(
                command: .version,
                deviceURL: nil,
                pairingCode: nil
            )
        }
        if arguments == ["doctor"] {
            return Configuration(
                command: .doctor,
                deviceURL: nil,
                pairingCode: nil
            )
        }
        guard arguments.first == "run" else {
            throw ConfigurationError.usage
        }
        var device: URL?
        var pairing: String?
        var index = 1
        while index < arguments.count {
            guard index + 1 < arguments.count else {
                throw ConfigurationError.usage
            }
            switch arguments[index] {
            case "--device":
                device = URL(string: arguments[index + 1])
            case "--pairing":
                pairing = arguments[index + 1]
            default:
                throw ConfigurationError.usage
            }
            index += 2
        }
        guard let device, isLANDeviceURL(device) else {
            throw ConfigurationError.invalidDeviceURL
        }
        guard let pairing,
              pairing.count == 8,
              pairing.allSatisfy(\.isNumber) else {
            throw ConfigurationError.invalidPairingCode
        }
        return Configuration(
            command: .run,
            deviceURL: device,
            pairingCode: pairing
        )
    }

    private static func isLANDeviceURL(_ url: URL) -> Bool {
        guard ["http", "https"].contains(url.scheme?.lowercased() ?? ""),
              let host = url.host?.lowercased() else {
            return false
        }
        if host.hasSuffix(".local") || host == "localhost" {
            return true
        }
        let parts = host.split(separator: ".").compactMap { Int($0) }
        guard parts.count == 4, parts.allSatisfy({ (0...255).contains($0) })
        else {
            return false
        }
        return parts[0] == 10 ||
            (parts[0] == 192 && parts[1] == 168) ||
            (parts[0] == 172 && (16...31).contains(parts[1]))
    }
}
