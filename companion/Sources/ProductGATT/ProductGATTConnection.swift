@preconcurrency import CoreBluetooth
import Dispatch
import Foundation
import OSLog
import ProductAudio

public enum ProductGATTCharacteristic: String, CaseIterable, Hashable, Sendable {
    case unicodeNotify
    case unicodeControl
    case audioData
    case audioControl
    case audioStatus
}

public enum ProductGATTConnectionAction: Equatable, Sendable {
    case subscribeUnicode
    case writeBind
    case subscribeAudioData
    case subscribeAudioStatus
    case writeAudioHello
    case writeSinkReady
    case writeSinkNotReady
}

public enum ProductGATTContract {
    public static let centralManagerCount = 1
    public static let maximumPeripheralCount = 1
    public static let callbackQueueQoS: DispatchQoS.QoSClass =
        .userInteractive
    public static let serviceUUID =
        "7A100001-2C4D-4F20-9F20-434F44455831"
    public static let hidServiceUUID = "1812"
    public static let connectedPeripheralServiceUUIDs = [
        serviceUUID,
        hidServiceUUID,
    ]
    public static let characteristics: [ProductGATTCharacteristic] = [
        .unicodeNotify,
        .unicodeControl,
        .audioData,
        .audioControl,
        .audioStatus
    ]

    public static func requiredCharacteristics(
        audioEnabled: Bool
    ) -> [ProductGATTCharacteristic] {
        audioEnabled
            ? characteristics
            : [.unicodeNotify, .unicodeControl]
    }
}

public struct ProductGATTSessionState: Equatable, Sendable {
    public let audioEnabled: Bool
    public private(set) var unicodeEnabled = false
    public private(set) var characteristicsDiscovered = false
    public private(set) var audioNotificationsEnabled = false
    public private(set) var protocolNegotiated = false
    public private(set) var audioReady = false
    public private(set) var reconnectCount: UInt32 = 0
    private var bindComplete = false
    private var audioDataEnabled = false
    private var audioStatusEnabled = false
    private var helloComplete = false

    public init(audioEnabled: Bool) {
        self.audioEnabled = audioEnabled
    }

    public mutating func didDiscoverAllCharacteristics()
        -> [ProductGATTConnectionAction] {
        unicodeEnabled = true
        characteristicsDiscovered = audioEnabled
        return [.subscribeUnicode, .writeBind]
    }

    public mutating func didWriteBind(
        succeeded: Bool
    ) -> [ProductGATTConnectionAction] {
        bindComplete = succeeded
        guard succeeded, audioEnabled else { return [] }
        return [.subscribeAudioData, .subscribeAudioStatus]
    }

    public mutating func didSetAudioNotification(
        _ characteristic: ProductGATTCharacteristic,
        enabled: Bool
    ) -> [ProductGATTConnectionAction] {
        if characteristic == .audioData {
            audioDataEnabled = enabled
        } else if characteristic == .audioStatus {
            audioStatusEnabled = enabled
        }
        guard bindComplete, audioEnabled, audioDataEnabled,
              audioStatusEnabled, !helloComplete else {
            return []
        }
        audioNotificationsEnabled = true
        return [.writeAudioHello]
    }

    public mutating func didWriteAudioHello(
        succeeded: Bool
    ) -> [ProductGATTConnectionAction] {
        helloComplete = succeeded
        protocolNegotiated = succeeded
        return succeeded ? [.writeSinkReady] : []
    }

    public mutating func didWriteSinkReady(succeeded: Bool) {
        audioReady = succeeded
    }

    public mutating func beginIntentionalShutdown()
        -> [ProductGATTConnectionAction] {
        guard audioReady else { return [] }
        audioReady = false
        return [.writeSinkNotReady]
    }

    public mutating func beginAudioSuspension()
        -> [ProductGATTConnectionAction] {
        guard audioReady else { return [] }
        audioReady = false
        return [.writeSinkNotReady]
    }

    public mutating func resumeAudio() -> [ProductGATTConnectionAction] {
        guard audioEnabled, audioNotificationsEnabled, protocolNegotiated,
              !audioReady else {
            return []
        }
        return [.writeSinkReady]
    }

