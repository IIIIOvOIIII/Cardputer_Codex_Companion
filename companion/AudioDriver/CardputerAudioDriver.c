#include "CardputerAudioDevice.h"
#include "CardputerAudioIPC.h"

#include <CoreAudio/AudioHardware.h>
#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreFoundation/CFPlugInCOM.h>
#include <mach/mach_time.h>
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

#define UNUSED(value) ((void)(value))
#define CARDPUTER_DEVICE_UID "com.lynx.cardputer-codex-microphone.device"
#define CARDPUTER_MODEL_UID "com.lynx.cardputer-codex-microphone.model"
#define CARDPUTER_ZERO_TIMESTAMP_PERIOD 480u

static HRESULT driver_query_interface(
    void *driver,
    REFIID uuid,
    LPVOID *out_interface);
static ULONG driver_add_ref(void *driver);
static ULONG driver_release(void *driver);
static OSStatus driver_initialize(
    AudioServerPlugInDriverRef driver,
    AudioServerPlugInHostRef host);
static OSStatus driver_create_device(
    AudioServerPlugInDriverRef driver,
    CFDictionaryRef description,
    const AudioServerPlugInClientInfo *client,
    AudioObjectID *device_id);
static OSStatus driver_destroy_device(
    AudioServerPlugInDriverRef driver,
    AudioObjectID device_id);
static OSStatus driver_add_client(
    AudioServerPlugInDriverRef driver,
    AudioObjectID device_id,
    const AudioServerPlugInClientInfo *client);
static OSStatus driver_remove_client(
    AudioServerPlugInDriverRef driver,
    AudioObjectID device_id,
    const AudioServerPlugInClientInfo *client);
static OSStatus driver_perform_change(
    AudioServerPlugInDriverRef driver,
    AudioObjectID device_id,
    UInt64 action,
    void *change_info);
static OSStatus driver_abort_change(
    AudioServerPlugInDriverRef driver,
    AudioObjectID device_id,
    UInt64 action,
    void *change_info);
static Boolean driver_has_property(
    AudioServerPlugInDriverRef driver,
    AudioObjectID object_id,
    pid_t client_pid,
    const AudioObjectPropertyAddress *address);
static OSStatus driver_is_property_settable(
    AudioServerPlugInDriverRef driver,
    AudioObjectID object_id,
    pid_t client_pid,
    const AudioObjectPropertyAddress *address,
    Boolean *settable);
static OSStatus driver_get_property_size(
    AudioServerPlugInDriverRef driver,
    AudioObjectID object_id,
    pid_t client_pid,
    const AudioObjectPropertyAddress *address,
    UInt32 qualifier_size,
    const void *qualifier,
    UInt32 *size);
static OSStatus driver_get_property(
    AudioServerPlugInDriverRef driver,
    AudioObjectID object_id,
    pid_t client_pid,
    const AudioObjectPropertyAddress *address,
    UInt32 qualifier_size,
    const void *qualifier,
    UInt32 data_size,
    UInt32 *used_size,
    void *data);
static OSStatus driver_set_property(
    AudioServerPlugInDriverRef driver,
    AudioObjectID object_id,
    pid_t client_pid,
    const AudioObjectPropertyAddress *address,
    UInt32 qualifier_size,
    const void *qualifier,
    UInt32 data_size,
    const void *data);
static OSStatus driver_start_io(
    AudioServerPlugInDriverRef driver,
    AudioObjectID device_id,
    UInt32 client_id);
static OSStatus driver_stop_io(
    AudioServerPlugInDriverRef driver,
    AudioObjectID device_id,
    UInt32 client_id);
static OSStatus driver_zero_timestamp(
    AudioServerPlugInDriverRef driver,
    AudioObjectID device_id,
    UInt32 client_id,
    Float64 *sample_time,
    UInt64 *host_time,
    UInt64 *seed);
static OSStatus driver_will_do_io(
    AudioServerPlugInDriverRef driver,
    AudioObjectID device_id,
    UInt32 client_id,
    UInt32 operation_id,
    Boolean *will_do,
    Boolean *in_place);
