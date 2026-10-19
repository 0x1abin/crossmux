"""Run the production writer with real JSON allocator failures and host files."""

from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/tests"))
from test_reading_ui_regressions import method


if __name__ == "__main__":
    compiler, arduinojson = sys.argv[1:]
    writer = method((ROOT / "lib/Serialization/PersistableStore.cpp").read_text(),
                    "bool PersistableStoreBase::writeDocToFile(")
    program = r'''
#include <ArduinoJson.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#define LOG_ERR(...) ((void)0)
struct HalFile {
  FILE* file = nullptr;
  size_t remaining = SIZE_MAX;
  ~HalFile() {
    if (file) std::fclose(file);
  }
  size_t write(uint8_t value) { return write(&value, 1); }
  size_t write(const uint8_t* values, size_t size) {
    const size_t count = std::min(size, remaining);
    remaining -= count;
    return std::fwrite(values, 1, count, file);
  }
  void flush() { std::fflush(file); }
};
struct StorageProbe {
  std::filesystem::path root;
  unsigned calls = 0;
  size_t writeLimit = SIZE_MAX;
  std::filesystem::path resolve(const char* p) {
    while (*p == '/') ++p;
    return root / p;
  }
  bool mkdir(const char* p) {
    ++calls;
    return std::filesystem::create_directories(resolve(p));
  }
  bool exists(const char* p) {
    ++calls;
    return std::filesystem::exists(resolve(p));
  }
  bool remove(const char* p) {
    ++calls;
    return std::filesystem::remove(resolve(p));
  }
  bool rename(const char* a, const char* b) {
    ++calls;
    return std::rename(resolve(a).c_str(), resolve(b).c_str()) == 0;
  }
  bool openFileForWrite(const char*, const char* p, HalFile& out) {
    ++calls;
    out.remaining = writeLimit;
    out.file = std::fopen(resolve(p).c_str(), "wb");
    return out.file;
  }
} Storage;
struct PersistableStoreBase {
  static bool writeDocToFile(const char*, const JsonDocument&);
};
struct GateAllocator : ArduinoJson::Allocator {
  bool reject = false;
  void* allocate(size_t n) override { return reject ? nullptr : std::malloc(n); }
  void deallocate(void* p) override { std::free(p); }
  void* reallocate(void* p, size_t n) override { return reject ? nullptr : std::realloc(p, n); }
};
std::string read(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}
''' + writer + r'''
int main(int argc, char** argv) {
  assert(argc == 2);
  Storage.root = argv[1];
  std::filesystem::create_directories(Storage.root / ".crosspoint");
  constexpr const char* path = "/.crosspoint/settings.json";
  const auto target = Storage.resolve(path), temp = Storage.resolve("/.crosspoint/settings.json.tmp");
  const std::string original = "{\"value\":12,\"retained\":\"previous valid configuration\"}";
  for (const bool previous : {false, true})
    for (const bool partial : {false, true}) {
      std::filesystem::remove(target);
      if (previous) {
        std::ofstream old(target);
        old << original;
      }
      {
        std::ofstream staging(temp);
        staging << "stale staging file";
      }
      GateAllocator allocator;
      allocator.reject = !partial;
      JsonDocument doc(&allocator);
      doc["value"] = 22;
      if (partial) {
        allocator.reject = true;
        doc["payload"] = std::string(512, 'x');
      }
      assert(doc.overflowed());
      Storage.calls = 0;
      assert(!PersistableStoreBase::writeDocToFile(path, doc));
      assert(Storage.calls == 0);
      assert(std::filesystem::exists(target) == previous);
      assert(!previous || read(target) == original);
      assert(read(temp) == "stale staging file");
    }
  JsonDocument doc;
  doc["value"] = 22;
  for (const char* invalid : {static_cast<const char*>(nullptr), ""}) {
    Storage.calls = 0;
    assert(!PersistableStoreBase::writeDocToFile(invalid, doc));
    assert(Storage.calls == 0);
  }
  for (const size_t limit : {size_t(0), size_t(5)}) {
    {
      std::ofstream old(target);
      old << original;
    }
    Storage.writeLimit = limit;
    assert(!PersistableStoreBase::writeDocToFile(path, doc));
    assert(read(target) == original && !std::filesystem::exists(temp));
  }
  Storage.writeLimit = SIZE_MAX;
  assert(PersistableStoreBase::writeDocToFile(path, doc));
  assert(read(target) == "{\"value\":22}" && !std::filesystem::exists(temp));
  JsonDocument empty;
  empty.to<JsonObject>();
  assert(!empty.overflowed() && PersistableStoreBase::writeDocToFile(path, empty));
  assert(read(target) == "{}");
  std::printf("10 persistence cases passed with ArduinoJson %s\n", ARDUINOJSON_VERSION);
}
'''
    with tempfile.TemporaryDirectory(prefix="persistable-write-") as directory:
        root = Path(directory)
        cpp, binary = root / "check.cpp", root / "check"
        cpp.write_text(program)
        for index, flags in enumerate(([], ["-fno-exceptions"])):
            subprocess.run([compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror",
                            *flags, "-I" + arduinojson, str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary), str(root / str(index))], check=True)
