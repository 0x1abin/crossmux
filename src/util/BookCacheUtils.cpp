#include "BookCacheUtils.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <Logging.h>
#include <Memory.h>
#include <Txt.h>
#include <Xtc.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>

#include "ClippingStore.h"
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
  if (FsHelpers::hasReflowableBookExtension(path)) {
    cleared = Epub(path, "/.crosspoint").clearCache();
  } else if (FsHelpers::hasXtcExtension(path)) {
    cleared = Xtc(path, "/.crosspoint").clearCache();

  } else {
    return true;
  }
  if (!cleared) return false;
  LOG_DBG("BookCache", "Done checking metadata cache for: %s", path.c_str());
  return true;
}

std::string bookCachePath(const std::string& path) {
  if (FsHelpers::hasReflowableBookExtension(path)) return Epub(path, "/.crosspoint").getCachePath();
  if (FsHelpers::hasXtcExtension(path)) return Xtc(path, "/.crosspoint").getCachePath();
  return {};
}

namespace {
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

enum class MovePhase { Check, Move, Rollback };
struct MoveDirectory {
  HalFile file;
  std::string from;
  std::string to;
};
// shortcut: directory traversal is bounded to 32 levels and 1024-byte paths, extend only with a measured RAM budget.
constexpr size_t MAX_MOVE_DEPTH = 32;

struct BookArtifacts {
  std::array<std::string, 9> paths;
  static constexpr size_t CACHE = 4;
  static constexpr size_t LEGACY_CACHE = 5;
};
BookArtifacts artifactsFor(const std::string& path) {
  const bool txt = FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path);
  const std::string bookmark = FsHelpers::hasEpubExtension(path) ? BookmarkUtil::getBookmarkPath(path) : "";
  return {{{path + ".key", path + ".key.tmp", path + ".rights", path + ".rights.tmp", bookCachePath(path),
            txt ? Txt(path, "/.crosspoint").getCachePath() : "", bookmark, bookmark.empty() ? "" : bookmark + ".tmp",
            bookmark.empty() ? "" : bookmark + ".bak"}}};
}
std::string backupBookPath(const std::string& path, const char* operation) {
  const std::string folder = FsHelpers::extractFolderPath(path);
  const auto dot = path.find_last_of('.');
  const auto slash = path.find_last_of('/');
  const std::string extension =
      dot != std::string::npos && (slash == std::string::npos || dot > slash) && path.size() - dot <= 16
          ? path.substr(dot)
          : ".bin";
  return folder + (folder.back() == '/' ? "" : "/") + operation + std::to_string(std::hash<std::string>{}(path)) +
         extension;
}
bool cacheContainsUserData(const std::string& path) {
  if (path.empty()) return false;
  constexpr std::array<const char*, 5> userFiles = {"progress.bin", "progress.bin.tmp", "progress.bin.bak", "links.bin",
                                                    "cover.override"};
  return std::any_of(userFiles.begin(), userFiles.end(),
                     [&path](const char* name) { return Storage.exists((path + "/" + name).c_str()); });
}
bool removeArtifact(const std::string& path) {
  if (path.empty() || !Storage.exists(path.c_str())) return true;
  bool directory;
  {
    HalFile file = Storage.open(path.c_str());
    if (!file) return false;
    directory = file.isDirectory();
  }
  return directory ? Storage.removeDir(path.c_str()) : Storage.remove(path.c_str());
}

bool moveArtifacts(const std::string& from, const std::string& to, MovePhase phase, bool allowCacheFailure,
                   bool& incomplete, bool sidecarsInTree) {
  const auto source = artifactsFor(from);
  const auto target = artifactsFor(to);
  if (phase == MovePhase::Check) {
    for (size_t i = sidecarsInTree ? 4 : 0; i < source.paths.size(); ++i) {
      if (!target.paths[i].empty() && source.paths[i] != target.paths[i] && Storage.exists(target.paths[i].c_str()))
        return false;
    }
    return ClippingStore::canRelocateForFilePath(from, to);
  }
  if (phase == MovePhase::Rollback) {
    bool ok = ClippingStore::relocateForFilePath(to, from);
    for (size_t i = source.paths.size(); i > 0;) {
      --i;
      if ((!sidecarsInTree || i >= 4) && !source.paths[i].empty() && !target.paths[i].empty() &&
          !renameIfTargetMissing(target.paths[i], source.paths[i]))
        ok = false;
    }
    return ok;
  }
  uint16_t moved = 0;
  for (size_t i = sidecarsInTree ? 4 : 0; i < source.paths.size(); ++i) {
    if (source.paths[i].empty() || source.paths[i] == target.paths[i] || !Storage.exists(source.paths[i].c_str()))
      continue;
    if (Storage.rename(source.paths[i].c_str(), target.paths[i].c_str())) {
      moved |= 1U << i;
      continue;
    }
    if (allowCacheFailure && (i == BookArtifacts::CACHE || i == BookArtifacts::LEGACY_CACHE) &&
        !cacheContainsUserData(source.paths[i])) {
      incomplete = true;
      LOG_ERR("BookCache", "Rebuildable cache migration failed: %s", from.c_str());
      continue;
    }
    for (size_t j = i; j > 0;) {
      --j;
      if ((moved & (1U << j)) && !renameIfTargetMissing(target.paths[j], source.paths[j]))
        LOG_ERR("BookCache", "Artifact rollback failed; retaining data at %s", target.paths[j].c_str());
    }
    return false;
  }
  if (ClippingStore::relocateForFilePath(from, to)) return true;
  for (size_t i = source.paths.size(); i > 0;) {
    --i;
    if ((moved & (1U << i)) && !renameIfTargetMissing(target.paths[i], source.paths[i]))
      LOG_ERR("BookCache", "Artifact rollback failed; retaining data at %s", target.paths[i].c_str());
  }
  return false;
}

