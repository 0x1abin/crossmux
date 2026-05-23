// HAL storage backend backed by POSIX file I/O. All firmware paths (which look like
// "/.crosspoint/settings.json" — SD-card-rooted) are resolved under the simulator's
// configured sd_root directory (set via --sd-root, default ./simulator/sd_root).

#define HAL_STORAGE_IMPL
#include <HalStorage.h>
#include <Logging.h>

#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern std::string g_simulator_sd_root;  // owned by simulator_main.cpp

namespace {

std::string resolve(const char* path) {
  std::string p(path ? path : "");
  // Strip the leading SD-root '/'; everything is relative to sd_root on host.
  while (!p.empty() && p.front() == '/') p.erase(0, 1);
  std::string out = g_simulator_sd_root;
  while (!out.empty() && out.back() == '/') out.pop_back();
  if (!p.empty()) {
    out += '/';
    out += p;
  }
  return out;
}

std::string basename_of(const std::string& path) {
  auto slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool mkdir_p(const std::string& path) {
  if (path.empty()) return true;
  std::string acc;
  for (size_t i = 0; i <= path.length(); i++) {
    if (i == path.length() || path[i] == '/') {
      if (!acc.empty() && acc != "/") {
        if (::mkdir(acc.c_str(), 0755) != 0 && errno != EEXIST) return false;
      }
    }
    if (i < path.length()) acc += path[i];
  }
  return true;
}

bool is_directory_path(const std::string& path) {
  struct stat st;
  if (::stat(path.c_str(), &st) != 0) return false;
  return S_ISDIR(st.st_mode);
}

bool remove_recursive(const std::string& path) {
  if (is_directory_path(path)) {
    DIR* d = ::opendir(path.c_str());
    if (!d) return false;
    while (auto* entry = ::readdir(d)) {
      if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) continue;
      remove_recursive(path + '/' + entry->d_name);
    }
    ::closedir(d);
    return ::rmdir(path.c_str()) == 0;
  }
  return ::remove(path.c_str()) == 0;
}

}  // namespace

// =============================================================================
// HalFile::Impl
// =============================================================================

class HalFile::Impl {
 public:
  std::FILE* fp = nullptr;  // non-null when open as file
  DIR* dir = nullptr;       // non-null when open as directory
  std::string path;         // resolved host-side path
  std::string name;         // last path component (returned by getName)
  size_t fileSize_ = 0;     // size cached at open time
  bool isDir_ = false;

  ~Impl() { closeAll(); }

  void closeAll() {
    if (fp) {
      std::fclose(fp);
      fp = nullptr;
    }
    if (dir) {
      ::closedir(dir);
      dir = nullptr;
    }
  }
};

HalFile::HalFile() = default;
HalFile::~HalFile() = default;
HalFile::HalFile(std::unique_ptr<Impl> p) : impl(std::move(p)) {}
HalFile::HalFile(HalFile&&) = default;
HalFile& HalFile::operator=(HalFile&&) = default;

HalFile::operator bool() const { return impl && (impl->fp != nullptr || impl->dir != nullptr); }
bool HalFile::isOpen() const { return static_cast<bool>(*this); }

void HalFile::flush() {
  if (impl && impl->fp) std::fflush(impl->fp);
}

size_t HalFile::getName(char* buf, size_t len) {
  if (!impl || !buf || len == 0) return 0;
  size_t n = impl->name.length();
  if (n >= len) n = len - 1;
  std::memcpy(buf, impl->name.data(), n);
  buf[n] = '\0';
  return n;
}

size_t HalFile::size() { return impl ? impl->fileSize_ : 0; }
size_t HalFile::fileSize() { return size(); }
uint64_t HalFile::fileSize64() { return size(); }

bool HalFile::seek(size_t pos) {
  return impl && impl->fp && std::fseek(impl->fp, static_cast<long>(pos), SEEK_SET) == 0;
}
bool HalFile::seek64(uint64_t pos) { return seek(static_cast<size_t>(pos)); }
bool HalFile::seekCur(int64_t offset) {
  return impl && impl->fp && std::fseek(impl->fp, static_cast<long>(offset), SEEK_CUR) == 0;
}
bool HalFile::seekSet(size_t offset) { return seek(offset); }

int HalFile::available() const {
  if (!impl || !impl->fp) return 0;
  long pos = std::ftell(impl->fp);
  if (pos < 0) return 0;
  long rem = static_cast<long>(impl->fileSize_) - pos;
  return rem > 0 ? static_cast<int>(rem) : 0;
}

size_t HalFile::position() const {
  if (!impl || !impl->fp) return 0;
  long pos = std::ftell(impl->fp);
  return pos < 0 ? 0 : static_cast<size_t>(pos);
}

int HalFile::read(void* buf, size_t count) {
  if (!impl || !impl->fp || !buf) return -1;
  return static_cast<int>(std::fread(buf, 1, count, impl->fp));
}

