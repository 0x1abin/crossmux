#pragma once

#include <Stream.h>
#include <esp_system.h>

#include <cstdio>

// Real Arduino-ESP32 makes these globally available through the HardwareSerial / esp32-hal
// include chain. Mirror that so consumer .cpp files that only `#include <HardwareSerial.h>`
// (or transitively through <Logging.h>) still get the basic Arduino runtime symbols.
unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void delayMicroseconds(unsigned long us);

class HardwareSerial : public Stream {
 public:
  explicit HardwareSerial(std::FILE* sink = nullptr);

  void begin(unsigned long baud);
  void begin(unsigned long baud, uint32_t config, int8_t rxPin = -1, int8_t txPin = -1);
  void end();
  // USB-CDC TX timeout knob on real ESP32 (HWCDC); a no-op on host since writes go to a FILE*.
  void setTxTimeoutMs(uint32_t) {}
  void setSink(std::FILE* sink) { sink_ = sink; }

  // Print overrides
  size_t write(uint8_t b) override;
  size_t write(const uint8_t* buffer, size_t size) override;
  void flush() override;

  // Stream overrides
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }

  operator bool() const { return sink_ != nullptr; }

 private:
  std::FILE* sink_ = nullptr;
};

// On real ESP32-S2/S3/C3 boards `Serial` is a USB CDC instance whose type alias is `HWCDC`.
// Some firmware references `HWCDC&` directly; alias it to HardwareSerial on host.
using HWCDC = HardwareSerial;

extern HardwareSerial Serial;
