import Foundation
import ProductContracts

enum ConfigurationError: Error {
    case usage
    case invalidDeviceURL
    case invalidPairingCode
}

struct CompanionConfigFile: Decodable {
    let device: String
    let pairing: String
    let pinRevision: UInt32?

    enum CodingKeys: String, CodingKey {
        case device, pairing
        case pinRevision = "pin_revision"
    }
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
    let configURL: URL?
    let pinRevision: UInt32

    static func parse(_ arguments: [String]) throws -> Configuration {
        if arguments == ["--version"] {
            return Configuration(
                command: .version,
                deviceURL: nil,
                pairingCode: nil,
                configURL: nil,
                pinRevision: 0
            )
        }
        if arguments == ["doctor"] {
            return Configuration(
                command: .doctor,
                deviceURL: nil,
                pairingCode: nil,
                configURL: nil,
                pinRevision: 0
            )
        }
        guard arguments.first == "run" else {
            throw ConfigurationError.usage
        }
        var deviceValue: String?
        var pairing: String?
        var configFile: URL?
        var pinRevision: UInt32 = 0
        var index = 1
        while index < arguments.count {
            guard index + 1 < arguments.count else {
                throw ConfigurationError.usage
            }
            switch arguments[index] {
            case "--device":
                deviceValue = arguments[index + 1]
            case "--pairing":
                pairing = arguments[index + 1]
            case "--config":
                configFile = URL(fileURLWithPath: arguments[index + 1])
            default:
                throw ConfigurationError.usage
            }
            index += 2
        }
        if let configFile {
            guard deviceValue == nil, pairing == nil else {
                throw ConfigurationError.usage
            }
            let data = try Data(contentsOf: configFile)
            let config = try JSONDecoder().decode(
                CompanionConfigFile.self,
                from: data
            )
            deviceValue = config.device
            pairing = config.pairing
            pinRevision = config.pinRevision ?? 0
        }
        let device = deviceValue.flatMap(URL.init(string:))
        guard let device, isLANDeviceURL(device) else {
            throw ConfigurationError.invalidDeviceURL
        }
        guard let pairing,
              PairingConfigWriter.validPairing(pairing) else {
            throw ConfigurationError.invalidPairingCode
        }
        return Configuration(
            command: .run,
            deviceURL: device,
            pairingCode: pairing,
            configURL: configFile,
            pinRevision: pinRevision
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