static OSStatus driver_begin_io(
    AudioServerPlugInDriverRef driver,
    AudioObjectID device_id,
    UInt32 client_id,
    UInt32 operation_id,
    UInt32 frame_count,
    const AudioServerPlugInIOCycleInfo *cycle);
static OSStatus driver_do_io(
    AudioServerPlugInDriverRef driver,
    AudioObjectID device_id,
    AudioObjectID stream_id,
    UInt32 client_id,
    UInt32 operation_id,
    UInt32 frame_count,
    const AudioServerPlugInIOCycleInfo *cycle,
    void *main_buffer,
    void *secondary_buffer);
static OSStatus driver_end_io(
    AudioServerPlugInDriverRef driver,
    AudioObjectID device_id,
    UInt32 client_id,
    UInt32 operation_id,
    UInt32 frame_count,
    const AudioServerPlugInIOCycleInfo *cycle);

static AudioServerPlugInDriverInterface g_driver_interface = {
    NULL,
    driver_query_interface,
    driver_add_ref,
    driver_release,
    driver_initialize,
    driver_create_device,
    driver_destroy_device,
    driver_add_client,
    driver_remove_client,
    driver_perform_change,
    driver_abort_change,
    driver_has_property,
    driver_is_property_settable,
    driver_get_property_size,
    driver_get_property,
    driver_set_property,
    driver_start_io,
    driver_stop_io,
    driver_zero_timestamp,
    driver_will_do_io,
    driver_begin_io,
    driver_do_io,
    driver_end_io,
};
static AudioServerPlugInDriverInterface *g_driver_interface_pointer =
    &g_driver_interface;
static AudioServerPlugInDriverRef g_driver = &g_driver_interface_pointer;
static CardputerAudioDevice g_device;
static _Atomic ULONG g_reference_count = 1;
static _Atomic Boolean g_stream_active = true;
static _Atomic UInt64 g_anchor_host_time = 0;
static _Atomic UInt64 g_timeline_seed = 1;
static double g_host_ticks_per_frame = 0.0;

__attribute__((visibility("default"))) void *CardputerAudioDriverFactory(
    CFAllocatorRef allocator,
    CFUUIDRef requested_type) {
  UNUSED(allocator);
  return CFEqual(requested_type, kAudioServerPlugInTypeUUID) ? g_driver : NULL;
}

static bool valid_driver(AudioServerPlugInDriverRef driver) {
  return driver == g_driver;
}

static bool valid_object(AudioObjectID object_id) {
  return object_id == kAudioObjectPlugInObject ||
         object_id == kCardputerAudioObjectDevice ||
         object_id == kCardputerAudioObjectInputStream;
}

static HRESULT driver_query_interface(
    void *driver,
    REFIID uuid,
    LPVOID *out_interface) {
  if (driver != g_driver || out_interface == NULL) {
    return E_POINTER;
  }
  CFUUIDRef requested = CFUUIDCreateFromUUIDBytes(NULL, uuid);
  if (requested == NULL) {
    return E_NOINTERFACE;
  }
  const bool supported =
      CFEqual(requested, IUnknownUUID) ||
      CFEqual(requested, kAudioServerPlugInDriverInterfaceUUID);
  CFRelease(requested);
  if (!supported) {
    *out_interface = NULL;
    return E_NOINTERFACE;
  }
  driver_add_ref(driver);
  *out_interface = g_driver;
  return S_OK;
}

static ULONG driver_add_ref(void *driver) {
  if (driver != g_driver) {
    return 0;
  }
  ULONG current =
      atomic_load_explicit(&g_reference_count, memory_order_relaxed);
  while (current != UINT32_MAX &&
         !atomic_compare_exchange_weak_explicit(
             &g_reference_count,
             &current,
             current + 1,
             memory_order_relaxed,
             memory_order_relaxed)) {
  }
  return current == UINT32_MAX ? current : current + 1;
}

static ULONG driver_release(void *driver) {
  if (driver != g_driver) {
    return 0;
  }
  ULONG current =
      atomic_load_explicit(&g_reference_count, memory_order_relaxed);
  while (current > 1 &&
         !atomic_compare_exchange_weak_explicit(
             &g_reference_count,
             &current,
             current - 1,
             memory_order_relaxed,
             memory_order_relaxed)) {
  }
  return current > 1 ? current - 1 : current;
}

