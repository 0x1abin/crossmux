#include "SleepCover.h"

#include <Bitmap.h>
#include <HalStorage.h>
#include <Logging.h>

#include "CrossPointSettings.h"

namespace sleepCover {
namespace {
constexpr char transactionPath[] = "/sleep.bmp.txn";
constexpr uint8_t transactionVersion = 1;

bool validImage(const char* path) {
  HalFile file;
  if (!Storage.openFileForRead("SLP", path, file)) return false;
  Bitmap bitmap(file, false);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) return false;
  return file.position() + static_cast<uint64_t>(bitmap.getRowBytes()) * bitmap.getHeight() <= file.fileSize64();
}

bool removeIfPresent(const char* path) { return !Storage.exists(path) || Storage.remove(path); }

bool restore(uint8_t previousMode, bool hadPrevious) {
  if (Storage.exists(backupPath)) {
    if (!validImage(backupPath) || !removeIfPresent(imagePath) || !Storage.rename(backupPath, imagePath)) return false;
  } else if (hadPrevious) {
    if (!Storage.exists(imagePath)) return false;
  } else if (!removeIfPresent(imagePath)) {
    return false;
  }
  SETTINGS.sleepScreen = previousMode;
  if (!SETTINGS.saveToFile()) return false;
  return removeIfPresent(partPath) && removeIfPresent(transactionPath);
}
}  // namespace

bool recover() {
  if (Storage.exists(transactionPath)) {
    uint8_t state[3];
    {
      HalFile file;
      if (!Storage.openFileForRead("SLP", transactionPath, file) || file.fileSize64() != sizeof(state) ||
          file.read(state, sizeof(state)) != sizeof(state) || state[0] != transactionVersion ||
          state[1] >= CrossPointSettings::SLEEP_SCREEN_MODE_COUNT || state[2] > 1) {
        LOG_ERR("SLP", "Invalid sleep-cover transaction; preserving recovery files");
        return false;
      }
    }
    if (!restore(state[1], state[2] != 0)) {
      LOG_ERR("SLP", "Could not restore sleep cover; preserving transaction and backup");
      return false;
    }
    return true;
  }
  if (!removeIfPresent(partPath)) return false;
  if (!Storage.exists(backupPath)) return true;
  const bool custom =
      SETTINGS.sleepScreen == CrossPointSettings::CUSTOM || SETTINGS.sleepScreen == CrossPointSettings::TRANSPARENT;
  if (custom && validImage(imagePath)) {
    // Legacy .bak or a commit interrupted after removing its transaction marker.
    return SETTINGS.saveToFile() && removeIfPresent(backupPath);
  }
  if (!validImage(backupPath) || !removeIfPresent(imagePath) || !Storage.rename(backupPath, imagePath)) {
    LOG_ERR("SLP", "Could not recover previous sleep cover");
    return false;
  }
  return true;
}

bool commit(const uint8_t mode) {
  if ((mode != CrossPointSettings::CUSTOM && mode != CrossPointSettings::TRANSPARENT) ||
      Storage.exists(transactionPath) || Storage.exists(backupPath) || !validImage(partPath))
    return false;
  const uint8_t previousMode = SETTINGS.sleepScreen;
  const bool hadPrevious = Storage.exists(imagePath);
  const uint8_t state[] = {transactionVersion, previousMode, static_cast<uint8_t>(hadPrevious)};
  bool saved = false;
  {
    HalFile file;
    saved = Storage.openFileForWrite("SLP", transactionPath, file) && file.write(state, sizeof(state)) == sizeof(state);
    if (saved) {
      file.flush();
      saved = file.close();
    }
  }
  if (!saved) {
    removeIfPresent(transactionPath);
    return false;
  }
  if ((hadPrevious && !Storage.rename(imagePath, backupPath)) || !Storage.rename(partPath, imagePath) ||
      !validImage(imagePath)) {
    if (!restore(previousMode, hadPrevious))
      LOG_ERR("SLP", "Sleep-cover rollback incomplete; preserving recovery files");
    return false;
  }
  SETTINGS.sleepScreen = mode;
  if (!SETTINGS.saveToFile() || !Storage.remove(transactionPath)) {
    if (!restore(previousMode, hadPrevious))
      LOG_ERR("SLP", "Sleep-cover rollback incomplete; preserving recovery files");
    return false;
  }
  // The image and settings are durable; a failed backup removal can be retried by recover().
  if (!removeIfPresent(backupPath)) LOG_ERR("SLP", "Sleep-cover committed; backup cleanup pending");
  return true;
}
}  // namespace sleepCover
