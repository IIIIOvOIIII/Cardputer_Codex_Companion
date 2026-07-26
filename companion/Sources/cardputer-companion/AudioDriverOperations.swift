import Darwin
import Foundation
import ProductAudio
import ProductGATT

enum AudioDriverOperationError: LocalizedError {
    case requiresSudo
    case missingBundledResource(String)
    case helperFailed(Int32)
    case diagnosticFailed

    var errorDescription: String? {
        switch self {
        case .requiresSudo:
            return "audio driver mutation requires sudo"
        case .missingBundledResource(let name):
            return "missing bundled resource: \(name)"
        case .helperFailed(let status):
            return "audio driver helper failed with status \(status)"
        case .diagnosticFailed:
            return "one or more audio diagnostics failed"
        }
    }
}

enum AudioDriverOperations {
    static let installedDriverURL = URL(
        fileURLWithPath:
            "/Library/Audio/Plug-Ins/HAL/CardputerCodexMicrophone.driver"
    )

    static func install() throws {
        try mutate(operation: "install", includeDriver: true)
    }

    static func uninstall() throws {
        try mutate(operation: "uninstall", includeDriver: false)
    }

    static func doctor() async throws {
        let installedVersion = bundleVersion(at: installedDriverURL)
        let installed = installedVersion != nil
        print(
            "Audio driver installed: " +
                (installed ? "OK (\(installedVersion!))" : "MISSING")
        )

        let enumerated = CoreAudioDeviceCatalog.containsInputDevice(
            named: "Cardputer Codex Microphone"
        )
        print(
            "Core Audio input: " +
                (enumerated ? "OK" : "NOT ENUMERATED")
        )

        var bridgeReady = false
        var silenceFlow = false
        var bridge: AudioDriverConnection?
        do {
            let candidate = AudioDriverConnection(
                transport: try XPCDriverTransport()
            )
            try candidate.start()
            bridgeReady = candidate.isReady
            let silence = [Float](repeating: 0, count: 480)
            silenceFlow = silence.withUnsafeBufferPointer {
                candidate.write(samples: $0) == $0.count
            }
            bridge = candidate
        } catch {
            bridgeReady = false
        }
        print("XPC authentication: \(bridgeReady ? "OK" : "FAILED")")
        print("Shared ring silence: \(silenceFlow ? "OK" : "FAILED")")

        var gattReady = false
        if let bridge, bridge.isReady {
            let receiver = ProductGATTReceiver()
            receiver.start(audioSink: bridge)
            for _ in 0..<50 {
                if receiver.audioLinkStatus.audioReady {
                    gattReady = true
                    break
                }
                try await Task.sleep(for: .milliseconds(200))
            }
            let link = receiver.audioLinkStatus
            print(
                "BLE audio characteristics: " +
                    (link.characteristicsDiscovered ? "OK" : "MISSING")
            )
            print(
                "BLE audio subscription: " +
                    (gattReady ? "OK" : "NOT READY")
            )
            print(
                "Audio protocol: " +
                    (gattReady ? "v\(link.protocolVersion)" : "UNAVAILABLE")
            )
            print(
                "Preferred sample rate: " +
                    (gattReady ? "\(link.preferredSampleRateHertz) Hz" :
                        "UNAVAILABLE")
            )
            receiver.stop()
        } else {
            print("BLE audio characteristics: SKIPPED")
            print("BLE audio subscription: SKIPPED")
            print("Audio protocol: UNAVAILABLE")
            print("Preferred sample rate: UNAVAILABLE")
        }
        bridge?.stop()

        guard installed, enumerated, bridgeReady, silenceFlow, gattReady else {
            throw AudioDriverOperationError.diagnosticFailed
        }
    }

    static func startRuntimeBridge() -> AudioDriverConnection? {
        do {
            let connection = AudioDriverConnection(
                transport: try XPCDriverTransport()
            )
            try connection.start()
            return connection.isReady ? connection : nil
        } catch {
            FileHandle.standardError.write(
                Data("audio bridge unavailable: \(error)\n".utf8)
            )
            return nil
        }
    }

    private static func mutate(
        operation: String,
        includeDriver: Bool
    ) throws {
        guard geteuid() == 0 else {
            throw AudioDriverOperationError.requiresSudo
        }
        guard let resources = Bundle.main.resourceURL else {
            throw AudioDriverOperationError.missingBundledResource(
                "Contents/Resources"
            )
        }
        let helper = resources.appending(path: "install_audio_driver.sh")
        guard FileManager.default.isExecutableFile(atPath: helper.path) else {
            throw AudioDriverOperationError.missingBundledResource(
                "install_audio_driver.sh"
            )
        }
        var arguments = [operation]
        if includeDriver {
            let driver = resources.appending(
                path: "CardputerCodexMicrophone.driver"
            )
            guard FileManager.default.fileExists(atPath: driver.path) else {
                throw AudioDriverOperationError.missingBundledResource(
                    "CardputerCodexMicrophone.driver"
                )
            }
            arguments.append(driver.path)
        }
        let process = Process()
        process.executableURL = helper
        process.arguments = arguments
        process.standardOutput = FileHandle.standardOutput
        process.standardError = FileHandle.standardError
        try process.run()
        process.waitUntilExit()
        guard process.terminationStatus == 0 else {
            throw AudioDriverOperationError.helperFailed(
                process.terminationStatus
            )
        }
    }

    private static func bundleVersion(at url: URL) -> String? {
        guard let bundle = Bundle(url: url) else { return nil }
        return bundle.object(
            forInfoDictionaryKey: "CFBundleShortVersionString"
        ) as? String
    }
}

