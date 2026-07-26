@preconcurrency import CoreBluetooth
import Foundation
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
    public static let serviceUUID =
        "7A100001-2C4D-4F20-9F20-434F44455831"
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
        return [.writeAudioHello]
    }

    public mutating func didWriteAudioHello(
        succeeded: Bool
    ) -> [ProductGATTConnectionAction] {
        helloComplete = succeeded
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

    public mutating func didDisconnect() {
        unicodeEnabled = false
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
    case audio(AudioWireFrame)
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
            guard let frame = try? AudioWireFrame(data: value) else {
                return .invalidAudio
            }
            return .audio(frame)
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
}

private enum ProductUUID {
    static let service = CBUUID(string: ProductGATTContract.serviceUUID)
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
        case sinkNotReady
    }

    private let queue = DispatchQueue(label: "com.lynx.cardputer.gatt")
    private weak var delegate: ProductGATTConnectionDelegate?
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var characteristics:
        [ProductGATTCharacteristic: CBCharacteristic] = [:]
    private var session = ProductGATTSessionState(audioEnabled: false)
    private var pendingWrite: PendingWrite?
    private var intentionalStop = false

    init(delegate: ProductGATTConnectionDelegate) {
        self.delegate = delegate
        super.init()
        central = CBCentralManager(delegate: self, queue: queue)
    }

    func start(audioEnabled: Bool) {
        queue.async { [self] in
            intentionalStop = false
            session = ProductGATTSessionState(audioEnabled: audioEnabled)
            beginConnection()
        }
    }

    func stop() {
        queue.async { [self] in
            intentionalStop = true
            let actions = session.beginIntentionalShutdown()
            if actions.isEmpty {
                disconnectNow()
            } else {
                perform(actions)
            }
        }
    }

    private func beginConnection() {
        guard central.state == .poweredOn, peripheral == nil else { return }
        if let connected = central.retrieveConnectedPeripherals(
            withServices: [ProductUUID.service]
        ).first {
            peripheral = connected
            connected.delegate = self
            central.connect(connected)
            return
        }
        central.scanForPeripherals(
            withServices: nil,
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
        )
    }

    private func perform(_ actions: [ProductGATTConnectionAction]) {
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
                    pending: .sinkNotReady,
                    peripheral: peripheral
                )
            }
        }
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
            beginConnection()
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
        guard self.peripheral == nil,
              ProductGATTDeviceIdentity.accepts(
                peripheralName: peripheral.name,
                advertisedName: advertisedName
              ) else {
            return
        }
        central.stopScan()
        self.peripheral = peripheral
        peripheral.delegate = self
        central.connect(peripheral)
    }

    func centralManager(
        _ central: CBCentralManager,
        didConnect peripheral: CBPeripheral
    ) {
        peripheral.discoverServices([ProductUUID.service])
    }

    func centralManager(
        _ central: CBCentralManager,
        didDisconnectPeripheral peripheral: CBPeripheral,
        timestamp: CFAbsoluteTime,
        isReconnecting: Bool,
        error: (any Error)?
    ) {
        let wasIntentional = intentionalStop
        self.peripheral = nil
        characteristics.removeAll(keepingCapacity: true)
        pendingWrite = nil
        session.didDisconnect()
        delegate?.productGATTDidDisconnect(self, intentional: wasIntentional)
        if !wasIntentional {
            beginConnection()
        }
    }
}

extension ProductGATTConnection: CBPeripheralDelegate {
    func peripheral(
        _ peripheral: CBPeripheral,
        didDiscoverServices error: (any Error)?
    ) {
        guard error == nil else { return }
        for service in peripheral.services ?? []
        where service.uuid == ProductUUID.service {
            peripheral.discoverCharacteristics(
                Array(ProductUUID.values.values),
                for: service
            )
        }
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didDiscoverCharacteristicsFor service: CBService,
        error: (any Error)?
    ) {
        guard error == nil else { return }
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
            return
        }
        perform(session.didDiscoverAllCharacteristics())
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didWriteValueFor characteristic: CBCharacteristic,
        error: (any Error)?
    ) {
        guard let completed = pendingWrite else { return }
        pendingWrite = nil
        let succeeded = error == nil
        switch completed {
        case .bind:
            perform(session.didWriteBind(succeeded: succeeded))
        case .hello:
            perform(session.didWriteAudioHello(succeeded: succeeded))
        case .sinkReady:
            session.didWriteSinkReady(succeeded: succeeded)
        case .sinkNotReady:
            disconnectNow()
        }
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didUpdateNotificationStateFor characteristic: CBCharacteristic,
        error: (any Error)?
    ) {
        guard error == nil,
              let kind = kind(for: characteristic),
              kind == .audioData || kind == .audioStatus else {
            return
        }
        perform(session.didSetAudioNotification(
            kind,
            enabled: characteristic.isNotifying
        ))
    }

    func peripheral(
        _ peripheral: CBPeripheral,
        didUpdateValueFor characteristic: CBCharacteristic,
        error: (any Error)?
    ) {
        guard error == nil,
              let value = characteristic.value,
              let kind = kind(for: characteristic) else {
            return
        }
        delegate?.productGATT(self, didReceive: kind, value: value)
    }
}
