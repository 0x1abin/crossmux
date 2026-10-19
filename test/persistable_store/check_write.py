"""Run production JSON writes and recovery against real files and fault injection."""

from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/tests"))
from test_reading_ui_regressions import method


if __name__ == "__main__":
    compiler, arduinojson, simulator = sys.argv[1:]
    store = (ROOT / "lib/Serialization/PersistableStore.cpp").read_text()
    analytics = (ROOT / "src/JsonSettingsIO.cpp").read_text()
    functions = "\n".join([
        method(store, "bool PersistableStoreBase::recoverTempFile("),
        method(store, "bool PersistableStoreBase::writeDocToFile("),
        method(store, "bool PersistableStoreBase::readDocFromFile("),
        method(analytics, "bool saveJsonDocumentToFile("),
        method(analytics, "bool JsonSettingsIO::loadReadingStatsFromFile("),
        method(analytics, "bool JsonSettingsIO::loadAchievementsFromFile("),
    ])
    program = r'''
#include <ArduinoJson.h>
#include <Memory.h>
#include <WString.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#define LOG_ERR(...) ((void)0)
struct HalStorage {
  inline static int lockDepth = 0;
  struct StorageLock {
    StorageLock() { ++lockDepth; }
    ~StorageLock() { --lockDepth; }
  };
};
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
  bool failRename = false;
  bool failTargetRemove = false;
  bool failOpenWrite = false;
  bool failPathAllocation = false;
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
    return !(failTargetRemove && std::string(p).ends_with(".json")) && std::filesystem::remove(resolve(p));
  }
  bool rename(const char* a, const char* b) {
    ++calls;
    return !failRename && !std::filesystem::exists(resolve(b)) &&
           std::rename(resolve(a).c_str(), resolve(b).c_str()) == 0;
  }
  bool openFileForWrite(const char*, const char* p, HalFile& out) {
    assert(HalStorage::lockDepth > 0);
    ++calls;
    if (failOpenWrite) return false;
    out.remaining = writeLimit;
    out.file = std::fopen(resolve(p).c_str(), "wb");
    return out.file;
  }
  String readFile(const char* path) {
    assert(HalStorage::lockDepth > 0);
    ++calls;
    std::ifstream input(resolve(path), std::ios::binary);
    return String(std::string(std::istreambuf_iterator<char>(input), {}));
  }
} Storage;
struct PersistableStoreBase {
  static bool writeDocToFile(const char*, const JsonDocument&);
  static bool recoverTempFile(const char*);
  static bool readDocFromFile(const char*, JsonDocument&);
};
template <typename T> auto allocatePath(size_t count) {
  if (Storage.failPathAllocation) return std::unique_ptr<T>{};
  return makeUniqueNoThrow<T>(count);
}
#define makeUniqueNoThrow allocatePath
struct ReadingStatsStore { int value = -1; };
struct AchievementsStore { int value = -1; };
namespace JsonSettingsIO {
bool loadReadingStatsFromFile(ReadingStatsStore&, const char*);
bool loadAchievementsFromFile(AchievementsStore&, const char*);
// Domain-schema validation is covered by application tests; these isolate file recovery.
template <typename T> bool parseValue(T& store, const char* text) {
  assert(HalStorage::lockDepth == 0);
  JsonDocument doc;
  if (deserializeJson(doc, text)) return false;
  store.value = doc["value"] | -1;
  return true;
}
bool loadReadingStats(ReadingStatsStore& store, const char* text) { return parseValue(store, text); }
bool loadAchievements(AchievementsStore& store, const char* text) { return parseValue(store, text); }
}
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
''' + functions + r'''
int main(int argc, char** argv) {
  assert(argc == 2);
  std::setbuf(stdout, nullptr);
  Storage.root = argv[1];
  std::filesystem::create_directories(Storage.root / ".crosspoint");
  const auto runCases = [](const char* name, auto save) {
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
        assert(!save(path, doc));
        assert(Storage.calls == 0);
        assert(std::filesystem::exists(target) == previous);
        assert(!previous || read(target) == original);
        assert(read(temp) == "stale staging file");
      }
    JsonDocument doc;
    doc["value"] = 22;
    for (const char* invalid : {static_cast<const char*>(nullptr), ""}) {
      Storage.calls = 0;
      assert(!save(invalid, doc));
      assert(Storage.calls == 0);
    }
    for (const size_t limit : {size_t(0), size_t(5)}) {
      {
        std::ofstream old(target);
        old << original;
      }
      Storage.writeLimit = limit;
      const bool saved = save(path, doc);
      std::printf("%s: write limit=%zu saved=%d target=%s\n", name, limit, saved, read(target).c_str());
      assert(!saved);
      assert(read(target) == original && !std::filesystem::exists(temp));
    }
    Storage.writeLimit = SIZE_MAX;
    assert(save(path, doc));
    assert(read(target) == "{\"value\":22}" && !std::filesystem::exists(temp));
    JsonDocument empty;
    empty.to<JsonObject>();
    assert(!empty.overflowed() && save(path, empty));
    assert(read(target) == "{}");
    std::printf("%s: 10 persistence cases passed with ArduinoJson %s\n", name, ARDUINOJSON_VERSION);
  };
  runCases("stores/bookmarks", PersistableStoreBase::writeDocToFile);
  runCases("analytics",
           [](const char* path, const JsonDocument& doc) { return saveJsonDocumentToFile("TEST", path, doc); });
  const auto loadStore = [](const char* path, int& value) {
    JsonDocument doc;
    if (!PersistableStoreBase::readDocFromFile(path, doc)) return false;
    value = doc["value"] | -1;
    return true;
  };
  const auto loadStats = [](const char* path, int& value) {
    ReadingStatsStore store;
    if (!JsonSettingsIO::loadReadingStatsFromFile(store, path)) return false;
    value = store.value;
    return true;
  };
  const auto loadAchievements = [](const char* path, int& value) {
    AchievementsStore store;
    if (!JsonSettingsIO::loadAchievementsFromFile(store, path)) return false;
    value = store.value;
    return true;
  };
  using Save = bool (*)(const char*, const JsonDocument&);
  using Load = bool (*)(const char*, int&);
  struct Mode { const char* name; Save save; Load load; };
  for (const Mode mode : {
           Mode{"stores", PersistableStoreBase::writeDocToFile, loadStore},
           Mode{"statistics", [](const char* p, const JsonDocument& d) {
                  return saveJsonDocumentToFile("TEST", p, d); }, loadStats},
           Mode{"achievements", [](const char* p, const JsonDocument& d) {
                  return saveJsonDocumentToFile("TEST", p, d); }, loadAchievements}}) {
    constexpr const char* path = "/.crosspoint/settings.json";
    const auto target = Storage.resolve(path), temp = Storage.resolve("/.crosspoint/settings.json.tmp");
    const auto seed = [&](const char* current, const char* staged) {
      assert(HalStorage::lockDepth == 0);
      Storage.failRename = Storage.failTargetRemove = Storage.failOpenWrite = Storage.failPathAllocation = false;
      Storage.writeLimit = SIZE_MAX;
      std::filesystem::remove(target);
      std::filesystem::remove(temp);
      if (current) { std::ofstream f(target); f << current; }
      if (staged) { std::ofstream f(temp); f << staged; }
    };
    JsonDocument doc, retry;
    doc["value"] = 22;
    retry["value"] = 33;
    int value = -1;
    seed("{\"value\":12}", nullptr);
    Storage.failRename = true;
    assert(!mode.save(path, doc));
    assert(!std::filesystem::exists(target) && read(temp) == "{\"value\":22}");
    Storage.failRename = false;
    assert(mode.load(path, value) && value == 22 && !std::filesystem::exists(temp));

    seed(nullptr, "{\"value\":22}");
    Storage.failRename = true;
    assert(!mode.save(path, retry) && read(temp) == "{\"value\":22}");
    Storage.failRename = false;
    Storage.writeLimit = 5;
    assert(!mode.save(path, retry) && read(target) == "{\"value\":22}");

    seed(nullptr, nullptr);
    assert(!mode.load(path, value));
    seed("{\"value\":12}", "{\"value\":22}");
    Storage.failPathAllocation = true;
    assert(mode.load(path, value) && value == 12 && read(temp) == "{\"value\":22}");
    seed(nullptr, "{bad");
    assert(!mode.load(path, value));
    seed(nullptr, "{\"value\":22}");
    Storage.failRename = true;
    assert(!mode.load(path, value) && read(temp) == "{\"value\":22}");
    Storage.failRename = false;
    assert(mode.load(path, value) && value == 22);

    seed("{\"value\":12}", nullptr);
    Storage.failTargetRemove = true;
    assert(!mode.save(path, doc) && read(target) == "{\"value\":12}" && read(temp) == "{\"value\":22}");
    assert(mode.load(path, value) && value == 12);
    Storage.failTargetRemove = false;
    assert(mode.save(path, retry) && read(target) == "{\"value\":33}");

    seed(nullptr, "{\"value\":22}");
    Storage.failPathAllocation = true;
    assert(!mode.load(path, value) && read(temp) == "{\"value\":22}");
    Storage.failPathAllocation = false;
    assert(mode.load(path, value) && value == 22);
    seed("{\"value\":12}", nullptr);
    Storage.failOpenWrite = true;
    assert(!mode.save(path, doc) && read(target) == "{\"value\":12}");

    seed(nullptr, "{\"value\":22}");
    GateAllocator allocator;
    allocator.reject = true;
    JsonDocument bad(&allocator);
    bad["value"] = 9;
    assert(bad.overflowed() && !mode.save(path, bad) && read(temp) == "{\"value\":22}");
    assert(mode.load(path, value) && value == 22);
    std::printf("%s: 10 recovery scenarios passed\n", mode.name);
  }
  assert(HalStorage::lockDepth == 0);
}
'''
    with tempfile.TemporaryDirectory(prefix="persistable-write-") as directory:
        root = Path(directory)
        cpp, binary = root / "check.cpp", root / "check"
        cpp.write_text(program)
        for index, flags in enumerate(([], ["-fno-exceptions"])):
            subprocess.run([compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror",
                            *flags, "-DCROSSPOINT_EMULATED=1", "-I" + simulator, "-I" + arduinojson,
                            "-I" + str(ROOT / "lib/Memory"), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary), str(root / str(index))], check=True)