static OSStatus driver_initialize(
    AudioServerPlugInDriverRef driver,
    AudioServerPlugInHostRef host) {
  if (!valid_driver(driver) || host == NULL) {
    return kAudioHardwareBadObjectError;
  }
  cardputer_audio_device_initialize(&g_device);
  mach_timebase_info_data_t timebase = {0};
  if (mach_timebase_info(&timebase) != KERN_SUCCESS ||
      timebase.numer == 0) {
    return kAudioHardwareUnspecifiedError;
  }
  const double ticks_per_second =
      1000000000.0 * (double)timebase.denom / (double)timebase.numer;
  g_host_ticks_per_frame = ticks_per_second / 48000.0;
  atomic_store_explicit(
      &g_anchor_host_time, mach_absolute_time(), memory_order_release);
  (void)cardputer_audio_ipc_server_start(&g_device);
  return noErr;
}

static OSStatus driver_create_device(
    AudioServerPlugInDriverRef driver,
    CFDictionaryRef description,
    const AudioServerPlugInClientInfo *client,
    AudioObjectID *device_id) {
  UNUSED(description);
  UNUSED(client);
  UNUSED(device_id);
  return valid_driver(driver) ? kAudioHardwareUnsupportedOperationError
                              : kAudioHardwareBadObjectError;
}

static OSStatus driver_destroy_device(
    AudioServerPlugInDriverRef driver,
    AudioObjectID device_id) {
  UNUSED(device_id);
  return valid_driver(driver) ? kAudioHardwareUnsupportedOperationError
                              : kAudioHardwareBadObjectError;
}

static OSStatus driver_add_client(
    AudioServerPlugInDriverRef driver,
    AudioObjectID device_id,
    const AudioServerPlugInClientInfo *client) {
  UNUSED(client);
  return valid_driver(driver) &&
                 device_id == kCardputerAudioObjectDevice
             ? noErr
             : kAudioHardwareBadObjectError;
}

static OSStatus driver_remove_client(
    AudioServerPlugInDriverRef driver,
    AudioObjectID device_id,
    const AudioServerPlugInClientInfo *client) {
  return driver_add_client(driver, device_id, client);
}

static OSStatus driver_perform_change(
    AudioServerPlugInDriverRef driver,
    AudioObjectID device_id,
    UInt64 action,
    void *change_info) {
  UNUSED(action);
  UNUSED(change_info);
  return valid_driver(driver) &&
                 device_id == kCardputerAudioObjectDevice
             ? noErr
             : kAudioHardwareBadObjectError;
}

static OSStatus driver_abort_change(
    AudioServerPlugInDriverRef driver,
    AudioObjectID device_id,
    UInt64 action,
    void *change_info) {
  return driver_perform_change(driver, device_id, action, change_info);
}

static UInt32 stream_configuration_size(
    const AudioObjectPropertyAddress *address) {
  return address->mScope == kAudioObjectPropertyScopeOutput
             ? (UInt32)offsetof(AudioBufferList, mBuffers)
             : (UInt32)sizeof(AudioBufferList);
}