    public mutating func didDisconnect() {
        unicodeEnabled = false
        characteristicsDiscovered = false
        audioNotificationsEnabled = false
        protocolNegotiated = false
        audioReady = false
        bindComplete = false
        audioDataEnabled = false
        audioStatusEnabled = false
        helloComplete = false
        reconnectCount &+= 1
    }
}

public enum ProductGATTValueRoute: Equatable, Sendable {
    case unicode
    case audio([AudioWireFrame])
    case audioStatus
    case invalidAudio
    case ignored
}

public enum ProductGATTValueRouter {
    public static func route(
        characteristic: ProductGATTCharacteristic,
        value: Data
    ) -> ProductGATTValueRoute {
        switch characteristic {
        case .unicodeNotify:
            return .unicode
        case .audioData:
            guard let frames = try? AudioWireFrame.decodeBatch(value) else {
                return .invalidAudio
            }
            return .audio(frames)
        case .audioStatus:
            return .audioStatus
        case .unicodeControl, .audioControl:
            return .ignored
        }
    }
}

protocol ProductGATTConnectionDelegate: AnyObject {
    func productGATT(
        _ connection: ProductGATTConnection,
        didReceive characteristic: ProductGATTCharacteristic,
        value: Data
    )
    func productGATTDidDisconnect(
        _ connection: ProductGATTConnection,
        intentional: Bool
    )
    func productGATT(
        _ connection: ProductGATTConnection,
        didUpdate session: ProductGATTSessionState
    )
}

private enum ProductUUID {
    static let service = CBUUID(string: ProductGATTContract.serviceUUID)
    static let connectedPeripheralServices =
        ProductGATTContract.connectedPeripheralServiceUUIDs.map(CBUUID.init)
    static let values: [ProductGATTCharacteristic: CBUUID] = [
        .unicodeNotify: CBUUID(
            string: "7A100002-2C4D-4F20-9F20-434F44455831"
        ),
        .unicodeControl: CBUUID(
            string: "7A100003-2C4D-4F20-9F20-434F44455831"
        ),
        .audioData: CBUUID(
            string: "7A100005-2C4D-4F20-9F20-434F44455831"
        ),
        .audioControl: CBUUID(
            string: "7A100006-2C4D-4F20-9F20-434F44455831"
        ),
        .audioStatus: CBUUID(
            string: "7A100007-2C4D-4F20-9F20-434F44455831"
        )
    ]
}

final class ProductGATTConnection: NSObject, @unchecked Sendable {
    private enum PendingWrite {
        case bind
        case hello
        case sinkReady
        case sinkNotReadyDisconnect
        case sinkNotReadyOnly
    }

    private enum RecoveryTimerAction {
        case beginConnection
        case timeout
    }

    private let queue = DispatchQueue(
        label: "com.lynx.cardputer.gatt",
        qos: DispatchQoS(
            qosClass: ProductGATTContract.callbackQueueQoS,
            relativePriority: 0
        ),
        autoreleaseFrequency: .workItem
    )
    private weak var delegate: ProductGATTConnectionDelegate?
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var characteristics:
        [ProductGATTCharacteristic: CBCharacteristic] = [:]
    private var session = ProductGATTSessionState(audioEnabled: false)
    private var pendingWrite: PendingWrite?
    private var intentionalStop = false
    private var shutdownWaiter: DispatchSemaphore?
    private var recovery = ProductGATTRecoveryPolicy()
    private var recoveryTimer: DispatchSourceTimer?
    private let logger = Logger(
        subsystem: "com.lynx.cardputer-companion",
        category: "gatt-recovery"
    )

    init(delegate: ProductGATTConnectionDelegate) {
        self.delegate = delegate
        super.init()
        central = CBCentralManager(delegate: self, queue: queue)
    }

    func start(audioEnabled: Bool) {
        queue.async { [self] in
            intentionalStop = false
            session = ProductGATTSessionState(audioEnabled: audioEnabled)
            applyRecovery(.start, reason: "start")
        }
    }

