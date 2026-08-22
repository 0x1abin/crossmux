#include "BookCacheUtils.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <Logging.h>
#include <Txt.h>
#include <Xtc.h>

#include "CrossPointState.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "util/BookmarkUtil.h"

namespace {
bool renameIfTargetMissing(const std::string& oldPath, const std::string& newPath) {
  if (oldPath == newPath || !Storage.exists(oldPath.c_str())) return true;
  if (Storage.exists(newPath.c_str())) return false;
  return Storage.rename(oldPath.c_str(), newPath.c_str());
}

std::string remapCacheAsset(const std::string& assetPath, const std::string& oldCachePath,
                            const std::string& newCachePath) {
  if (oldCachePath.empty() || assetPath.rfind(oldCachePath, 0) != 0) return assetPath;
  return newCachePath + assetPath.substr(oldCachePath.size());
}
}  // namespace

bool isBookCacheDirectoryName(const char* name) {
  if (!name) {
    return false;
  }

  constexpr char EPUB_PREFIX[] = "epub_";
  constexpr char TXT_PREFIX[] = "txt_";
  constexpr char XTC_PREFIX[] = "xtc_";

  return strncmp(name, EPUB_PREFIX, std::size(EPUB_PREFIX) - 1) == 0 ||
         strncmp(name, TXT_PREFIX, std::size(TXT_PREFIX) - 1) == 0 ||
         strncmp(name, XTC_PREFIX, std::size(XTC_PREFIX) - 1) == 0;
}

bool clearBookCache(const std::string& path) {
  bool cleared = true;
  if (FsHelpers::hasEpubExtension(path)) {
    cleared = Epub(path, "/.crosspoint").clearCache();
  } else if (FsHelpers::hasXtcExtension(path)) {
    cleared = Xtc(path, "/.crosspoint").clearCache();
  } else if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) {
    cleared = Txt(path, "/.crosspoint").clearCache();
  } else {
    return true;
  }
  if (!cleared) return false;
  LOG_DBG("BookCache", "Done checking metadata cache for: %s", path.c_str());
  return true;
}

std::string bookCachePath(const std::string& path) {
  if (FsHelpers::hasEpubExtension(path)) return Epub(path, "/.crosspoint").getCachePath();
  if (FsHelpers::hasXtcExtension(path)) return Xtc(path, "/.crosspoint").getCachePath();
  if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) {
    return Txt(path, "/.crosspoint").getCachePath();
  }
  return {};
}

bool relocateBookArtifacts(const std::string& oldPath, const std::string& newPath) {
  bool ok = true;
  const std::string oldCachePath = bookCachePath(oldPath);
  const std::string newCachePath = bookCachePath(newPath);
  if (!oldCachePath.empty() && oldCachePath != newCachePath && Storage.exists(oldCachePath.c_str())) {
    if (Storage.exists(newCachePath.c_str()) || !Storage.rename(oldCachePath.c_str(), newCachePath.c_str())) ok = false;
  }

  if (FsHelpers::hasEpubExtension(oldPath)) {
    const std::string oldBookmarkPath = BookmarkUtil::getBookmarkPath(oldPath);
    const std::string newBookmarkPath = BookmarkUtil::getBookmarkPath(newPath);
    if (!renameIfTargetMissing(oldBookmarkPath, newBookmarkPath)) ok = false;
  }
  return ok;
}

bool relocateBookReferences(const std::string& oldPath, const std::string& newPath) {
  bool ok = true;
  const std::string oldCachePath = bookCachePath(oldPath);
  const std::string newCachePath = bookCachePath(newPath);
  if (!RECENT_BOOKS.updatePath(oldPath, newPath, oldCachePath, newCachePath)) ok = false;

  if (const ReadingBookStats* stats = READING_STATS.findBook(oldPath)) {
    const std::string newCoverPath = remapCacheAsset(stats->coverBmpPath, oldCachePath, newCachePath);
    if (!READING_STATS.updateBookPath(oldPath, newPath, "", "", newCoverPath)) ok = false;
  }

  if (APP_STATE.openEpubPath == oldPath) {
    APP_STATE.openEpubPath = newPath;
    if (!APP_STATE.saveToFile()) ok = false;
  }
  return ok;
}
