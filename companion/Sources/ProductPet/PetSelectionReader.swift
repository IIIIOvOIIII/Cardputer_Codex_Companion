import Foundation
import ImageIO

public enum PetSelectionError: Error, Equatable {
    case missingCodexHome
    case missingConfiguration
    case missingSelection
    case invalidID
    case sourceNotFound
    case invalidManifest
    case pathTraversal
    case invalidAtlas
}

public struct PetSource: Equatable, Sendable {
    public enum AtlasVersion: UInt8, Sendable {
        case v1 = 1
        case v2 = 2
    }

    public let id: String
    public let atlasURL: URL
    public let atlasVersion: AtlasVersion

    public init(id: String, atlasURL: URL, atlasVersion: AtlasVersion) {
        self.id = id
        self.atlasURL = atlasURL
        self.atlasVersion = atlasVersion
    }
}

public enum PetAtlas {
    public static let columns = 8
    public static let cellWidth = 192
    public static let cellHeight = 208

    public static func expectedDimensions(
        version: PetSource.AtlasVersion
    ) -> (Int, Int) {
        switch version {
        case .v1:
            return (1536, 1872)
        case .v2:
            return (1536, 2288)
        }
    }

    public static func version(width: Int, height: Int) -> PetSource.AtlasVersion? {
        if (width, height) == expectedDimensions(version: .v1) {
            return .v1
        }
        if (width, height) == expectedDimensions(version: .v2) {
            return .v2
        }
        return nil
    }
}

private struct CustomPetManifest: Decodable {
    let id: String
    let spriteVersionNumber: UInt8?
    let spritesheetPath: String
}

public struct PetSelectionReader {
    public typealias AtlasDimensions = (URL) throws -> (Int, Int)

    private let environment: [String: String]
    private let fileManager: FileManager
    private let atlasDimensions: AtlasDimensions

    public init(
        environment: [String: String] = ProcessInfo.processInfo.environment,
        fileManager: FileManager = .default,
        atlasDimensions: @escaping AtlasDimensions = PetSelectionReader.readDimensions
    ) {
        self.environment = environment
        self.fileManager = fileManager
        self.atlasDimensions = atlasDimensions
    }

    public func selectedSource() throws -> PetSource {
        let home = try codexHome()
        let selected = try selectedID(
            from: home.appending(path: "config.toml")
        )
        if let official = try officialSource(id: selected, home: home) {
            return official
        }
        return try customSource(id: selected, home: home)
    }

    private func codexHome() throws -> URL {
        if let value = environment["CODEX_HOME"], !value.isEmpty {
            return URL(fileURLWithPath: value, isDirectory: true)
        }
        if let value = environment["HOME"], !value.isEmpty {
            return URL(fileURLWithPath: value, isDirectory: true)
                .appending(path: ".codex", directoryHint: .isDirectory)
        }
        throw PetSelectionError.missingCodexHome
    }

    private func selectedID(from configURL: URL) throws -> String {
        guard let text = try? String(contentsOf: configURL, encoding: .utf8) else {
            throw PetSelectionError.missingConfiguration
        }
        var inTui = false
        for rawLine in text.split(separator: "\n", omittingEmptySubsequences: false) {
            let line = stripComment(String(rawLine)).trimmingCharacters(in: .whitespaces)
            if line.hasPrefix("[") && line.hasSuffix("]") {
                inTui = line == "[tui]"
                continue
            }
            guard inTui, let equals = line.firstIndex(of: "=") else {
                continue
            }
            let key = line[..<equals].trimmingCharacters(in: .whitespaces)
            guard key == "pet" else { continue }
            let value = line[line.index(after: equals)...]
                .trimmingCharacters(in: .whitespaces)
            guard value.count >= 2,
                  let first = value.first,
                  first == "\"" || first == "'",
                  value.last == first else {
                throw PetSelectionError.invalidID
            }
            let id = String(value.dropFirst().dropLast())
            guard validID(id) else { throw PetSelectionError.invalidID }
            return id
        }
        throw PetSelectionError.missingSelection
    }