static UInt32 property_size(
    AudioObjectID object_id,
    const AudioObjectPropertyAddress *address) {
  if (address == NULL || !valid_object(object_id)) {
    return UINT32_MAX;
  }
  switch (object_id) {
    case kAudioObjectPlugInObject:
      switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioPlugInPropertyDeviceList:
        case kAudioPlugInPropertyTranslateUIDToDevice:
          return sizeof(AudioObjectID);
        case kAudioObjectPropertyManufacturer:
        case kAudioPlugInPropertyResourceBundle:
          return sizeof(CFStringRef);
        case kAudioObjectPropertyOwnedObjects:
          return sizeof(AudioObjectID);
        default:
          return UINT32_MAX;
      }
    case kCardputerAudioObjectDevice:
      switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioDevicePropertyTransportType:
        case kAudioDevicePropertyClockDomain:
        case kAudioDevicePropertyDeviceIsAlive:
        case kAudioDevicePropertyDeviceIsRunning:
        case kAudioDevicePropertyDeviceCanBeDefaultDevice:
        case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
        case kAudioDevicePropertyLatency:
        case kAudioDevicePropertySafetyOffset:
        case kAudioDevicePropertyIsHidden:
        case kAudioDevicePropertyZeroTimeStampPeriod:
        case kAudioDevicePropertyBufferFrameSize:
          return sizeof(UInt32);
        case kAudioObjectPropertyName:
        case kAudioObjectPropertyModelName:
        case kAudioObjectPropertyManufacturer:
        case kAudioDevicePropertyDeviceUID:
        case kAudioDevicePropertyModelUID:
          return sizeof(CFStringRef);
        case kAudioObjectPropertyOwnedObjects:
        case kAudioDevicePropertyRelatedDevices:
          return sizeof(AudioObjectID);
        case kAudioDevicePropertyStreams:
          return address->mScope == kAudioObjectPropertyScopeOutput
                     ? 0
                     : sizeof(AudioObjectID);
        case kAudioObjectPropertyControlList:
          return 0;
        case kAudioDevicePropertyNominalSampleRate:
        case kAudioDevicePropertyActualSampleRate:
          return sizeof(Float64);
        case kAudioDevicePropertyAvailableNominalSampleRates:
        case kAudioDevicePropertyBufferFrameSizeRange:
          return sizeof(AudioValueRange);
        case kAudioDevicePropertyStreamConfiguration:
          return stream_configuration_size(address);
        default:
          return UINT32_MAX;
      }
    case kCardputerAudioObjectInputStream:
      switch (address->mSelector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass:
        case kAudioObjectPropertyOwner:
        case kAudioStreamPropertyIsActive:
        case kAudioStreamPropertyDirection:
        case kAudioStreamPropertyTerminalType:
        case kAudioStreamPropertyStartingChannel:
        case kAudioStreamPropertyLatency:
          return sizeof(UInt32);
        case kAudioObjectPropertyName:
          return sizeof(CFStringRef);
        case kAudioObjectPropertyOwnedObjects:
          return 0;
        case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat:
          return sizeof(AudioStreamBasicDescription);
        case kAudioStreamPropertyAvailableVirtualFormats:
        case kAudioStreamPropertyAvailablePhysicalFormats:
          return sizeof(AudioStreamRangedDescription);
        default:
          return UINT32_MAX;
      }
    default:
      return UINT32_MAX;
  }
}

static Boolean driver_has_property(
    AudioServerPlugInDriverRef driver,
    AudioObjectID object_id,
    pid_t client_pid,
    const AudioObjectPropertyAddress *address) {
  UNUSED(client_pid);
  return valid_driver(driver) &&
         property_size(object_id, address) != UINT32_MAX;
}

static OSStatus driver_is_property_settable(
    AudioServerPlugInDriverRef driver,
    AudioObjectID object_id,
    pid_t client_pid,
    const AudioObjectPropertyAddress *address,
    Boolean *settable) {
  UNUSED(client_pid);
  if (!valid_driver(driver) || !valid_object(object_id)) {
    return kAudioHardwareBadObjectError;
  }
  if (address == NULL || settable == NULL) {
    return kAudioHardwareIllegalOperationError;
  }
  if (property_size(object_id, address) == UINT32_MAX) {
    return kAudioHardwareUnknownPropertyError;
  }
  *settable =
      object_id == kCardputerAudioObjectInputStream &&
      address->mSelector == kAudioStreamPropertyIsActive;
  return noErr;
}

static OSStatus driver_get_property_size(
    AudioServerPlugInDriverRef driver,
    AudioObjectID object_id,
    pid_t client_pid,
    const AudioObjectPropertyAddress *address,
    UInt32 qualifier_size,
    const void *qualifier,
    UInt32 *size) {
  UNUSED(client_pid);
  UNUSED(qualifier_size);
  UNUSED(qualifier);
  if (!valid_driver(driver) || !valid_object(object_id)) {
    return kAudioHardwareBadObjectError;
  }
  if (address == NULL || size == NULL) {
    return kAudioHardwareIllegalOperationError;
  }
  *size = property_size(object_id, address);
  return *size == UINT32_MAX ? kAudioHardwareUnknownPropertyError : noErr;
}

