#include "BookCoverLoader.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Txt.h>
#include <Xtc.h>

namespace BookCoverLoader {
namespace {

enum class CachedCoverState { Missing, Ready, Terminal };

CachedCoverState inspectCachedCover(const std::string& path, const bool emptyIsTerminal) {
  if (!Storage.exists(path.c_str())) return CachedCoverState::Missing;

  {
    HalFile file;
    if (!Storage.openFileForRead("COVER", path, file)) return CachedCoverState::Terminal;
    if (file.fileSize() == 0 && emptyIsTerminal) return CachedCoverState::Terminal;

    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
      // Header-only stubs parse cleanly but have no pixels. BmpHeader is the
      // 1bpp file+DIB+palette prefix those thumbs use; a larger palette still
      // fails the payload check when the file is shorter than its pixels.
      constexpr size_t headerBytes = sizeof(BmpHeader);
      const size_t fileBytes = file.fileSize();
      const size_t pixelBytes = static_cast<size_t>(bitmap.getRowBytes()) * static_cast<size_t>(bitmap.getHeight());
      if (fileBytes > headerBytes && fileBytes >= pixelBytes + headerBytes) {
        return CachedCoverState::Ready;
      }
    }
  }

  if (!Storage.remove(path.c_str())) {
    LOG_ERR("COVER", "Failed to remove invalid cover: %s", path.c_str());
    return CachedCoverState::Terminal;
  }
  return CachedCoverState::Missing;
}

// Empty file + emptyIsTerminal is the existing "do not try again" marker.
void leaveEmptyCoverSentinel(const std::string& path) {
  HalFile file;
  if (!Storage.openFileForWrite("COVER", path, file)) {
    LOG_ERR("COVER", "Failed to leave empty cover sentinel: %s", path.c_str());
  }
}

template <typename Generate>
std::string ensureCachedCover(const std::string& path, const bool emptyIsTerminal, bool* generated,
                              Generate&& generate) {
  if (generated) *generated = false;
  const CachedCoverState state = inspectCachedCover(path, emptyIsTerminal);
  if (state == CachedCoverState::Ready) return path;
  if (state == CachedCoverState::Terminal) return "";
  // inspectCachedCover already deleted an invalid stub, so this generate is the
  // one-shot recovery. A failure, or a result that is still not a full BMP,
  // becomes a 0-byte sentinel so the next ensure* is Terminal.
  if (generate() && inspectCachedCover(path, emptyIsTerminal) == CachedCoverState::Ready) {
    if (generated) *generated = true;
    return path;
  }
  leaveEmptyCoverSentinel(path);
  return "";
}

}  // namespace

std::string ensureThumbnail(const std::string& bookPath, const int height, bool* generated) {
  if (generated) *generated = false;
  if (height <= 0 || !Storage.exists(bookPath.c_str())) return "";

  if (FsHelpers::hasEpubExtension(bookPath)) {
    Epub epub(bookPath, "/.crosspoint");
    const std::string path = epub.getThumbBmpPath(height);
    return ensureCachedCover(path, true, generated, [&]() {
      if (!epub.load(true, true)) return false;
      epub.setupCacheDir();
      return epub.generateThumbBmp(height);
    });
  }

  if (FsHelpers::hasXtcExtension(bookPath)) {
    Xtc xtc(bookPath, "/.crosspoint");
    const std::string path = xtc.getThumbBmpPath(height);
    return ensureCachedCover(path, true, generated, [&]() {
      if (!xtc.load()) return false;
      xtc.setupCacheDir();
      return xtc.generateThumbBmp(height);
    });
  }

  return "";
}

std::string ensureFullCover(const std::string& bookPath, std::string* title, std::string* author, bool* generated) {
  if (title) title->clear();
  if (author) author->clear();
  if (generated) *generated = false;
  if (!Storage.exists(bookPath.c_str())) return "";

  if (FsHelpers::hasEpubExtension(bookPath)) {
    Epub epub(bookPath, "/.crosspoint");
    const std::string path = epub.getCoverBmpPath();
    return ensureCachedCover(path, true, generated, [&]() {
      if (!epub.load(true, true)) return false;
      epub.setupCacheDir();
      if (title) *title = epub.getTitle();
      if (author) *author = epub.getAuthor();
      return epub.generateCoverBmp();
    });
  }

  if (FsHelpers::hasXtcExtension(bookPath)) {
    Xtc xtc(bookPath, "/.crosspoint");
    const std::string path = xtc.getCoverBmpPath();
    return ensureCachedCover(path, true, generated, [&]() {
      if (!xtc.load()) return false;
      xtc.setupCacheDir();
      if (title) *title = xtc.getTitle();
      if (author) *author = xtc.getAuthor();
      return xtc.generateCoverBmp();
    });
  }

  if (FsHelpers::hasTxtExtension(bookPath) || FsHelpers::hasMarkdownExtension(bookPath)) {
    Txt txt(bookPath, "/.crosspoint");
    const std::string path = txt.getCoverBmpPath();
    return ensureCachedCover(path, true, generated, [&]() {
      if (!txt.load()) return false;
      txt.setupCacheDir();
      if (title) *title = txt.getTitle();
      return txt.generateCoverBmp();
    });
  }

  return "";
}

}  // namespace BookCoverLoader
