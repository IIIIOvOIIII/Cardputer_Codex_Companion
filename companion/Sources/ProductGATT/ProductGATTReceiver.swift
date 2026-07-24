@preconcurrency import CoreBluetooth
import Foundation
import ProductUnicode

private enum ProductUUID {
    static let service = CBUUID(
        string: "7A100001-2C4D-4F20-9F20-434F44455831"
    )
    static let notify = CBUUID(
        string: "7A100002-2C4D-4F20-9F20-434F44455831"
    )
    static let control = CBUUID(
        string: "7A100003-2C4D-4F20-9F20-434F44455831"
    )
}

public enum ProductGATTDeviceIdentity {
    public static let currentName = "Cardputer Codex"
    public static let legacyName = "Cardputer Companion"

    public static func accepts(
        peripheralName: String?,
        advertisedName: String?
    ) -> Bool {
        peripheralName == currentName ||
            advertisedName == currentName ||
            peripheralName == legacyName ||
            advertisedName == legacyName
    }
}

private struct PartialText {
    let count: Int
    var fragments: [Int: Data]
}

public final class ProductGATTReceiver: NSObject, @unchecked Sendable {
    private let queue = DispatchQueue(label: "com.lynx.cardputer.gatt")
    private let injector: UnicodeInjector
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var notifyCharacteristic: CBCharacteristic?
    private var controlCharacteristic: CBCharacteristic?
    private var partial: [UInt32: PartialText] = [:]

    public init(injector: UnicodeInjector = UnicodeInjector()) {
        self.injector = injector
        super.init()
        central = CBCentralManager(delegate: self, queue: queue)
    }

    public func start() {
        queue.async { [weak self] in
            guard let self, self.central.state == .poweredOn else { return }
            if let connected = self.central.retrieveConnectedPeripherals(
                withServices: [ProductUUID.service]
            ).first {
                self.peripheral = connected
                connected.delegate = self
                self.central.connect(connected)
                return
            }
            self.central.scanForPeripherals(
                withServices: nil,
                options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
            )
        }
    }

    private func receive(_ data: Data) {
        guard data.count >= 10,
              data[0] == 1,
              data[1] == 1 else {
            return
        }
        let operationID =
            UInt32(data[2]) << 24 |
            UInt32(data[3]) << 16 |
            UInt32(data[4]) << 8 |
            UInt32(data[5])
        let index = Int(data[6])
        let count = Int(data[7])
        let length = Int(data[8]) << 8 | Int(data[9])
        guard count > 0, index < count, length == data.count - 10 else {
            return
        }
        var value = partial[operationID] ?? PartialText(
            count: count,
            fragments: [:]
        )
        guard value.count == count, value.fragments[index] == nil else {
            partial.removeValue(forKey: operationID)
            return
        }
        value.fragments[index] = data.subdata(in: 10..<data.count)
        partial[operationID] = value
        guard value.fragments.count == count else { return }
        var complete = Data()
        for fragmentIndex in 0..<count {
            guard let fragment = value.fragments[fragmentIndex] else { return }
            complete.append(fragment)
        }
        partial.removeValue(forKey: operationID)
        guard complete.count <= 1024,
              let text = String(data: complete, encoding: .utf8) else {
            return
        }
        DispatchQueue.main.async { [injector] in
            try? injector.inject(text)
        }
    }
}

extension ProductGATTReceiver: CBCentralManagerDelegate {
    public func centralManagerDidUpdateState(_ central: CBCentralManager) {
        guard central.state == .poweredOn else { return }
        start()
    }

    public func centralManager(
        _ central: CBCentralManager,
        didDiscover peripheral: CBPeripheral,
        advertisementData: [String: Any],
        rssi RSSI: NSNumber
    ) {
        let advertisedName =
            advertisementData[CBAdvertisementDataLocalNameKey] as? String
        guard ProductGATTDeviceIdentity.accepts(
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

    public func centralManager(
        _ central: CBCentralManager,
        didConnect peripheral: CBPeripheral
    ) {
        peripheral.discoverServices([ProductUUID.service])
    }

    public func centralManager(
        _ central: CBCentralManager,
        didDisconnectPeripheral peripheral: CBPeripheral,
        timestamp: CFAbsoluteTime,
        isReconnecting: Bool,
        error: (any Error)?
    ) {
        self.peripheral = nil
        notifyCharacteristic = nil
        controlCharacteristic = nil
        start()
    }
}

extension ProductGATTReceiver: CBPeripheralDelegate {
    public func peripheral(
        _ peripheral: CBPeripheral,
        didDiscoverServices error: (any Error)?
    ) {
        guard error == nil else { return }
        for service in peripheral.services ?? []
        where service.uuid == ProductUUID.service {
            peripheral.discoverCharacteristics(
                [ProductUUID.notify, ProductUUID.control],
                for: service
            )
        }
    }

    public func peripheral(
        _ peripheral: CBPeripheral,
        didDiscoverCharacteristicsFor service: CBService,
        error: (any Error)?
    ) {
        guard error == nil else { return }
        for characteristic in service.characteristics ?? [] {
            if characteristic.uuid == ProductUUID.notify {
                notifyCharacteristic = characteristic
                peripheral.setNotifyValue(true, for: characteristic)
            } else if characteristic.uuid == ProductUUID.control {
                controlCharacteristic = characteristic
                peripheral.writeValue(
                    Data("BIND1".utf8),
                    for: characteristic,
                    type: .withResponse
                )
            }
        }
    }

    public func peripheral(
        _ peripheral: CBPeripheral,
        didUpdateValueFor characteristic: CBCharacteristic,
        error: (any Error)?
    ) {
        guard error == nil,
              characteristic.uuid == ProductUUID.notify,
              let value = characteristic.value else {
            return
        }
        receive(value)
    }
}