    func stop() {
        let waiter = DispatchSemaphore(value: 0)
        queue.async { [self] in
            intentionalStop = true
            shutdownWaiter = waiter
            central.stopScan()
            applyRecovery(
                .stop,
                reason: "intentional_stop",
                cancelConnection: false
            )
            let actions = session.beginIntentionalShutdown()
            publishSession()
            if actions.isEmpty {
                disconnectNow()
                finishShutdownWait()
            } else {
                perform(actions, sinkNotReadyDisconnects: true)
            }
        }
        _ = waiter.wait(timeout: .now() + .milliseconds(500))
    }

    func suspendAudio() {
        queue.async { [self] in
            publishAndPerform(session.beginAudioSuspension())
        }
    }

    func resumeAudio() {
        queue.async { [self] in
            publishAndPerform(session.resumeAudio())
        }
    }

    private func beginConnection() {
        guard recovery.phase != .stopped,
              central.state == .poweredOn,
              peripheral == nil else {
            return
        }
        if let connected = central.retrieveConnectedPeripherals(
            withServices: ProductUUID.connectedPeripheralServices
        ).first {
            connect(connected)
            return
        }
        central.scanForPeripherals(
            withServices: nil,
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
        )
        applyRecovery(
            .scanStarted,
            reason: "scan_started",
            cancelConnection: false
        )
    }

    private func connect(_ candidate: CBPeripheral) {
        central.stopScan()
        peripheral = candidate
        candidate.delegate = self
        applyRecovery(.candidateSelected, reason: "candidate_selected")
        central.connect(candidate)
    }

    private func cancelRecoveryTimer() {
        recoveryTimer?.setEventHandler {}
        recoveryTimer?.cancel()
        recoveryTimer = nil
    }

    private func scheduleRecoveryTimer(
        milliseconds: Int,
        generation: UInt64,
        action: RecoveryTimerAction
    ) {
        cancelRecoveryTimer()
        let timer = DispatchSource.makeTimerSource(queue: queue)
        timer.schedule(deadline: .now() + .milliseconds(milliseconds))
        timer.setEventHandler { [weak self] in
            guard let self,
                  generation == recovery.generation else {
                return
            }
            switch action {
            case .beginConnection:
                beginConnection()
            case .timeout:
                recoverConnection(reason: "deadline", event: .timedOut)
            }
        }
        recoveryTimer = timer
        timer.resume()
    }

    @discardableResult
    private func applyRecovery(
        _ event: ProductGATTRecoveryEvent,
        reason: String,
        cancelConnection: Bool = true
    ) -> ProductGATTRecoveryDecision {
        cancelRecoveryTimer()
        let decision = recovery.apply(event)
        let phase = String(describing: decision.phase)
        let retry = decision.retryAfterMilliseconds ?? -1
        logger.notice(
            "phase=\(phase, privacy: .public) reason=\(reason, privacy: .public) retry_ms=\(retry, privacy: .public) generation=\(decision.generation, privacy: .public)"
        )
        guard decision.generation == recovery.generation else {
            return decision
        }
        if decision.cancelPeripheral,
           cancelConnection,
           let current = peripheral {
            central.cancelPeripheralConnection(current)
        }
        if let watchdog = decision.watchdogMilliseconds {
            scheduleRecoveryTimer(
                milliseconds: watchdog,
                generation: decision.generation,
                action: .timeout
            )
        } else if let retry = decision.retryAfterMilliseconds {
            scheduleRecoveryTimer(
                milliseconds: retry,
                generation: decision.generation,
                action: .beginConnection
            )
        }
        return decision
    }

    private func clearConnectionState(intentional: Bool) {
        characteristics.removeAll(keepingCapacity: true)
        pendingWrite = nil
        session.didDisconnect()
        publishSession()
        delegate?.productGATTDidDisconnect(self, intentional: intentional)
    }

    private func recoverConnection(
        reason: String,
        event: ProductGATTRecoveryEvent = .failed
    ) {
        guard !intentionalStop else { return }
        let current = peripheral
        peripheral = nil
        central.stopScan()
        clearConnectionState(intentional: false)
        if let current {
            central.cancelPeripheralConnection(current)
        }
        applyRecovery(event, reason: reason)
    }