int HalFile::read() {
  if (!impl || !impl->fp) return -1;
  return std::fgetc(impl->fp);
}

size_t HalFile::write(const void* buf, size_t count) {
  if (!impl || !impl->fp || !buf) return 0;
  return std::fwrite(buf, 1, count, impl->fp);
}
size_t HalFile::write(uint8_t b) { return write(&b, 1); }

bool HalFile::rename(const char* newPath) {
  if (!impl || impl->path.empty() || !newPath) return false;
  return ::rename(impl->path.c_str(), resolve(newPath).c_str()) == 0;
}

bool HalFile::isDirectory() const { return impl && impl->isDir_; }

void HalFile::rewindDirectory() {
  if (impl && impl->dir) ::rewinddir(impl->dir);
}

bool HalFile::close() {
  if (impl) impl->closeAll();
  return true;
}

HalFile HalFile::openNextFile() {
  if (!impl || !impl->dir) return HalFile();
  while (auto* entry = ::readdir(impl->dir)) {
    if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) continue;
    auto child = std::make_unique<Impl>();
    child->path = impl->path + '/' + entry->d_name;
    child->name = entry->d_name;
    struct stat st;
    if (::stat(child->path.c_str(), &st) != 0) continue;
    if (S_ISDIR(st.st_mode)) {
      child->isDir_ = true;
      child->dir = ::opendir(child->path.c_str());
      if (!child->dir) continue;
    } else if (S_ISREG(st.st_mode)) {
      child->fp = std::fopen(child->path.c_str(), "rb");
      if (!child->fp) continue;
      child->fileSize_ = static_cast<size_t>(st.st_size);
    } else {
      continue;
    }
    return HalFile(std::move(child));
  }
  return HalFile();
}

// =============================================================================
// HalStorage
// =============================================================================

HalStorage HalStorage::instance;

HalStorage::HalStorage() {
  storageMutex = xSemaphoreCreateMutex();
  assert(storageMutex != nullptr);
}

class HalStorage::StorageLock {
 public:
  StorageLock() { xSemaphoreTake(HalStorage::getInstance().storageMutex, portMAX_DELAY); }
  ~StorageLock() { xSemaphoreGive(HalStorage::getInstance().storageMutex); }
};

bool HalStorage::begin() {
  if (g_simulator_sd_root.empty()) {
    LOG_ERR("STORAGE", "sd_root is empty");
    return false;
  }
  // Create sd_root if missing so first-time users don't have to.
  if (!is_directory_path(g_simulator_sd_root)) {
    if (!mkdir_p(g_simulator_sd_root)) {
      LOG_ERR("STORAGE", "Failed to create sd_root: %s", g_simulator_sd_root.c_str());
      return false;
    }
  }
  initialized = true;
  LOG_INF("STORAGE", "SD mounted at %s", g_simulator_sd_root.c_str());
  return true;
}

bool HalStorage::ready() const { return initialized; }

std::vector<String> HalStorage::listFiles(const char* path, int maxFiles) {
  StorageLock lock;
  std::vector<String> result;
  std::string resolved = resolve(path);
  DIR* d = ::opendir(resolved.c_str());
  if (!d) return result;
  while (auto* entry = ::readdir(d)) {
    if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) continue;
    if (static_cast<int>(result.size()) >= maxFiles) break;
    result.emplace_back(entry->d_name);
  }
  ::closedir(d);
  return result;
}

String HalStorage::readFile(const char* path) {
  StorageLock lock;
  std::string resolved = resolve(path);
  std::FILE* fp = std::fopen(resolved.c_str(), "rb");
  if (!fp) return String();
  std::fseek(fp, 0, SEEK_END);
  long size = std::ftell(fp);
  std::fseek(fp, 0, SEEK_SET);
  if (size < 0) {
    std::fclose(fp);
    return String();
  }
  std::string content(static_cast<size_t>(size), '\0');
  std::fread(content.data(), 1, static_cast<size_t>(size), fp);
  std::fclose(fp);
  return String(content);
}

bool HalStorage::readFileToStream(const char* path, Print& out, size_t chunkSize) {
  StorageLock lock;
  std::string resolved = resolve(path);
  std::FILE* fp = std::fopen(resolved.c_str(), "rb");
  if (!fp) return false;
  std::vector<uint8_t> buf(chunkSize);
  while (true) {
    size_t n = std::fread(buf.data(), 1, chunkSize, fp);
    if (n == 0) break;
    out.write(buf.data(), n);
  }
  std::fclose(fp);
  return true;
}

