import CoreAudio
import Foundation

public enum CoreAudioDeviceCatalog {
    public static func containsInputDevice(named expectedName: String) -> Bool {
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDevices,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        var byteCount: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(
            AudioObjectID(kAudioObjectSystemObject),
            &address,
            0,
            nil,
            &byteCount
        ) == noErr else {
            return false
        }
        let count = Int(byteCount) / MemoryLayout<AudioObjectID>.size
        var devices = [AudioObjectID](repeating: 0, count: count)
        guard AudioObjectGetPropertyData(
            AudioObjectID(kAudioObjectSystemObject),
            &address,
            0,
            nil,
            &byteCount,
            &devices
        ) == noErr else {
            return false
        }
        return devices.contains { device in
            deviceName(device) == expectedName && hasInputStream(device)
        }
    }

    private static func deviceName(_ device: AudioObjectID) -> String? {
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioObjectPropertyName,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        var name: Unmanaged<CFString>?
        var size = UInt32(MemoryLayout.size(ofValue: name))
        guard AudioObjectGetPropertyData(
            device,
            &address,
            0,
            nil,
            &size,
            &name
        ) == noErr else {
            return nil
        }
        return name?.takeUnretainedValue() as String?
    }

    private static func hasInputStream(_ device: AudioObjectID) -> Bool {
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioDevicePropertyStreams,
            mScope: kAudioDevicePropertyScopeInput,
            mElement: kAudioObjectPropertyElementMain
        )
        var size: UInt32 = 0
        return AudioObjectGetPropertyDataSize(
            device,
            &address,
            0,
            nil,
            &size
        ) == noErr && size >= UInt32(MemoryLayout<AudioStreamID>.size)
    }
}