    private func stripComment(_ line: String) -> String {
        var quote: Character?
        var escaped = false
        for index in line.indices {
            let character = line[index]
            if escaped {
                escaped = false
                continue
            }
            if character == "\\", quote == "\"" {
                escaped = true
                continue
            }
            if character == "\"" || character == "'" {
                if quote == character {
                    quote = nil
                } else if quote == nil {
                    quote = character
                }
                continue
            }
            if character == "#", quote == nil {
                return String(line[..<index])
            }
        }
        return line
    }

    private func validID(_ id: String) -> Bool {
        !id.isEmpty &&
            id.utf8.count <= 64 &&
            id != "." &&
            id != ".." &&
            !id.contains("/") &&
            !id.contains("\\")
    }

    private func officialSource(id: String, home: URL) throws -> PetSource? {
        let directory = home.appending(
            path: "cache/tui-pets/v1/assets",
            directoryHint: .isDirectory
        )
        guard let entries = try? fileManager.contentsOfDirectory(
            at: directory,
            includingPropertiesForKeys: nil
        ) else {
            return nil
        }
        let prefix = "\(id)-spritesheet-v"
        let candidates = entries.compactMap { url -> (Int, URL)? in
            let name = url.lastPathComponent
            guard name.hasPrefix(prefix), name.hasSuffix(".webp") else {
                return nil
            }
            let versionText = name
                .dropFirst(prefix.count)
                .dropLast(".webp".count)
            guard let version = Int(versionText) else { return nil }
            return (version, url)
        }.sorted { $0.0 > $1.0 }

        for (_, url) in candidates {
            guard let dimensions = try? atlasDimensions(url),
                  let version = PetAtlas.version(
                    width: dimensions.0,
                    height: dimensions.1
                  ) else {
                continue
            }
            return PetSource(id: id, atlasURL: url, atlasVersion: version)
        }
        return nil
    }

    private func customSource(id: String, home: URL) throws -> PetSource {
        let petDirectory = home
            .appending(path: "pets", directoryHint: .isDirectory)
            .appending(path: id, directoryHint: .isDirectory)
            .standardizedFileURL
        let manifestURL = petDirectory.appending(path: "pet.json")
        guard let data = try? Data(contentsOf: manifestURL),
              let manifest = try? JSONDecoder().decode(
                CustomPetManifest.self,
                from: data
              ),
              manifest.id == id else {
            throw PetSelectionError.sourceNotFound
        }
        let version: PetSource.AtlasVersion
        switch manifest.spriteVersionNumber ?? 1 {
        case 1:
            version = .v1
        case 2:
            version = .v2
        default:
            throw PetSelectionError.invalidManifest
        }
        let atlasURL = petDirectory
            .appending(path: manifest.spritesheetPath)
            .standardizedFileURL
        let prefix = petDirectory.path.hasSuffix("/")
            ? petDirectory.path
            : petDirectory.path + "/"
        guard atlasURL.path.hasPrefix(prefix) else {
            throw PetSelectionError.pathTraversal
        }
        guard fileManager.fileExists(atPath: atlasURL.path),
              let dimensions = try? atlasDimensions(atlasURL),
              PetAtlas.version(
                width: dimensions.0,
                height: dimensions.1
              ) == version else {
            throw PetSelectionError.invalidAtlas
        }
        return PetSource(id: id, atlasURL: atlasURL, atlasVersion: version)
    }

    public static func readDimensions(_ url: URL) throws -> (Int, Int) {
        guard let source = CGImageSourceCreateWithURL(url as CFURL, nil),
              let properties = CGImageSourceCopyPropertiesAtIndex(
                source,
                0,
                nil
              ) as? [CFString: Any],
              let width = properties[kCGImagePropertyPixelWidth] as? Int,
              let height = properties[kCGImagePropertyPixelHeight] as? Int else {
            throw PetSelectionError.invalidAtlas
        }
        return (width, height)
    }
}
