import ApplicationServices
import Carbon.HIToolbox
import Foundation

public enum UnicodeInjectionError: Error {
    case accessibilityPermissionRequired
    case secureInputActive
    case eventCreationFailed
    case textTooLong
}

public final class UnicodeInjector: @unchecked Sendable {
    public init() {}

    public var accessibilityTrusted: Bool {
        AXIsProcessTrusted()
    }

    public var secureInputActive: Bool {
        IsSecureEventInputEnabled()
    }

    public func inject(_ text: String) throws {
        let utf16 = Array(text.utf16)
        guard utf16.count <= 1024 else {
            throw UnicodeInjectionError.textTooLong
        }
        guard accessibilityTrusted else {
            throw UnicodeInjectionError.accessibilityPermissionRequired
        }
        guard !secureInputActive else {
            throw UnicodeInjectionError.secureInputActive
        }
        guard let source = CGEventSource(stateID: .hidSystemState),
              let down = CGEvent(
                  keyboardEventSource: source,
                  virtualKey: CGKeyCode(0),
                  keyDown: true
              ),
              let up = CGEvent(
                  keyboardEventSource: source,
                  virtualKey: CGKeyCode(0),
                  keyDown: false
              ) else {
            throw UnicodeInjectionError.eventCreationFailed
        }
        down.keyboardSetUnicodeString(
            stringLength: utf16.count,
            unicodeString: utf16
        )
        up.keyboardSetUnicodeString(stringLength: 0, unicodeString: [])
        down.post(tap: .cghidEventTap)
        up.post(tap: .cghidEventTap)
    }
}
