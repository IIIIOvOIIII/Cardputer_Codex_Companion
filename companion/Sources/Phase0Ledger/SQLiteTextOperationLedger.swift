import Foundation
import Phase0Contracts

public protocol TextOperationLedger: Sendable {
    func fetch(pairedDeviceID: String, operationID: UUID) throws -> TextOperationRecord?
}
