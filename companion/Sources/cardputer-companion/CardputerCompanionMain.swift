import ApplicationServices
import CodexAppServer
import Foundation
import ProductContracts
import ProductGATT
import ProductPet
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
        let petSync = PetSyncCoordinator(
            reader: PetSelectionReader(),
            transcoder: PetTranscoder()
        )
        var lastPostedSnapshot: CompanionSnapshot?
        var wireSequence: UInt64 = 0
        var synchronizedPet = PetSyncResult(
            petID: "",
            digest: "",
            errorCode: nil
        )
        let clock = ContinuousClock()
        var petSyncCadence = PetSyncCadence()
        print("Cardputer Companion running for \(deviceURL.host ?? "LAN device")")
        while !Task.isCancelled {
            let now = clock.now
            if petSyncCadence.isDue(at: now) {
                synchronizedPet = await petSync.synchronize(client: bridge)
                petSyncCadence.record(result: synchronizedPet, at: now)
                if let errorCode = synchronizedPet.errorCode {
                    FileHandle.standardError.write(
                        Data(
                            (
                                "pet sync warning: \(errorCode); " +
                                    "retry in 5 seconds\n"
                            ).utf8
                        )
                    )
                } else {
                    FileHandle.standardOutput.write(
                        Data(
                            (
                                "pet sync: \(synchronizedPet.petID); " +
                                    "next check in 30 seconds\n"
                            ).utf8
                        )
                    )
                }
            }
            do {
                let action = try await bridge.pollAction()
                if action.needsSnapshot {
                    lastPostedSnapshot = nil
                }
                try adapter.perform(action.action)
                let currentSnapshot = try adapter.snapshot().withPet(
                    id: synchronizedPet.petID,
                    digest: synchronizedPet.digest
                )
                try await postSnapshotIfChanged(
                    currentSnapshot,
                    bridge: bridge,
                    lastPosted: &lastPostedSnapshot,
                    wireSequence: &wireSequence
                )
            } catch {
                FileHandle.standardError.write(
                    Data("sync warning: \(error)\n".utf8)
                )
            }
            try await Task.sleep(for: .seconds(2))
        }
        withExtendedLifetime(receiver) {}
    }

    private static func postSnapshotIfChanged(
        _ current: CompanionSnapshot,
        bridge: LANBridge,
        lastPosted: inout CompanionSnapshot?,
        wireSequence: inout UInt64
    ) async throws {
        guard lastPosted == nil ||
                !current.hasSameContent(as: lastPosted!) else {
            return
        }
        wireSequence += 1
        let snapshot = current.withSequence(wireSequence)
        try await bridge.post(snapshot)
        lastPosted = snapshot
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