template <typename Visit>
bool visitBooks(const std::string& tree, const std::string& from, const std::string& to, MoveDirectory* stack,
                char* name, Visit&& visit, bool keepGoing = false) {
  for (size_t i = 0; i < MAX_MOVE_DEPTH; ++i)
    if (stack && stack[i].file) stack[i].file.close();
  HalFile root = Storage.open(tree.c_str());
  if (!root) return false;
  if (!root.isDirectory()) return visit(from, to);
  if (!stack || !name) return false;
  stack[0] = {std::move(root), from, to};
  size_t depth = 1;
  bool ok = true;
  while (depth) {
    auto& directory = stack[depth - 1];
    HalFile entry = directory.file.openNextFile();
    if (!entry) {
      directory.file.close();
      --depth;
      continue;
    }
    const size_t length = entry.getName(name, 1024);
    if (length == 0 || length >= 1024) return false;
    if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
    const std::string oldPath = directory.from + (directory.from.back() == '/' ? "" : "/") + name;
    const std::string newPath = directory.to + (directory.to.back() == '/' ? "" : "/") + name;
    if (oldPath.size() >= 1024 || newPath.size() >= 1024) return false;
    if (entry.isDirectory()) {
      if (depth == MAX_MOVE_DEPTH) return false;
      stack[depth++] = {std::move(entry), oldPath, newPath};
    } else if (!visit(oldPath, newPath)) {
      ok = false;
      if (!keepGoing) return false;
    }
  }
  return ok;
}

bool moveBundle(const std::string& from, const std::string& to, MoveDirectory* stack, char* name,
                bool allowCacheFailure, bool& incomplete) {
  const auto phase = [&](MovePhase phase) {
    return visitBooks(
        from, from, to, stack, name,
        [&](const auto& oldPath, const auto& newPath) {
          return moveArtifacts(oldPath, newPath, phase, allowCacheFailure, incomplete, stack != nullptr);
        },
        phase == MovePhase::Rollback);
  };
  if (!phase(MovePhase::Check)) return false;
  if (phase(MovePhase::Move) && Storage.rename(from.c_str(), to.c_str())) return true;
  if (!phase(MovePhase::Rollback)) LOG_ERR("BookCache", "Book rollback incomplete: %s -> %s", from.c_str(), to.c_str());
  return false;
}

bool removeBundle(const std::string& path, MoveDirectory* stack, char* name) {
  bool ok = visitBooks(
      path, path, path, stack, name,
      [stack](const auto& from, const auto&) {
        bool removed = true;
        const auto artifacts = artifactsFor(from);
        for (size_t i = stack ? 4 : 0; i < artifacts.paths.size(); ++i)
          if (!removeArtifact(artifacts.paths[i])) removed = false;
        if (!ClippingStore::deleteForFilePath(from, "epub")) removed = false;
        return removed;
      },
      true);
  // Retain the backing path when cleanup fails, so failed deletes cannot strand a same-name move.
  if (ok) ok = removeArtifact(path);
  return ok;
}
}  // namespace

