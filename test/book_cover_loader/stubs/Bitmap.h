#pragma once

#include <HalStorage.h>

enum class BmpReaderError { Ok, NotBMP };

class Bitmap {
 public:
  explicit Bitmap(HalFile& file) : file(file) {}

  BmpReaderError parseHeaders() {
    if (!file.seek(0)) return BmpReaderError::NotBMP;
    const int first = file.read();
    const int second = file.read();
    valid = first == 'B' && second == 'M';
    return valid ? BmpReaderError::Ok : BmpReaderError::NotBMP;
  }

  int getWidth() const { return valid ? 1 : 0; }
  int getHeight() const { return valid ? 1 : 0; }

 private:
  HalFile& file;
  bool valid = false;
};