static OSStatus write_bytes(
    const void *source,
    UInt32 source_size,
    UInt32 data_size,
    UInt32 *used_size,
    void *data) {
  if (used_size == NULL || data == NULL) {
    return kAudioHardwareIllegalOperationError;
  }
  if (data_size < source_size) {
    return kAudioHardwareBadPropertySizeError;
  }
  if (source_size > 0) {
    memcpy(data, source, source_size);
  }
  *used_size = source_size;
  return noErr;
}

static AudioStreamBasicDescription mono_float_format(void) {
  AudioStreamBasicDescription format = {0};
  format.mSampleRate = 48000.0;
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags =
      kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian |
      kAudioFormatFlagIsPacked;
  format.mBytesPerPacket = sizeof(float);
  format.mFramesPerPacket = 1;
  format.mBytesPerFrame = sizeof(float);
  format.mChannelsPerFrame = 1;
  format.mBitsPerChannel = 32;
  return format;
}

static OSStatus write_cf_string(
    CFStringRef value,
    UInt32 data_size,
    UInt32 *used_size,
    void *data) {
  return write_bytes(
      &value, sizeof(value), data_size, used_size, data);
}

static OSStatus get_plugin_property(
    const AudioObjectPropertyAddress *address,
    UInt32 qualifier_size,
    const void *qualifier,
    UInt32 data_size,
    UInt32 *used_size,
    void *data) {
  AudioObjectID object = kAudioObjectUnknown;
  switch (address->mSelector) {
    case kAudioObjectPropertyBaseClass: {
      const AudioClassID value = kAudioObjectClassID;
      return write_bytes(
          &value, sizeof(value), data_size, used_size, data);
    }
    case kAudioObjectPropertyClass: {
      const AudioClassID value = kAudioPlugInClassID;
      return write_bytes(
          &value, sizeof(value), data_size, used_size, data);
    }
    case kAudioObjectPropertyOwner:
      return write_bytes(
          &object, sizeof(object), data_size, used_size, data);
    case kAudioObjectPropertyManufacturer:
      return write_cf_string(
          CFSTR("Lynx"), data_size, used_size, data);
    case kAudioObjectPropertyOwnedObjects:
    case kAudioPlugInPropertyDeviceList:
      object = kCardputerAudioObjectDevice;
      return write_bytes(
          &object, sizeof(object), data_size, used_size, data);
    case kAudioPlugInPropertyTranslateUIDToDevice:
      if (qualifier_size != sizeof(CFStringRef) || qualifier == NULL) {
        return kAudioHardwareBadPropertySizeError;
      }
      if (CFEqual(
              *(const CFStringRef *)qualifier,
              CFSTR(CARDPUTER_DEVICE_UID))) {
        object = kCardputerAudioObjectDevice;
      }
      return write_bytes(
          &object, sizeof(object), data_size, used_size, data);
    case kAudioPlugInPropertyResourceBundle:
      return write_cf_string(CFSTR(""), data_size, used_size, data);
    default:
      return kAudioHardwareUnknownPropertyError;
  }
}

