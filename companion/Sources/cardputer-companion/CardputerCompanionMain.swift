import ApplicationServices
import CodexAppServer
import Foundation
import ProductContracts
import ProductAudio
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
                print("cardputer-companion 1.3.0")
            case .doctor:
                doctor()
            case .doctorAudio:
                try await AudioDriverOperations.doctor()
            case .audioDeviceStatus:
                let present = CoreAudioDeviceCatalog.containsInputDevice(
                    named: "Cardputer Codex Microphone"
                )
                print(present ? "PRESENT" : "ABSENT")
                Foundation.exit(present ? 0 : 1)
            case .installAudioDriver:
                try AudioDriverOperations.install()
            case .uninstallAudioDriver:
                try AudioDriverOperations.uninstall()
            case .run:
                try await run(configuration)
            case .audioProbe(let duration, let metricsURL):
                try await audioProbe(
                    duration: duration,
                    metricsURL: metricsURL
                )
            }
        } catch ConfigurationError.invalidDuration {
            usage()
            Foundation.exit(64)
        } catch ConfigurationError.usage {
            usage()
            Foundation.exit(64)
        } catch AudioDriverOperationError.requiresSudo {
            FileHandle.standardError.write(
                Data(
                    (
                        "cardputer-companion: audio driver mutation " +
                            "requires sudo\n"
                    ).utf8
                )
            )
            Foundation.exit(77)
        } catch {
            let message = (error as NSError).localizedDescription
            FileHandle.standardError.write(
                Data("cardputer-companion: \(message)\n".utf8)
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
        let audioBridge = AudioBridgeCoordinator()
        let receiver = ProductGATTReceiver()
        let audioRecovery = AudioBridgeRecoveryPolicy()
        audioBridge.readinessHandler = { ready in
            switch audioRecovery.bridgeReadinessChanged(ready) {
            case .none:
                break
            case .restartReceiverWithAudio:
                receiver.stop()
                receiver.start(audioSink: audioBridge)
            case .suspendAudio:
                receiver.suspendAudioSink()
            case .resumeAudio:
                receiver.resumeAudioSink()
            }
        }
        _ = audioBridge.reconnectIfNeeded()
        receiver.start(
            audioSink: audioRecovery.receiverWillStart()
                ? audioBridge
                : nil
        )
        defer {
            receiver.stop()
            audioBridge.stop()
        }
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
        var pinRevision = configuration.pinRevision
        print("Cardputer Companion running for \(deviceURL.host ?? "LAN device")")
        while !Task.isCancelled {
            if !audioBridge.isReady {
                _ = audioBridge.reconnectIfNeeded()
            }
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
                if let migration = action.pairingMigration,
                   migration.pinRevision > pinRevision {
                    if let configURL = configuration.configURL {
                        try PairingConfigWriter.persist(
                            migration,
                            to: configURL
                        )
                    } else {
                        FileHandle.standardError.write(
                            Data(
                                (
                                    "pairing migration is process-only; " +
                                        "update the command-line config\n"
                                ).utf8
                            )
                        )
                    }
                    bridge.updatePairing(
                        migration.nextPairing,
                        revision: migration.pinRevision
                    )
                    pinRevision = migration.pinRevision
                }
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

    private struct AudioProbeReport: Encodable {
        let durationSeconds: Int
        let capturedFrames: UInt64
        let receivedFrames: UInt64
        let sourceOverruns: UInt64
        let transportDrops: UInt64
        let sequenceGaps: UInt64
        let maxGapMs: UInt64
        let sampleRateHz: Int
        let bleReconnects: UInt64
        let signalPeak: Int32
        let signalRms: Double
        let nonzeroSamplePercent: Double

        enum CodingKeys: String, CodingKey {
            case durationSeconds = "duration_seconds"
            case capturedFrames = "captured_frames"
            case receivedFrames = "received_frames"
            case sourceOverruns = "source_overruns"
            case transportDrops = "transport_drops"
            case sequenceGaps = "sequence_gaps"
            case maxGapMs = "max_gap_ms"
            case sampleRateHz = "sample_rate_hz"
            case bleReconnects = "ble_reconnects"
            case signalPeak = "signal_peak"
            case signalRms = "signal_rms"
            case nonzeroSamplePercent = "nonzero_sample_percent"
        }
    }

    private static func audioProbe(
        duration: Int,
        metricsURL: URL
    ) async throws {
        let receiver = ProductGATTReceiver()
        let audioBridge = AudioBridgeCoordinator()
        defer {
            receiver.stop()
            audioBridge.stop()
        }
        var sinkGate = AudioProbeSinkGate(timeoutSeconds: 30)
        bridgeLoop: while true {
            let bridgeReady = audioBridge.reconnectIfNeeded()
            switch sinkGate.observe(bridgeReady: bridgeReady) {
            case .waiting:
                try await Task.sleep(for: .seconds(1))
            case .ready:
                break bridgeLoop
            case .timedOut:
                throw AudioProbeError.audioBridgeUnavailable
            }
        }
        receiver.start(audioSink: audioBridge)
        print("Audio probe ready; USB HIL will start capture")
        var startGate = AudioProbeStartGate(timeoutSeconds: 120)
        startLoop: while true {
            try await Task.sleep(for: .seconds(1))
            let metrics = receiver.audioMetrics
            switch startGate.observe(receivedFrames: metrics.receivedFrames) {
            case .waiting:
                continue
            case .started:
                print("Audio capture started; beginning \(duration)-second gate")
                break startLoop
            case .timedOut:
                throw AudioProbeError.captureStartTimedOut
            }
        }
        let baseline = receiver.audioMetrics
        let startedAt = DispatchTime.now().uptimeNanoseconds
        let deadline = AudioProbeDeadline(
            durationSeconds: duration,
            startedAtNanoseconds: startedAt
        )
        var nextReportSecond = 5
        while true {
            let remaining = deadline.remainingNanoseconds(
                at: DispatchTime.now().uptimeNanoseconds
            )
            if remaining == 0 {
                break
            }
            try await Task.sleep(
                for: .nanoseconds(Int64(min(remaining, 1_000_000_000)))
            )
            let elapsedSeconds = Int(
                (DispatchTime.now().uptimeNanoseconds - startedAt)
                    / 1_000_000_000
            )
            if elapsedSeconds >= nextReportSecond {
                let value = receiver.audioMetrics
                print(
                    "audio metrics: received=" +
                    "\(value.receivedFrames - baseline.receivedFrames) " +
                    "gaps=" +
                    "\(value.pipeline.sequenceGaps - baseline.pipeline.sequenceGaps) " +
                    "max_gap_ms=\(value.maximumGapMilliseconds)"
                )
                nextReportSecond =
                    (elapsedSeconds / 5 + 1) * 5
            }
        }
        let metrics = receiver.audioMetrics
        let receivedFrames = metrics.receivedFrames - baseline.receivedFrames
        let sequenceGaps =
            metrics.pipeline.sequenceGaps - baseline.pipeline.sequenceGaps
        let signalValueCount =
            metrics.pipeline.signalValueCount -
            baseline.pipeline.signalValueCount
        let signalNonzeroValues =
            metrics.pipeline.signalNonzeroValues -
            baseline.pipeline.signalNonzeroValues
        let signalSquareSum =
            metrics.pipeline.signalSquareSum -
            baseline.pipeline.signalSquareSum
        let signalRms = signalValueCount == 0
            ? 0
            : sqrt(signalSquareSum / Double(signalValueCount))
        let nonzeroSamplePercent = signalValueCount == 0
            ? 0
            : Double(signalNonzeroValues) * 100 /
                Double(signalValueCount)
        let report = AudioProbeReport(
            durationSeconds: duration,
            capturedFrames: receivedFrames + sequenceGaps,
            receivedFrames: receivedFrames,
            sourceOverruns: 0,
            transportDrops: 0,
            sequenceGaps: sequenceGaps,
            maxGapMs: metrics.maximumGapMilliseconds,
            sampleRateHz: metrics.sampleRateHertz,
            bleReconnects: metrics.reconnects - baseline.reconnects,
            signalPeak: metrics.pipeline.signalPeak,
            signalRms: signalRms,
            nonzeroSamplePercent: nonzeroSamplePercent
        )
        let data = try JSONEncoder().encode(report)
        try FileManager.default.createDirectory(
            at: metricsURL.deletingLastPathComponent(),
            withIntermediateDirectories: true
        )
        try data.write(to: metricsURL, options: .atomic)
        print("Audio probe metrics: \(metricsURL.path)")
    }

    private enum AudioProbeError: Error {
        case audioBridgeUnavailable
        case captureStartTimedOut
    }

    private static func usage() {
        FileHandle.standardError.write(
            Data(
                """
                usage:
                  cardputer-companion --version
                  cardputer-companion doctor
                  cardputer-companion doctor audio
                  cardputer-companion audio-device-status
                  sudo cardputer-companion install-audio-driver
                  sudo cardputer-companion uninstall-audio-driver
                  cardputer-companion audio-probe --duration 600 --metrics PATH
                  cardputer-companion run --device https://CARDPUTER-IP --pairing 12345678
                  cardputer-companion run --config ~/Library/Application\\ Support/CardputerCodexCompanion/config.json

                """.utf8
            )
        )
    }
}
