#pragma once
// Minimal StreamString shim. Arduino's StreamString is a Stream that accumulates
// everything written to it; HttpDownloader::fetchUrl(url, std::string&) uses it to
// buffer a response body, then reads it back via c_str(). Host build only needs
// the write-side accumulation plus c_str().
#include <Stream.h>

#include <string>

class StreamString : public Stream {
 public:
  size_t write(uint8_t b) override {
    buf_.push_back(static_cast<char>(b));
    return 1;
  }
  size_t write(const uint8_t* data, size_t size) override {
    buf_.append(reinterpret_cast<const char*>(data), size);
    return size;
  }
  int available() override { return static_cast<int>(buf_.size() - pos_); }
  int read() override { return pos_ < buf_.size() ? static_cast<unsigned char>(buf_[pos_++]) : -1; }
  int peek() override { return pos_ < buf_.size() ? static_cast<unsigned char>(buf_[pos_]) : -1; }
  void flush() override {}

  const char* c_str() const { return buf_.c_str(); }
  size_t length() const { return buf_.size(); }

 private:
  std::string buf_;
  size_t pos_ = 0;
};
