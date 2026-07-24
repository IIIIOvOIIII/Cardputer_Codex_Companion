import ApplicationServices
import CodexAppServer
import Foundation
import ProductContracts
import ProductGATT
import ProductUnicode

@main
struct CardputerCompanionMain {
    static func main() async {
        do {
            let configuration = try Configuration.parse(
                Array(CommandLine.arguments.dropFirst())
            )
            switch configuration.command {
            case .version:
                print("cardputer-companion 1.0.0")
            case .doctor:
                doctor()
            case .run:
                try await run(configuration)
            }
        } catch ConfigurationError.usage {
            usage()
            Foundation.exit(64)
        } catch {
            FileHandle.standardError.write(
                Data("cardputer-companion: \(error)\n".utf8)
            )
            Foundation.exit(1)
        }
    }

    private static func run(_ configuration: Configuration) async throws {
        guard let deviceURL = configuration.deviceURL,
              let pairingCode = configuration.pairingCode else {
            throw ConfigurationError.usage
        }
        let adapter = CodexAdapter()
        try adapter.start()
        let receiver = ProductGATTReceiver()
        receiver.start()
        let bridge = LANBridge(
            baseURL: deviceURL,
            pairingCode: pairingCode
        )
        var lastPostedSnapshot: CompanionSnapshot?
        var wireSequence: UInt64 = 0
        print("Cardputer Companion running for \(deviceURL.host ?? "LAN device")")
        while !Task.isCancelled {
            do {
                let action = try await bridge.pollAction()
                if action.needsSnapshot {
                    lastPostedSnapshot = nil
                }
                try adapter.perform(action.action)
                let currentSnapshot = try adapter.snapshot()
                if lastPostedSnapshot == nil ||
                    !currentSnapshot.hasSameContent(as: lastPostedSnapshot!) {
                    wireSequence += 1
                    let snapshot = currentSnapshot.withSequence(wireSequence)
                    try await bridge.post(snapshot)
                    lastPostedSnapshot = snapshot
                }
            } catch {
                FileHandle.standardError.write(
                    Data("sync warning: \(error)\n".utf8)
                )
            }
            try await Task.sleep(for: .seconds(2))
        }
        withExtendedLifetime(receiver) {}
    }

    private static func doctor() {
        let codexAvailable = ProcessInfo.processInfo.environment["PATH"]?
            .split(separator: ":")
            .map(String.init)
            .contains(where: {
                FileManager.default.isExecutableFile(
                    atPath: URL(fileURLWithPath: $0)
                        .appending(path: "codex").path
                )
            }) ?? false
        let injector = UnicodeInjector()
        print("Codex CLI: \(codexAvailable ? "OK" : "MISSING")")
        print(
            "Accessibility: \(injector.accessibilityTrusted ? "OK" : "PERMISSION REQUIRED")"
        )
        print(
            "Secure Input: \(injector.secureInputActive ? "ACTIVE" : "INACTIVE")"
        )
        print("Bluetooth: checked when run starts")
        print("LAN: provide an RFC1918 or .local Cardputer URL")
    }

    private static func usage() {
        FileHandle.standardError.write(
            Data(
                """
                usage:
                  cardputer-companion --version
                  cardputer-companion doctor
                  cardputer-companion run --device https://CARDPUTER-IP --pairing 12345678
                  cardputer-companion run --config ~/Library/Application\\ Support/CardputerCodexCompanion/config.json

                """.utf8
            )
        )
    }
}
