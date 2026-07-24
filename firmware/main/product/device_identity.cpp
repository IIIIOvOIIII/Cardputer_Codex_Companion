#include "product/device_identity.hpp"

#ifdef ESP_PLATFORM

#include <array>
#include <cstring>
#include <span>

#include "esp_random.h"
#include "mbedtls/ecp.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"
#include "nvs.h"

namespace {
constexpr char kNamespace[] = "product_tls";
constexpr char kCertificateKey[] = "certificate";
constexpr char kPrivateKeyKey[] = "private_key";
std::array<uint8_t, 2048> g_certificate{};
std::array<uint8_t, 512> g_private_key{};

int random_bytes(void*, unsigned char* output, std::size_t length) {
  esp_fill_random(output, length);
  return 0;
}

bool load_pem(nvs_handle_t handle, const char* key,
              std::span<uint8_t> output, std::size_t* length) {
  std::size_t required = output.size();
  if (nvs_get_blob(handle, key, output.data(), &required) != ESP_OK ||
      required < 2 || required > output.size() ||
      output[required - 1] != '\0') {
    return false;
  }
  *length = required;
  return true;
}

esp_err_t generate_identity(std::size_t* certificate_length,
                            std::size_t* private_key_length) {
  mbedtls_pk_context key;
  mbedtls_pk_init(&key);
  mbedtls_x509write_cert certificate;
  mbedtls_x509write_crt_init(&certificate);
  esp_err_t result = ESP_FAIL;
  std::array<uint8_t, 16> serial{};

  const mbedtls_pk_info_t* key_info =
      mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY);
  if (key_info == nullptr || mbedtls_pk_setup(&key, key_info) != 0) {
    goto cleanup;
  }
  if (mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key),
                          random_bytes, nullptr) != 0) {
    goto cleanup;
  }

  esp_fill_random(serial.data(), serial.size());
  serial[0] &= 0x7f;
  serial[0] |= 0x01;

  mbedtls_x509write_crt_set_version(
      &certificate, MBEDTLS_X509_CRT_VERSION_3);
  mbedtls_x509write_crt_set_md_alg(&certificate, MBEDTLS_MD_SHA256);
  mbedtls_x509write_crt_set_subject_key(&certificate, &key);
  mbedtls_x509write_crt_set_issuer_key(&certificate, &key);
  if (mbedtls_x509write_crt_set_subject_name(
          &certificate, "CN=Cardputer Codex Companion,O=Lynx") != 0 ||
      mbedtls_x509write_crt_set_issuer_name(
          &certificate, "CN=Cardputer Codex Companion,O=Lynx") != 0 ||
      mbedtls_x509write_crt_set_serial_raw(
          &certificate, serial.data(), serial.size()) != 0 ||
      mbedtls_x509write_crt_set_validity(
          &certificate, "20240101000000", "20491231235959") != 0 ||
      mbedtls_x509write_crt_set_basic_constraints(
          &certificate, 0, -1) != 0 ||
      mbedtls_x509write_crt_set_key_usage(
          &certificate, MBEDTLS_X509_KU_DIGITAL_SIGNATURE) != 0 ||
      mbedtls_x509write_crt_pem(
          &certificate, g_certificate.data(), g_certificate.size(),
          random_bytes, nullptr) != 0 ||
      mbedtls_pk_write_key_pem(
          &key, g_private_key.data(), g_private_key.size()) != 0) {
    goto cleanup;
  }
  *certificate_length =
      std::strlen(reinterpret_cast<const char*>(g_certificate.data())) + 1;
  *private_key_length =
      std::strlen(reinterpret_cast<const char*>(g_private_key.data())) + 1;
  result = ESP_OK;

cleanup:
  mbedtls_x509write_crt_free(&certificate);
  mbedtls_pk_free(&key);
  return result;
}
}  // namespace

esp_err_t load_or_create_device_tls_identity(DeviceTlsIdentity* identity) {
  if (identity == nullptr) return ESP_ERR_INVALID_ARG;
  nvs_handle_t handle = 0;
  esp_err_t result =
      nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (result != ESP_OK) return result;

  std::size_t certificate_length = 0;
  std::size_t private_key_length = 0;
  if (!load_pem(handle, kCertificateKey, g_certificate,
                &certificate_length) ||
      !load_pem(handle, kPrivateKeyKey, g_private_key,
                &private_key_length)) {
    g_certificate.fill(0);
    g_private_key.fill(0);
    result = generate_identity(&certificate_length, &private_key_length);
    if (result == ESP_OK) {
      result = nvs_set_blob(handle, kCertificateKey, g_certificate.data(),
                            certificate_length);
    }
    if (result == ESP_OK) {
      result = nvs_set_blob(handle, kPrivateKeyKey, g_private_key.data(),
                            private_key_length);
    }
    if (result == ESP_OK) result = nvs_commit(handle);
  }
  nvs_close(handle);
  if (result != ESP_OK) return result;

  *identity = {
      .certificate = g_certificate.data(),
      .certificate_length = certificate_length,
      .private_key = g_private_key.data(),
      .private_key_length = private_key_length,
  };
  return ESP_OK;
}

#endif