BookMoveResult moveBookFile(const std::string& oldPath, const std::string& newPath, const bool overwrite) {
  if (oldPath == newPath) return BookMoveResult::Complete;
  if (oldPath.empty() || newPath.empty() || oldPath == "/" || newPath == "/" || oldPath.size() >= 1024 ||
      newPath.size() >= 1024 || FsHelpers::isSameOrDescendantPath(newPath, oldPath) ||
      FsHelpers::isSameOrDescendantPath(oldPath, newPath))
    return BookMoveResult::NotMoved;
  const std::string backup = backupBookPath(newPath, ".crossmux-move-");
  const bool targetExists = Storage.exists(newPath.c_str());
  if ((targetExists && !overwrite) || backup.size() >= 1024 || Storage.exists(backup.c_str()))
    return BookMoveResult::NotMoved;
  bool directory;
  {
    HalFile source = Storage.open(oldPath.c_str());
    if (!source) return BookMoveResult::NotMoved;
    directory = source.isDirectory();
    if (targetExists) {
      HalFile target = Storage.open(newPath.c_str());
      if (!target || target.isDirectory() != directory) return BookMoveResult::NotMoved;
    }
  }
  // Reuse the former clipping traversal's checked 32-frame/1024-byte workspace across every phase.
  auto stack = directory ? makeUniqueNoThrow<MoveDirectory[]>(MAX_MOVE_DEPTH) : nullptr;
  auto name = directory ? makeUniqueNoThrow<char[]>(1024) : nullptr;
  if (directory && (!stack || !name)) {
    LOG_ERR("BookCache", "OOM: book folder transaction workspace");
    return BookMoveResult::NotMoved;
  }
  bool incomplete = false;
  if (targetExists && !moveBundle(newPath, backup, stack.get(), name.get(), false, incomplete))
    return BookMoveResult::NotMoved;
  if (!moveBundle(oldPath, newPath, stack.get(), name.get(), true, incomplete)) {
    if (targetExists && !moveBundle(backup, newPath, stack.get(), name.get(), false, incomplete))
      LOG_ERR("BookCache", "Destination rollback failed; backup retained at %s", backup.c_str());
    return BookMoveResult::NotMoved;
  }
  if (targetExists) {
    // Replace destination identities only after the backing path committed.
    if (!visitBooks(
            backup, newPath, newPath, stack.get(), name.get(),
            [&](const auto& target, const auto&) {
              RECENT_BOOKS.removeByPath(target);
              const auto* stats = READING_STATS.findBook(target);
              const auto source = directory ? FsHelpers::rebasePath(target, newPath, oldPath) : oldPath;
              return !stats || stats == READING_STATS.findBook(source) || READING_STATS.removeBook(target);
            },
            true))
      incomplete = true;
    if (!removeBundle(backup, stack.get(), name.get())) {
      incomplete = true;
      LOG_ERR("BookCache", "Move committed; destination backup cleanup pending: %s", backup.c_str());
    }
  }
  if (!visitBooks(
          newPath, oldPath, newPath, stack.get(), name.get(),
          [](const auto& from, const auto& to) { return relocateBookReferences(from, to); }, true))
    incomplete = true;
  if (directory) {
    if (!RECENT_BOOKS.updatePathPrefix(oldPath, newPath)) incomplete = true;
    if (!READING_STATS.updateBookPathPrefix(oldPath, newPath)) incomplete = true;
    if (FsHelpers::isSameOrDescendantPath(APP_STATE.openEpubPath, oldPath)) {
      APP_STATE.openEpubPath = FsHelpers::rebasePath(APP_STATE.openEpubPath, oldPath, newPath);
      if (!APP_STATE.saveToFile()) incomplete = true;
    }
  }
  return incomplete ? BookMoveResult::MovedDataIncomplete : BookMoveResult::Complete;
}

bool removeBookFile(const std::string& path) {
  if (path.empty() || path == "/" || path.size() >= 1024) return false;
  const std::string backup = backupBookPath(path, ".crossmux-delete-");
  if (backup.size() >= 1024 || Storage.exists(backup.c_str())) {
    LOG_ERR("BookCache", "Deletion backup exists: %s", backup.c_str());
    return false;
  }
  bool directory;
  {
    HalFile file = Storage.open(path.c_str());
    if (!file) return false;
    directory = file.isDirectory();
  }
  auto stack = directory ? makeUniqueNoThrow<MoveDirectory[]>(MAX_MOVE_DEPTH) : nullptr;
  auto name = directory ? makeUniqueNoThrow<char[]>(1024) : nullptr;
  if (directory && (!stack || !name)) {
    LOG_ERR("BookCache", "OOM: book deletion workspace");
    return false;
  }
  bool incomplete = false;
  if (!moveBundle(path, backup, stack.get(), name.get(), false, incomplete)) return false;
  // The original path and all saved data are now retired together; failed cleanup retains the private backup.
  if (!visitBooks(
          backup, path, path, stack.get(), name.get(),
          [](const auto& book, const auto&) {
            RECENT_BOOKS.removeByPath(book);
            return true;
          },
          true))
    LOG_ERR("BookCache", "Deletion committed; recent-book cleanup incomplete");
  if (FsHelpers::isSameOrDescendantPath(APP_STATE.openEpubPath, path)) {
    APP_STATE.openEpubPath.clear();
    if (!APP_STATE.saveToFile()) LOG_ERR("BookCache", "Deletion committed; open-book persistence failed");
  }
  if (!removeBundle(backup, stack.get(), name.get()))
    LOG_ERR("BookCache", "Deletion committed; backup retained at %s", backup.c_str());
  return true;
}
