#pragma once

#include <Logging.h>

#include <cstddef>
#include <cstdint>

#if __has_include(<mbedtls/sha256.h>)
#include <mbedtls/sha256.h>
#else
#include <psa/crypto.h>
#endif

// Stack-owned streaming hash; failed operations stay failed until destruction.
class HalSha256 {
#if __has_include(<mbedtls/sha256.h>)
  mbedtls_sha256_context context;
#else
  psa_hash_operation_t context = PSA_HASH_OPERATION_INIT;
#endif
  bool healthy = false;

 public:
  HalSha256() {
#if __has_include(<mbedtls/sha256.h>)
    mbedtls_sha256_init(&context);
    healthy = mbedtls_sha256_starts(&context, 0) == 0;
#else
    healthy = psa_crypto_init() == PSA_SUCCESS && psa_hash_setup(&context, PSA_ALG_SHA_256) == PSA_SUCCESS;
#endif
    if (!healthy) LOG_ERR("SHA", "SHA256 initialization failed");
  }
  ~HalSha256() {
#if __has_include(<mbedtls/sha256.h>)
    mbedtls_sha256_free(&context);
#else
    psa_hash_abort(&context);
#endif
  }
  HalSha256(const HalSha256&) = delete;
  HalSha256& operator=(const HalSha256&) = delete;

  bool update(const uint8_t* data, size_t length) {
    if (!healthy) return false;
#if __has_include(<mbedtls/sha256.h>)
    healthy = mbedtls_sha256_update(&context, data, length) == 0;
#else
    healthy = psa_hash_update(&context, data, length) == PSA_SUCCESS;
#endif
    if (!healthy) LOG_ERR("SHA", "SHA256 update failed");
    return healthy;
  }
  bool finish(uint8_t out[32]) {
    if (!healthy) return false;
    healthy = false;
#if __has_include(<mbedtls/sha256.h>)
    const bool ok = mbedtls_sha256_finish(&context, out) == 0;
#else
    size_t length = 0;
    const bool ok = psa_hash_finish(&context, out, 32, &length) == PSA_SUCCESS && length == 32;
#endif
    if (!ok) LOG_ERR("SHA", "SHA256 finish failed");
    return ok;
  }
};
