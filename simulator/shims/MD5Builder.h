#pragma once
// Arduino-ESP32 MD5Builder shape for host code. Native builds use OpenSSL's
// fixed-size MD5 context so protocol signatures and shard checks are real.

#include <Stream.h>
#include <WString.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#ifdef CROSSPOINT_SIM_REAL_MD5
#include <openssl/md5.h>
#endif

class MD5Builder {
 public:
  void begin() {
    std::memset(buf_, 0, sizeof(buf_));
#ifdef CROSSPOINT_SIM_REAL_MD5
    active_ = MD5_Init(&context_) == 1;
#endif
  }

  void add(const uint8_t* data, size_t len) {
#ifdef CROSSPOINT_SIM_REAL_MD5
    if (active_ && data && len > 0) active_ = MD5_Update(&context_, data, len) == 1;
#else
    (void)data;
    (void)len;
#endif
  }

  void add(const char* data) {
    if (data) add(reinterpret_cast<const uint8_t*>(data), std::strlen(data));
  }

  void add(const String& data) { add(reinterpret_cast<const uint8_t*>(data.c_str()), data.length()); }

  bool addStream(Stream& stream, size_t maxLen) {
#ifdef CROSSPOINT_SIM_REAL_MD5
    uint8_t chunk[512];
    while (active_ && maxLen > 0 && stream.available() > 0) {
      const size_t wanted = std::min(maxLen, sizeof(chunk));
      size_t got = 0;
      while (got < wanted) {
        const int value = stream.read();
        if (value < 0) break;
        chunk[got++] = static_cast<uint8_t>(value);
      }
      if (got == 0) return false;
      add(chunk, got);
      maxLen -= got;
    }
    return active_;
#else
    (void)stream;
    (void)maxLen;
    return true;
#endif
  }

  void calculate() {
#ifdef CROSSPOINT_SIM_REAL_MD5
    if (active_ && MD5_Final(buf_, &context_) != 1) std::memset(buf_, 0, sizeof(buf_));
    active_ = false;
#endif
  }

  String toString() {
    char out[33];
    for (size_t i = 0; i < sizeof(buf_); ++i) std::snprintf(out + i * 2, 3, "%02x", buf_[i]);
    return String(out);
  }

  void getBytes(uint8_t* out) {
    if (out) std::memcpy(out, buf_, sizeof(buf_));
  }

 private:
  uint8_t buf_[16] = {};
#ifdef CROSSPOINT_SIM_REAL_MD5
  MD5_CTX context_ = {};
  bool active_ = false;
#endif
};
