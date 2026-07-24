import Foundation

public enum StableErrorCode: String, Codable, Sendable, Error {
    case invalidRequest = "invalid_request"
    case unauthenticated
    case forbidden
    case permissionDenied = "permission_denied"
    case secureInputActive = "secure_input_active"
    case partial
    case indeterminate
    case resultExpired = "result_expired"
    case timeout
    case focusChanged = "focus_changed"
    case replay
    case malformedFrame = "malformed_frame"
    case identityMismatch = "identity_mismatch"
    case interfaceChanged = "interface_changed"
}

public enum GateStatus: String, Codable, Sendable {
    case pass
    case fail
    case blocked
}