size_t HalStorage::readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) {
  StorageLock lock;
  if (!buffer || bufferSize == 0) return 0;
  std::string resolved = resolve(path);
  std::FILE* fp = std::fopen(resolved.c_str(), "rb");
  if (!fp) return 0;
  size_t cap = bufferSize - 1;
  size_t toRead = (maxBytes > 0 && maxBytes < cap) ? maxBytes : cap;
  size_t n = std::fread(buffer, 1, toRead, fp);
  std::fclose(fp);
  buffer[n] = '\0';
  return n;
}

bool HalStorage::writeFile(const char* path, const String& content) {
  StorageLock lock;
  std::string resolved = resolve(path);
  auto slash = resolved.find_last_of('/');
  if (slash != std::string::npos) mkdir_p(resolved.substr(0, slash));
  std::FILE* fp = std::fopen(resolved.c_str(), "wb");
  if (!fp) return false;
  size_t written = std::fwrite(content.c_str(), 1, content.length(), fp);
  std::fclose(fp);
  return written == content.length();
}

bool HalStorage::ensureDirectoryExists(const char* path) {
  StorageLock lock;
  return mkdir_p(resolve(path));
}

HalFile HalStorage::open(const char* path, const oflag_t oflag) {
  StorageLock lock;
  if (!path) return HalFile();
  std::string resolved = resolve(path);

  if (is_directory_path(resolved)) {
    auto p = std::make_unique<HalFile::Impl>();
    p->path = resolved;
    p->name = basename_of(resolved);
    p->isDir_ = true;
    p->dir = ::opendir(resolved.c_str());
    if (!p->dir) return HalFile();
    return HalFile(std::move(p));
  }

  const char* mode = "rb";
  const bool wantWrite = (oflag & O_WRONLY) || (oflag & O_RDWR);
  const bool wantCreate = (oflag & O_CREAT);
  const bool wantTrunc = (oflag & O_TRUNC);
  const bool wantAppend = (oflag & O_APPEND);

  if (wantAppend) {
    mode = (oflag & O_RDWR) ? "ab+" : "ab";
  } else if (wantWrite && wantTrunc) {
    mode = (oflag & O_RDWR) ? "wb+" : "wb";
  } else if (oflag & O_RDWR) {
    mode = "rb+";
  } else if (wantWrite) {
    mode = "wb";
  }

  if (wantCreate) {
    auto slash = resolved.find_last_of('/');
    if (slash != std::string::npos) mkdir_p(resolved.substr(0, slash));
  }

  std::FILE* fp = std::fopen(resolved.c_str(), mode);
  if (!fp) return HalFile();

  auto p = std::make_unique<HalFile::Impl>();
  p->fp = fp;
  p->path = resolved;
  p->name = basename_of(resolved);
  struct stat st;
  if (::stat(resolved.c_str(), &st) == 0) p->fileSize_ = static_cast<size_t>(st.st_size);
  return HalFile(std::move(p));
}

bool HalStorage::mkdir(const char* path, bool /*pFlag*/) {
  StorageLock lock;
  return mkdir_p(resolve(path));
}

bool HalStorage::exists(const char* path) {
  StorageLock lock;
  struct stat st;
  return ::stat(resolve(path).c_str(), &st) == 0;
}

bool HalStorage::remove(const char* path) {
  StorageLock lock;
  return ::remove(resolve(path).c_str()) == 0;
}

bool HalStorage::rename(const char* oldPath, const char* newPath) {
  StorageLock lock;
  return ::rename(resolve(oldPath).c_str(), resolve(newPath).c_str()) == 0;
}

bool HalStorage::rmdir(const char* path) {
  StorageLock lock;
  return ::rmdir(resolve(path).c_str()) == 0;
}

namespace {

bool open_with_flag(const char* moduleName, const char* path, oflag_t flag, HalFile& out) {
  out = HalStorage::getInstance().open(path, flag);
  if (!out) {
    LOG_ERR(moduleName, "Failed to open %s", path ? path : "(null)");
    return false;
  }
  return true;
}

}  // namespace

bool HalStorage::openFileForRead(const char* m, const char* p, HalFile& out) {
  return open_with_flag(m, p, O_RDONLY, out);
}
bool HalStorage::openFileForRead(const char* m, const std::string& p, HalFile& out) {
  return open_with_flag(m, p.c_str(), O_RDONLY, out);
}
bool HalStorage::openFileForRead(const char* m, const String& p, HalFile& out) {
  return open_with_flag(m, p.c_str(), O_RDONLY, out);
}
bool HalStorage::openFileForWrite(const char* m, const char* p, HalFile& out) {
  return open_with_flag(m, p, O_WRONLY | O_CREAT | O_TRUNC, out);
}
bool HalStorage::openFileForWrite(const char* m, const std::string& p, HalFile& out) {
  return open_with_flag(m, p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, out);
}
bool HalStorage::openFileForWrite(const char* m, const String& p, HalFile& out) {
  return open_with_flag(m, p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, out);
}

bool HalStorage::removeDir(const char* path) {
  StorageLock lock;
  return remove_recursive(resolve(path));
}