    private func perform(
        _ actions: [ProductGATTConnectionAction],
        sinkNotReadyDisconnects: Bool = false
    ) {
        guard let peripheral else { return }
        for action in actions {
            switch action {
            case .subscribeUnicode:
                setNotify(.unicodeNotify, peripheral: peripheral)
            case .writeBind:
                write(
                    Data("BIND1".utf8),
                    to: .unicodeControl,
                    pending: .bind,
                    peripheral: peripheral
                )
            case .subscribeAudioData:
                setNotify(.audioData, peripheral: peripheral)
            case .subscribeAudioStatus:
                setNotify(.audioStatus, peripheral: peripheral)
            case .writeAudioHello:
                write(
                    Data([1, 1]),
                    to: .audioControl,
                    pending: .hello,
                    peripheral: peripheral
                )
            case .writeSinkReady:
                write(
                    Data([1, 2]),
                    to: .audioControl,
                    pending: .sinkReady,
                    peripheral: peripheral
                )
            case .writeSinkNotReady:
                write(
                    Data([1, 3]),
                    to: .audioControl,
                    pending: sinkNotReadyDisconnects
                        ? .sinkNotReadyDisconnect
                        : .sinkNotReadyOnly,
                    peripheral: peripheral
                )
            }
        }
    }

    private func publishSession() {
        delegate?.productGATT(self, didUpdate: session)
    }

    private func publishAndPerform(
        _ actions: [ProductGATTConnectionAction]
    ) {
        publishSession()
        perform(actions)
    }

    private func finishShutdownWait() {
        shutdownWaiter?.signal()
        shutdownWaiter = nil
    }

    private func setNotify(
        _ kind: ProductGATTCharacteristic,
        peripheral: CBPeripheral
    ) {
        guard let characteristic = characteristics[kind] else { return }
        peripheral.setNotifyValue(true, for: characteristic)
    }

    private func write(
        _ data: Data,
        to kind: ProductGATTCharacteristic,
        pending: PendingWrite,
        peripheral: CBPeripheral
    ) {
        guard pendingWrite == nil,
              let characteristic = characteristics[kind] else {
            return
        }
        pendingWrite = pending
        peripheral.writeValue(data, for: characteristic, type: .withResponse)
    }

    private func disconnectNow() {
        guard let peripheral else { return }
        central.cancelPeripheralConnection(peripheral)
    }

    private func kind(
        for characteristic: CBCharacteristic
    ) -> ProductGATTCharacteristic? {
        ProductUUID.values.first {
            $0.value == characteristic.uuid
        }?.key
    }
}

extension ProductGATTConnection: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state == .poweredOn {
            applyRecovery(
                .bluetoothPoweredOn,
                reason: "bluetooth_powered_on"
            )
        } else {
            cancelRecoveryTimer()
            let current = peripheral
            peripheral = nil
            central.stopScan()
            if current != nil {
                clearConnectionState(intentional: false)
            }
            if let current {
                central.cancelPeripheralConnection(current)
            }
            applyRecovery(
                .bluetoothUnavailable,
                reason: "bluetooth_unavailable"
            )
        }
    }

    func centralManager(
        _ central: CBCentralManager,
        didDiscover peripheral: CBPeripheral,
        advertisementData: [String: Any],
        rssi RSSI: NSNumber
    ) {
        let advertisedName =
            advertisementData[CBAdvertisementDataLocalNameKey] as? String
        guard recovery.phase != .stopped,
              self.peripheral == nil,
              ProductGATTDeviceIdentity.accepts(
                peripheralName: peripheral.name,
                advertisedName: advertisedName
              ) else {
            return
        }
        connect(peripheral)
    }

    func centralManager(
        _ central: CBCentralManager,
        didConnect peripheral: CBPeripheral
    ) {
        guard self.peripheral === peripheral else { return }
        applyRecovery(.connected, reason: "connected")
        peripheral.discoverServices([ProductUUID.service])
    }

    func centralManager(
        _ central: CBCentralManager,
        didFailToConnect peripheral: CBPeripheral,
        error: (any Error)?
    ) {
        guard self.peripheral === peripheral else { return }
        recoverConnection(reason: "connect_failed")
    }

    func centralManager(
        _ central: CBCentralManager,
        didDisconnectPeripheral peripheral: CBPeripheral,
        timestamp: CFAbsoluteTime,
        isReconnecting: Bool,
        error: (any Error)?
    ) {
        let wasIntentional = intentionalStop
        guard self.peripheral === peripheral else {
            if wasIntentional {
                finishShutdownWait()
            }
            return
        }
        self.peripheral = nil
        clearConnectionState(intentional: wasIntentional)
        finishShutdownWait()
        applyRecovery(
            .disconnected(intentional: wasIntentional),
            reason: wasIntentional
                ? "intentional_disconnect"
                : "disconnected"
        )
    }
}