static OSStatus get_device_property(
    const AudioObjectPropertyAddress *address,
    UInt32 data_size,
    UInt32 *used_size,
    void *data) {
  UInt32 value32 = 0;
  switch (address->mSelector) {
    case kAudioObjectPropertyBaseClass:
      value32 = kAudioObjectClassID;
      break;
    case kAudioObjectPropertyClass:
      value32 = kAudioDeviceClassID;
      break;
    case kAudioObjectPropertyOwner:
      value32 = kAudioObjectPlugInObject;
      break;
    case kAudioDevicePropertyTransportType:
      value32 = kAudioDeviceTransportTypeVirtual;
      break;
    case kAudioDevicePropertyClockDomain:
      value32 = 0;
      break;
    case kAudioDevicePropertyDeviceIsAlive:
      value32 = 1;
      break;
    case kAudioDevicePropertyDeviceIsRunning:
      value32 = cardputer_audio_device_client_count(&g_device) > 0;
      break;
    case kAudioDevicePropertyDeviceCanBeDefaultDevice:
    case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
      value32 =
          address->mScope != kAudioObjectPropertyScopeOutput;
      break;
    case kAudioDevicePropertyLatency:
    case kAudioDevicePropertySafetyOffset:
    case kAudioDevicePropertyIsHidden:
      value32 = 0;
      break;
    case kAudioDevicePropertyZeroTimeStampPeriod:
      value32 = CARDPUTER_ZERO_TIMESTAMP_PERIOD;
      break;
    case kAudioDevicePropertyBufferFrameSize:
      value32 = CARDPUTER_ZERO_TIMESTAMP_PERIOD;
      break;
    case kAudioObjectPropertyName:
      return write_cf_string(
          CFSTR("Cardputer Codex Microphone"),
          data_size,
          used_size,
          data);
    case kAudioObjectPropertyModelName:
      return write_cf_string(
          CFSTR("Cardputer Codex Microphone"),
          data_size,
          used_size,
          data);
    case kAudioObjectPropertyManufacturer:
      return write_cf_string(
          CFSTR("Lynx"), data_size, used_size, data);
    case kAudioDevicePropertyDeviceUID:
      return write_cf_string(
          CFSTR(CARDPUTER_DEVICE_UID), data_size, used_size, data);
    case kAudioDevicePropertyModelUID:
      return write_cf_string(
          CFSTR(CARDPUTER_MODEL_UID), data_size, used_size, data);
    case kAudioObjectPropertyOwnedObjects:
    case kAudioDevicePropertyRelatedDevices:
    case kAudioDevicePropertyStreams: {
      if (address->mScope == kAudioObjectPropertyScopeOutput &&
          address->mSelector == kAudioDevicePropertyStreams) {
        *used_size = 0;
        return noErr;
      }
      const AudioObjectID object =
          address->mSelector == kAudioDevicePropertyRelatedDevices
              ? kCardputerAudioObjectDevice
              : kCardputerAudioObjectInputStream;
      return write_bytes(
          &object, sizeof(object), data_size, used_size, data);
    }
    case kAudioObjectPropertyControlList:
      *used_size = 0;
      return noErr;
    case kAudioDevicePropertyNominalSampleRate:
    case kAudioDevicePropertyActualSampleRate: {
      const Float64 rate = 48000.0;
      return write_bytes(
          &rate, sizeof(rate), data_size, used_size, data);
    }
    case kAudioDevicePropertyAvailableNominalSampleRates: {
      const AudioValueRange range = {48000.0, 48000.0};
      return write_bytes(
          &range, sizeof(range), data_size, used_size, data);
    }
    case kAudioDevicePropertyBufferFrameSizeRange: {
      const AudioValueRange range = {64.0, 4096.0};
      return write_bytes(
          &range, sizeof(range), data_size, used_size, data);
    }
    case kAudioDevicePropertyStreamConfiguration: {
      const UInt32 required = stream_configuration_size(address);
      if (data_size < required) {
        return kAudioHardwareBadPropertySizeError;
      }
      AudioBufferList *buffers = data;
      buffers->mNumberBuffers =
          address->mScope == kAudioObjectPropertyScopeOutput ? 0 : 1;
      if (buffers->mNumberBuffers == 1) {
        buffers->mBuffers[0].mNumberChannels = 1;
        buffers->mBuffers[0].mDataByteSize = 0;
        buffers->mBuffers[0].mData = NULL;
      }
      *used_size = required;
      return noErr;
    }
    default:
      return kAudioHardwareUnknownPropertyError;
  }
  return write_bytes(
      &value32, sizeof(value32), data_size, used_size, data);
}

