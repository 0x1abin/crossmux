#include "VoiceNotesStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace voicenotes {
namespace {

// Parses "RECnnnn.wav" (case-insensitive extension). Returns -1 otherwise.
int recordingNumber(const char* fileName) {
  if (strncmp(fileName, "REC", 3) != 0) return -1;
  int number = 0;
  for (int i = 3; i < 7; ++i) {
    if (fileName[i] < '0' || fileName[i] > '9') return -1;
    number = number * 10 + (fileName[i] - '0');
  }
  if (strcasecmp(fileName + 7, ".wav") != 0) return -1;
  return number;
}

}  // namespace

void wavPath(const char* name, char* out, const size_t outLen) { snprintf(out, outLen, "%s/%s.wav", DIR, name); }

void txtPath(const char* name, char* out, const size_t outLen) { snprintf(out, outLen, "%s/%s.txt", DIR, name); }

uint32_t durationSeconds(const uint32_t wavBytes) {
  return wavBytes > WAV_HEADER_BYTES ? (wavBytes - WAV_HEADER_BYTES) / BYTES_PER_SECOND : 0;
}

bool list(std::vector<Recording>& out) {
  out.clear();
  HalFile dir = Storage.open(DIR);
  // A missing folder just means nothing has been recorded yet.
  if (!dir || !dir.isDirectory()) return !Storage.exists(DIR);
  // 16 bytes per entry; typical collections fit without regrowth.
  out.reserve(32);

  char fileName[64];
  dir.rewindDirectory();
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (file.isDirectory()) continue;
    file.getName(fileName, sizeof(fileName));
    if (recordingNumber(fileName) < 0) continue;
    if (out.size() >= MAX_LISTED) {
      LOG_INF("VNS", "More than %u recordings; listing the first %u", static_cast<unsigned>(MAX_LISTED),
              static_cast<unsigned>(MAX_LISTED));
      break;
    }
    Recording rec{};
    memcpy(rec.name, fileName, NAME_LEN - 1);
    rec.name[NAME_LEN - 1] = '\0';
    rec.bytes = static_cast<uint32_t>(file.fileSize());
    out.push_back(rec);
  }

  // Zero-padded numbers sort lexically; newest (highest) first.
  std::sort(out.begin(), out.end(), [](const Recording& a, const Recording& b) { return strcmp(a.name, b.name) > 0; });
  char path[PATH_LEN];
  for (auto& rec : out) {
    txtPath(rec.name, path, sizeof(path));
    rec.hasTranscript = Storage.exists(path);
  }
  return true;
}

bool nextWavPath(char* out, const size_t outLen) {
  if (!Storage.ensureDirectoryExists(DIR)) {
    LOG_ERR("VNS", "Cannot create %s", DIR);
    return false;
  }
  int highest = 0;
  HalFile dir = Storage.open(DIR);
  if (dir && dir.isDirectory()) {
    char fileName[64];
    dir.rewindDirectory();
    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      file.getName(fileName, sizeof(fileName));
      highest = std::max(highest, recordingNumber(fileName));
    }
  }
  if (highest >= 9999) {
    LOG_ERR("VNS", "Recording numbers exhausted");
    return false;
  }
  char name[NAME_LEN];
  snprintf(name, sizeof(name), "REC%04d", highest + 1);
  wavPath(name, out, outLen);
  return true;
}

bool remove(const char* name) {
  char path[PATH_LEN];
  txtPath(name, path, sizeof(path));
  if (Storage.exists(path)) Storage.remove(path);
  wavPath(name, path, sizeof(path));
  return Storage.remove(path);
}

}  // namespace voicenotes
