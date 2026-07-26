#include <cassert>

#include "product/device_identity.hpp"

int main() {
  static_assert(kDeviceCertificateBufferBytes == 640);
  static_assert(kDevicePrivateKeyBufferBytes == 320);
  assert(kDeviceCertificateBufferBytes + kDevicePrivateKeyBufferBytes == 960);
  return 0;
}