static OSStatus get_stream_property(
    const AudioObjectPropertyAddress *address,
    UInt32 data_size,
    UInt32 *used_size,
    void *data) {
  UInt32 value32 = 0;
  switch (address->mSelector) {
    case kAudioObjectPropertyBaseClass:
      value32 = kAudioObjectClassID;
      break;
    case kAudioObjectPropertyClass:
      value32 = kAudioStreamClassID;
      break;
    case kAudioObjectPropertyOwner:
      value32 = kCardputerAudioObjectDevice;
      break;
    case kAudioStreamPropertyIsActive:
      value32 =
          atomic_load_explicit(&g_stream_active, memory_order_acquire);
      break;
    case kAudioStreamPropertyDirection:
      value32 = 1;
      break;
    case kAudioStreamPropertyTerminalType:
      value32 = kAudioStreamTerminalTypeMicrophone;
      break;
    case kAudioStreamPropertyStartingChannel:
      value32 = 1;
      break;
    case kAudioStreamPropertyLatency:
      value32 = 0;
      break;
    case kAudioObjectPropertyName:
      return write_cf_string(
          CFSTR("Cardputer Codex Microphone Input"),
          data_size,
          used_size,
          data);
    case kAudioObjectPropertyOwnedObjects:
      *used_size = 0;
      return noErr;
    case kAudioStreamPropertyVirtualFormat:
    case kAudioStreamPropertyPhysicalFormat: {
      const AudioStreamBasicDescription format = mono_float_format();
      return write_bytes(
          &format, sizeof(format), data_size, used_size, data);
    }
    case kAudioStreamPropertyAvailableVirtualFormats:
    case kAudioStreamPropertyAvailablePhysicalFormats: {
      AudioStreamRangedDescription description = {
          .mFormat = mono_float_format(),
          .mSampleRateRange = {48000.0, 48000.0},
      };
      return write_bytes(
          &description,
          sizeof(description),
          data_size,
          used_size,
          data);
    }
    default:
      return kAudioHardwareUnknownPropertyError;
  }
  return write_bytes(
      &value32, sizeof(value32), data_size, used_size, data);
}

static OSStatus driver_get_property(
    AudioServerPlugInDriverRef driver,
    AudioObjectID object_id,
    pid_t client_pid,
    const AudioObjectPropertyAddress *address,
    UInt32 qualifier_size,
    const void *qualifier,
    UInt32 data_size,
    UInt32 *used_size,
    void *data) {
  UNUSED(client_pid);
  if (!valid_driver(driver) || !valid_object(object_id)) {
    return kAudioHardwareBadObjectError;
  }
  if (address == NULL || used_size == NULL || data == NULL) {
    return kAudioHardwareIllegalOperationError;
  }
  switch (object_id) {
    case kAudioObjectPlugInObject:
      return get_plugin_property(
          address,
          qualifier_size,
          qualifier,
          data_size,
          used_size,
          data);
    case kCardputerAudioObjectDevice:
      return get_device_property(address, data_size, used_size, data);
    case kCardputerAudioObjectInputStream:
      return get_stream_property(address, data_size, used_size, data);
    default:
      return kAudioHardwareBadObjectError;
  }
}

static OSStatus driver_set_property(
    AudioServerPlugInDriverRef driver,
    AudioObjectID object_id,
    pid_t client_pid,
    const AudioObjectPropertyAddress *address,
    UInt32 qualifier_size,
    const void *qualifier,
    UInt32 data_size,
    const void *data) {
  UNUSED(client_pid);
  UNUSED(qualifier_size);
  UNUSED(qualifier);
  if (!valid_driver(driver) || !valid_object(object_id)) {
    return kAudioHardwareBadObjectError;
  }
  if (address == NULL || data == NULL) {
    return kAudioHardwareIllegalOperationError;
  }
  if (object_id != kCardputerAudioObjectInputStream ||
      address->mSelector != kAudioStreamPropertyIsActive) {
    return kAudioHardwareUnknownPropertyError;
  }
  if (data_size != sizeof(UInt32)) {
    return kAudioHardwareBadPropertySizeError;
  }
  const Boolean active = *(const UInt32 *)data != 0;
  atomic_store_explicit(
      &g_stream_active, active, memory_order_release);
  return noErr;
}

static OSStatus driver_start_io(
    AudioServerPlugInDriverRef driver,
    AudioObjectID device_id,
    UInt32 client_id) {
  UNUSED(client_id);
  if (!valid_driver(driver) ||
      device_id != kCardputerAudioObjectDevice) {
    return kAudioHardwareBadObjectError;
  }
  if (cardputer_audio_device_client_count(&g_device) == 0) {
    atomic_store_explicit(
        &g_anchor_host_time, mach_absolute_time(), memory_order_release);
    atomic_fetch_add_explicit(
        &g_timeline_seed, 1, memory_order_relaxed);
  }
  cardputer_audio_device_start(&g_device);
  return noErr;
}

