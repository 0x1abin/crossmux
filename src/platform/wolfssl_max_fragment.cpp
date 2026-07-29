#include <wolfssl/ssl.h>

#include <cstring>

extern "C" {

int __real_wolfSSL_UseSNI(WOLFSSL* ssl, unsigned char type, const void* data, unsigned short size);

int __wrap_wolfSSL_UseSNI(WOLFSSL* ssl, unsigned char type, const void* data, unsigned short size) {
  const int result = __real_wolfSSL_UseSNI(ssl, type, data, size);
  static constexpr char kWeReadHost[] = "weread.qq.com";
  // ponytail: replace this linker hook when SecureClient exposes a maximum-fragment setter.
  if (result == WOLFSSL_SUCCESS && data && type == WOLFSSL_SNI_HOST_NAME && size == sizeof(kWeReadHost) - 1 &&
      memcmp(data, kWeReadHost, sizeof(kWeReadHost) - 1) == 0) {
    wolfSSL_UseMaxFragment(ssl, WOLFSSL_MFL_2_12);
  }
  return result;
}

}  // extern "C"