extension ProductGATTConnection: CBPeripheralDelegate {
    func peripheral(
        _ peripheral: CBPeripheral,
        didDiscoverServices error: (any Error)?
    ) {
        guard self.peripheral === peripheral else { return }
        guard error == nil else {
            recoverConnection(reason: "service_discovery_failed")
            return
        }
        guard let service = peripheral.services?.first(
            where: { $0.uuid == ProductUUID.service }
        ) else {
            recoverConnection(reason: "service_missing")
            return
        }
        peripheral.discoverCharacteristics(
            Array(ProductUUID.values.values),
            for: service
        )
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didDiscoverCharacteristicsFor service: CBService,
        error: (any Error)?
    ) {
        guard self.peripheral === peripheral else { return }
        guard error == nil else {
            recoverConnection(reason: "characteristic_discovery_failed")
            return
        }
        for characteristic in service.characteristics ?? [] {
            if let kind = ProductUUID.values.first(
                where: { $0.value == characteristic.uuid }
            )?.key {
                characteristics[kind] = characteristic
            }
        }
        let required = ProductGATTContract.requiredCharacteristics(
            audioEnabled: session.audioEnabled
        )
        guard required.allSatisfy({
            characteristics[$0] != nil
        }) else {
            recoverConnection(reason: "required_characteristic_missing")
            return
        }
        applyRecovery(.subscribing, reason: "subscribing")
        let actions = session.didDiscoverAllCharacteristics()
        publishSession()
        perform(actions)
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didWriteValueFor characteristic: CBCharacteristic,
        error: (any Error)?
    ) {
        guard self.peripheral === peripheral else { return }
        guard let completed = pendingWrite else { return }
        pendingWrite = nil
        let succeeded = error == nil
        switch completed {
        case .bind:
            guard succeeded else {
                recoverConnection(reason: "bind_write_failed")
                return
            }
            let actions = session.didWriteBind(succeeded: succeeded)
            publishSession()
            perform(actions)
            if !session.audioEnabled {
                applyRecovery(.ready, reason: "unicode_ready")
            }
        case .hello:
            guard succeeded else {
                recoverConnection(reason: "hello_write_failed")
                return
            }
            let actions = session.didWriteAudioHello(succeeded: succeeded)
            publishSession()
            perform(actions)
        case .sinkReady:
            guard succeeded else {
                recoverConnection(reason: "sink_ready_write_failed")
                return
            }
            session.didWriteSinkReady(succeeded: succeeded)
            publishSession()
            applyRecovery(.ready, reason: "audio_ready")
        case .sinkNotReadyDisconnect:
            disconnectNow()
            finishShutdownWait()
        case .sinkNotReadyOnly:
            break
        }
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didUpdateNotificationStateFor characteristic: CBCharacteristic,
        error: (any Error)?
    ) {
        guard self.peripheral === peripheral,
              let kind = kind(for: characteristic) else {
            return
        }
        let isHandshakeNotification =
            kind == .unicodeNotify ||
            kind == .audioData ||
            kind == .audioStatus
        guard !isHandshakeNotification ||
                (error == nil && characteristic.isNotifying) else {
            recoverConnection(reason: "notification_setup_failed")
            return
        }
        guard kind == .audioData || kind == .audioStatus else { return }
        let actions = session.didSetAudioNotification(
            kind,
            enabled: characteristic.isNotifying
        )
        publishSession()
        perform(actions)
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didUpdateValueFor characteristic: CBCharacteristic,
        error: (any Error)?
    ) {
        guard self.peripheral === peripheral,
              error == nil,
              let value = characteristic.value,
              let kind = kind(for: characteristic) else {
            return
        }
        delegate?.productGATT(self, didReceive: kind, value: value)
    }
}