static OSStatus driver_stop_io(
    AudioServerPlugInDriverRef driver,
    AudioObjectID device_id,
    UInt32 client_id) {
  UNUSED(client_id);
  if (!valid_driver(driver) ||
      device_id != kCardputerAudioObjectDevice) {
    return kAudioHardwareBadObjectError;
  }
  cardputer_audio_device_stop(&g_device);
  return noErr;
}

static OSStatus driver_zero_timestamp(
    AudioServerPlugInDriverRef driver,
    AudioObjectID device_id,
    UInt32 client_id,
    Float64 *sample_time,
    UInt64 *host_time,
    UInt64 *seed) {
  UNUSED(client_id);
  if (!valid_driver(driver) ||
      device_id != kCardputerAudioObjectDevice) {
    return kAudioHardwareBadObjectError;
  }
  if (sample_time == NULL || host_time == NULL || seed == NULL ||
      g_host_ticks_per_frame <= 0.0) {
    return kAudioHardwareIllegalOperationError;
  }
  const UInt64 anchor =
      atomic_load_explicit(&g_anchor_host_time, memory_order_acquire);
  const UInt64 now = mach_absolute_time();
  const double elapsed_frames =
      (double)(now - anchor) / g_host_ticks_per_frame;
  const UInt64 frame =
      ((UInt64)elapsed_frames / CARDPUTER_ZERO_TIMESTAMP_PERIOD) *
      CARDPUTER_ZERO_TIMESTAMP_PERIOD;
  *sample_time = (Float64)frame;
  *host_time =
      anchor + (UInt64)((double)frame * g_host_ticks_per_frame);
  *seed = atomic_load_explicit(&g_timeline_seed, memory_order_acquire);
  return noErr;
}

static OSStatus driver_will_do_io(
    AudioServerPlugInDriverRef driver,
    AudioObjectID device_id,
    UInt32 client_id,
    UInt32 operation_id,
    Boolean *will_do,
    Boolean *in_place) {
  UNUSED(client_id);
  if (!valid_driver(driver) ||
      device_id != kCardputerAudioObjectDevice) {
    return kAudioHardwareBadObjectError;
  }
  if (will_do == NULL || in_place == NULL) {
    return kAudioHardwareIllegalOperationError;
  }
  *will_do = operation_id == kAudioServerPlugInIOOperationReadInput;
  *in_place = true;
  return noErr;
}

static OSStatus driver_begin_io(
    AudioServerPlugInDriverRef driver,
    AudioObjectID device_id,
    UInt32 client_id,
    UInt32 operation_id,
    UInt32 frame_count,
    const AudioServerPlugInIOCycleInfo *cycle) {
  UNUSED(client_id);
  UNUSED(operation_id);
  UNUSED(frame_count);
  UNUSED(cycle);
  return valid_driver(driver) &&
                 device_id == kCardputerAudioObjectDevice
             ? noErr
             : kAudioHardwareBadObjectError;
}

static OSStatus driver_do_io(
    AudioServerPlugInDriverRef driver,
    AudioObjectID device_id,
    AudioObjectID stream_id,
    UInt32 client_id,
    UInt32 operation_id,
    UInt32 frame_count,
    const AudioServerPlugInIOCycleInfo *cycle,
    void *main_buffer,
    void *secondary_buffer) {
  UNUSED(client_id);
  UNUSED(cycle);
  UNUSED(secondary_buffer);
  if (!valid_driver(driver) ||
      device_id != kCardputerAudioObjectDevice ||
      stream_id != kCardputerAudioObjectInputStream) {
    return kAudioHardwareBadObjectError;
  }
  if (operation_id != kAudioServerPlugInIOOperationReadInput ||
      main_buffer == NULL) {
    return kAudioHardwareUnsupportedOperationError;
  }
  cardputer_audio_device_render(&g_device, main_buffer, frame_count);
  return noErr;
}

static OSStatus driver_end_io(
    AudioServerPlugInDriverRef driver,
    AudioObjectID device_id,
    UInt32 client_id,
    UInt32 operation_id,
    UInt32 frame_count,
    const AudioServerPlugInIOCycleInfo *cycle) {
  return driver_begin_io(
      driver,
      device_id,
      client_id,
      operation_id,
      frame_count,
      cycle);
}
