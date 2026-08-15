#include "BookMetadataCache.h"

#include <BufferedFile.h>
#include <Logging.h>
#include <Memory.h>
#include <Serialization.h>
#include <Utf8.h>
#include <ZipFile.h>

#include <deque>

#include "FsHelpers.h"

namespace {
// v9: TOC/book titles stored NFC-composed (upstream NFC normalization). The byte
// layout is identical to the fork's v8 and upstream's v8 (both added the `language`
// field), but those two v8 lineages stored titles un-normalized; the `!=` version
// check cannot tell "same number, NFC vs non-NFC" apart, so bump to 9 to force a
// one-time clean re-parse that re-composes existing caches' titles to NFC.
// v11: ignore ambiguous guide text references. Upstream shipped that behavior
// as v10, so CrossMux advances past both lineages to avoid a version collision.
constexpr uint8_t BOOK_CACHE_VERSION = 11;
constexpr char bookBinFile[] = "/book.bin";
constexpr char tmpSpineBinFile[] = "/spine.bin.tmp";
constexpr char tmpTocBinFile[] = "/toc.bin.tmp";
// Buffer size for the buildBookBin streams. 3 buffers x 4KB, transient (freed on
// return); 4KB = 8 SD sectors per transfer, enough to stop the sector-cache thrash.
constexpr size_t BUILD_IO_BUFFER_SIZE = 4096;

// Entry (de)serializers, templated so they run over HalFile and the Buffered*
// wrappers alike (two instantiations each -- a few hundred bytes of flash, in
// exchange for the build path streaming at SD speed instead of per-pod).
template <typename F>
uint32_t writeSpineEntryTo(F& file, const BookMetadataCache::SpineEntry& entry) {
  const uint32_t pos = file.position();
  serialization::writeString(file, entry.href);
  serialization::writePod(file, entry.cumulativeSize);
  serialization::writePod(file, entry.tocIndex);
  return pos;
}

template <typename F>
uint32_t writeTocEntryTo(F& file, const BookMetadataCache::TocEntry& entry) {
  const uint32_t pos = file.position();
  serialization::writeString(file, entry.title);
  serialization::writeString(file, entry.href);
  serialization::writeString(file, entry.anchor);
  serialization::writePod(file, entry.level);
  serialization::writePod(file, entry.spineIndex);
  return pos;
}

template <typename F>
bool readSpineEntryFrom(F& file, BookMetadataCache::SpineEntry& entry) {
  BookMetadataCache::SpineEntry next;
  if (!serialization::readString(file, next.href, serialization::MAX_PATH_BYTES) ||
      !serialization::readPod(file, next.cumulativeSize) || !serialization::readPod(file, next.tocIndex)) {
    return false;
  }
  entry = std::move(next);
  return true;
}

bool readSpineCumulativeSize(HalFile& file, uint32_t& cumulativeSize) {
  uint32_t hrefLength = 0;
  if (!serialization::readPod(file, hrefLength)) return false;
  const size_t position = file.position();
  const size_t fileSize = file.size();
  constexpr size_t trailingBytes = sizeof(uint32_t) + sizeof(int16_t);
  if (position > fileSize || hrefLength > fileSize - position || trailingBytes > fileSize - position - hrefLength ||
      !file.seek(position + hrefLength)) {
    return false;
  }
  int16_t tocIndex = -1;
  return serialization::readPod(file, cumulativeSize) && serialization::readPod(file, tocIndex);
}

template <typename F>
bool readTocEntryFrom(F& file, BookMetadataCache::TocEntry& entry) {
  BookMetadataCache::TocEntry next;
  if (!serialization::readString(file, next.title, serialization::MAX_TEXT_BYTES) ||
      !serialization::readString(file, next.href, serialization::MAX_PATH_BYTES) ||
      !serialization::readString(file, next.anchor, serialization::MAX_PATH_BYTES) ||
      !serialization::readPod(file, next.level) || !serialization::readPod(file, next.spineIndex)) {
    return false;
  }
  entry = std::move(next);
  return true;
}
}  // namespace

/* ============= WRITING / BUILDING FUNCTIONS ================ */

bool BookMetadataCache::beginWrite() {
  buildMode = true;
  buildIoFailed = false;
  spineCount = 0;
  tocCount = 0;
  LOG_DBG("BMC", "Entering write mode");
  return true;
}

bool BookMetadataCache::beginContentOpfPass() {
  LOG_DBG("BMC", "Beginning content opf pass");

  // Open spine file for writing
  if (!Storage.openFileForWrite("BMC", cachePath + tmpSpineBinFile, spineFile)) {
    return false;
  }
  // Wrapper OOM is fine: createSpineEntry falls back to unbuffered writes.
  passOut = makeUniqueNoThrow<serialization::BufferedFileWriter>(spineFile, BUILD_IO_BUFFER_SIZE);
  return true;
}

bool BookMetadataCache::endContentOpfPass() {
  const bool flushed = !passOut || passOut->flush();
  passOut.reset();
  // Explicit close() required: member variable persists beyond function scope
  spineFile.close();
  if (!flushed) {
    LOG_ERR("BMC", "Failed writing spine tmp file");
  }
  return flushed;
}

bool BookMetadataCache::beginTocPass() {
  LOG_DBG("BMC", "Beginning toc pass");

  if (!Storage.openFileForRead("BMC", cachePath + tmpSpineBinFile, spineFile)) {
    return false;
  }
  if (!Storage.openFileForWrite("BMC", cachePath + tmpTocBinFile, tocFile)) {
    // Explicit close() required: member variable persists beyond function scope
    spineFile.close();
    return false;
  }

  if (spineCount >= LARGE_SPINE_THRESHOLD) {
    spineHrefIndex.clear();
    spineHrefIndex.resize(spineCount);
    spineFile.seek(0);
    for (int i = 0; i < spineCount; i++) {
      SpineEntry entry;
      if (!readSpineEntry(spineFile, entry)) {
        LOG_ERR("BMC", "Spine tmp file is truncated at entry %d", i);
        spineFile.close();
        tocFile.close();
        return false;
      }
      SpineHrefIndexEntry idx;
      idx.hrefHash = fnvHash64(entry.href);
      idx.hrefLen = static_cast<uint16_t>(entry.href.size());
      idx.spineIndex = static_cast<int16_t>(i);
      spineHrefIndex[i] = idx;
    }
    std::sort(spineHrefIndex.begin(), spineHrefIndex.end(),
              [](const SpineHrefIndexEntry& a, const SpineHrefIndexEntry& b) {
                return a.hrefHash < b.hrefHash || (a.hrefHash == b.hrefHash && a.hrefLen < b.hrefLen);
              });
    spineFile.seek(0);
    useSpineHrefIndex = true;
    LOG_DBG("BMC", "Using fast index for %d spine items", spineCount);
  } else {
    useSpineHrefIndex = false;
  }

  // Wrapper OOM is fine: createTocEntry falls back to unbuffered writes.
  passOut = makeUniqueNoThrow<serialization::BufferedFileWriter>(tocFile, BUILD_IO_BUFFER_SIZE);
  return true;
}

bool BookMetadataCache::endTocPass() {
  const bool flushed = !passOut || passOut->flush();
  passOut.reset();
  if (!flushed || buildIoFailed) {
    LOG_ERR("BMC", "Failed writing toc tmp file");
  }
  // Explicit close() required: member variables persist beyond function scope
  tocFile.close();
  spineFile.close();

  spineHrefIndex.clear();
  spineHrefIndex.shrink_to_fit();
  useSpineHrefIndex = false;

  return flushed && !buildIoFailed;
}

bool BookMetadataCache::endWrite() {
  if (!buildMode) {
    LOG_DBG("BMC", "endWrite called but not in build mode");
    return false;
  }

  buildMode = false;
  LOG_DBG("BMC", "Wrote %d spine, %d TOC entries", spineCount, tocCount);
  return true;
}

bool BookMetadataCache::buildBookBin(const std::string& epubPath, const BookMetadata& metadata) {
  // Open all three files, writing to meta, reading from spine and toc
  if (!Storage.openFileForWrite("BMC", cachePath + bookBinFile, bookFile)) {
    return false;
  }

  if (!Storage.openFileForRead("BMC", cachePath + tmpSpineBinFile, spineFile)) {
    // Explicit close() required: member variable persists beyond function scope
    bookFile.close();
    return false;
  }

  if (!Storage.openFileForRead("BMC", cachePath + tmpTocBinFile, tocFile)) {
    // Explicit close() required: member variables persist beyond function scope
    bookFile.close();
    spineFile.close();
    return false;
  }

  // Buffered streams for the whole build: every access below is sequential per
  // file, but interleaved ACROSS files, which thrashes SdFat's single shared
  // sector cache when unbuffered (one 512B SD transaction per 4-byte pod --
  // measured 31s for a 1,732-spine omnibus). Three 4KB buffers, freed on return.
  serialization::BufferedFileWriter bookOut(bookFile, BUILD_IO_BUFFER_SIZE);
  serialization::BufferedFileReader spineIn(spineFile, BUILD_IO_BUFFER_SIZE);
  serialization::BufferedFileReader tocIn(tocFile, BUILD_IO_BUFFER_SIZE);
  const auto failBuild = [&](const char* message) {
    LOG_ERR("BMC", "%s", message);
    bookOut.flush();
    bookFile.close();
    spineFile.close();
    tocFile.close();
    Storage.remove((cachePath + bookBinFile).c_str());
    return false;
  };

  constexpr uint32_t headerASize =
      sizeof(BOOK_CACHE_VERSION) + /* LUT Offset */ sizeof(uint32_t) + sizeof(spineCount) + sizeof(tocCount);
  const uint32_t metadataSize = metadata.title.size() + metadata.author.size() + metadata.language.size() +
                                metadata.coverItemHref.size() + metadata.textReferenceHref.size() +
                                sizeof(uint32_t) * 5;
  const uint32_t lutSize = sizeof(uint32_t) * spineCount + sizeof(uint32_t) * tocCount;
  const uint32_t lutOffset = headerASize + metadataSize;

  // Header A
  serialization::writePod(bookOut, BOOK_CACHE_VERSION);
  serialization::writePod(bookOut, lutOffset);
  serialization::writePod(bookOut, spineCount);
  serialization::writePod(bookOut, tocCount);
  // Metadata
  serialization::writeString(bookOut, metadata.title);
  serialization::writeString(bookOut, metadata.author);
  serialization::writeString(bookOut, metadata.language);
  serialization::writeString(bookOut, metadata.coverItemHref);
  serialization::writeString(bookOut, metadata.textReferenceHref);

  // Loop through spine entries, writing LUT positions
  spineIn.seek(0);
  for (int i = 0; i < spineCount; i++) {
    const uint32_t pos = spineIn.position();
    SpineEntry entry;
    if (!readSpineEntryFrom(spineIn, entry)) return failBuild("Spine tmp file is truncated while building LUT");
    serialization::writePod(bookOut, pos + lutOffset + lutSize);
  }
  // Total size of the spine tmp file: entries land in book.bin after the toc LUT
  // and the full spine block, so toc LUT positions are offset by it.
  const auto spineBytes = static_cast<uint32_t>(spineIn.position());

  // Loop through toc entries, writing LUT positions
  tocIn.seek(0);
  for (int i = 0; i < tocCount; i++) {
    const uint32_t pos = tocIn.position();
    TocEntry entry;
    if (!readTocEntryFrom(tocIn, entry)) return failBuild("TOC tmp file is truncated while building LUT");
    serialization::writePod(bookOut, pos + lutOffset + lutSize + spineBytes);
  }

  // LUTs complete
  // Loop through spines from spine file matching up TOC indexes, calculating cumulative size and writing to book.bin

  // Build spineIndex->tocIndex mapping in one pass (O(n) instead of O(n*m))
  std::deque<int16_t> spineToTocIndex(spineCount, -1);
  tocIn.seek(0);
  for (int j = 0; j < tocCount; j++) {
    TocEntry tocEntry;
    if (!readTocEntryFrom(tocIn, tocEntry)) return failBuild("TOC tmp file is truncated while mapping entries");
    if (tocEntry.spineIndex >= 0 && tocEntry.spineIndex < spineCount) {
      if (spineToTocIndex[tocEntry.spineIndex] == -1) {
        spineToTocIndex[tocEntry.spineIndex] = static_cast<int16_t>(j);
      }
    }
  }

  ZipFile zip(epubPath);
  // Pre-open zip file to speed up size calculations
  if (!zip.open()) {
    return failBuild("Could not open EPUB zip for size calculations");
  }
  // NOTE: We intentionally skip calling loadAllFileStatSlims() here.
  // For large EPUBs (2000+ chapters), pre-loading all ZIP central directory entries
  // into memory causes OOM crashes on ESP32-C3's limited ~380KB RAM.
  // Instead, for large books we use a one-pass batch lookup that scans the ZIP
  // central directory once and matches against spine targets using hash comparison.
  // This is O(n*log(m)) instead of O(n*m) while avoiding memory exhaustion.
  // See: https://github.com/crosspoint-reader/crosspoint-reader/issues/134

  std::deque<uint32_t> spineSizes;
  bool useBatchSizes = false;

  if (spineCount >= LARGE_SPINE_THRESHOLD) {
    LOG_DBG("BMC", "Using batch size lookup for %d spine items", spineCount);

    std::deque<ZipFile::SizeTarget> targets;
    targets.resize(spineCount);

    spineIn.seek(0);
    for (int i = 0; i < spineCount; i++) {
      SpineEntry entry;
      if (!readSpineEntryFrom(spineIn, entry)) {
        return failBuild("Spine tmp file is truncated while preparing ZIP size lookup");
      }
      std::string path = FsHelpers::normalisePath(entry.href);

      ZipFile::SizeTarget t;
      t.hash = ZipFile::fnvHash64(path.c_str(), path.size());
      t.len = static_cast<uint16_t>(path.size());
      t.index = static_cast<uint16_t>(i);
      targets[i] = t;
    }

    std::sort(targets.begin(), targets.end(), [](const ZipFile::SizeTarget& a, const ZipFile::SizeTarget& b) {
      return a.hash < b.hash || (a.hash == b.hash && a.len < b.len);
    });

    spineSizes.resize(spineCount, 0);
    int matched = zip.fillUncompressedSizes(targets, spineSizes);
    LOG_DBG("BMC", "Batch lookup matched %d/%d spine items", matched, spineCount);

    targets.clear();
    targets.shrink_to_fit();

    useBatchSizes = true;
  }

  uint32_t cumSize = 0;
  spineIn.seek(0);
  int lastSpineTocIndex = -1;
  for (int i = 0; i < spineCount; i++) {
    SpineEntry spineEntry;
    if (!readSpineEntryFrom(spineIn, spineEntry)) {
      return failBuild("Spine tmp file is truncated while finalizing entries");
    }

    spineEntry.tocIndex = spineToTocIndex[i];

    // Not a huge deal if we don't fine a TOC entry for the spine entry, this is expected behaviour for EPUBs
    // Logging here is for debugging
    if (spineEntry.tocIndex == -1) {
      LOG_DBG("BMC", "Warning: Could not find TOC entry for spine item %d: %s, using title from last section", i,
              spineEntry.href.c_str());
      spineEntry.tocIndex = lastSpineTocIndex;
    }
    lastSpineTocIndex = spineEntry.tocIndex;

    size_t itemSize = 0;
    if (useBatchSizes) {
      itemSize = spineSizes[i];
      if (itemSize == 0) {
        const std::string path = FsHelpers::normalisePath(spineEntry.href);
        if (!zip.getInflatedFileSize(path.c_str(), &itemSize)) {
          LOG_ERR("BMC", "Warning: Could not get size for spine item: %s", path.c_str());
        }
      }
    } else {
      const std::string path = FsHelpers::normalisePath(spineEntry.href);
      if (!zip.getInflatedFileSize(path.c_str(), &itemSize)) {
        LOG_ERR("BMC", "Warning: Could not get size for spine item: %s", path.c_str());
      }
    }

    cumSize += itemSize;
    spineEntry.cumulativeSize = cumSize;

    // Write out spine data to book.bin
    writeSpineEntryTo(bookOut, spineEntry);
  }
  // Close opened zip file
  zip.close();

  // Loop through toc entries from toc file writing to book.bin
  tocIn.seek(0);
  for (int i = 0; i < tocCount; i++) {
    TocEntry tocEntry;
    if (!readTocEntryFrom(tocIn, tocEntry)) return failBuild("TOC tmp file is truncated while finalizing entries");
    writeTocEntryTo(bookOut, tocEntry);
  }

  const bool written = bookOut.flush();

  // Explicit close() required: member variables persist beyond function scope
  bookFile.close();
  spineFile.close();
  tocFile.close();

  if (!written) {
    // A short write (card full/removed) would leave a truncated book.bin that
    // still passes the version check on load; remove it so the next open rebuilds.
    LOG_ERR("BMC", "Failed writing book.bin, removing truncated file");
    Storage.remove((cachePath + bookBinFile).c_str());
    return false;
  }

  LOG_DBG("BMC", "Successfully built book.bin");
  return true;
}

bool BookMetadataCache::cleanupTmpFiles() const {
  const auto spineBinFile = cachePath + tmpSpineBinFile;
  if (Storage.exists(spineBinFile.c_str())) {
    Storage.remove(spineBinFile.c_str());
  }
  const auto tocBinFile = cachePath + tmpTocBinFile;
  if (Storage.exists(tocBinFile.c_str())) {
    Storage.remove(tocBinFile.c_str());
  }
  return true;
}

uint32_t BookMetadataCache::writeSpineEntry(HalFile& file, const SpineEntry& entry) const {
  return writeSpineEntryTo(file, entry);
}

uint32_t BookMetadataCache::writeTocEntry(HalFile& file, const TocEntry& entry) const {
  return writeTocEntryTo(file, entry);
}

// Note: for the LUT to be accurate, this **MUST** be called for all spine items before `addTocEntry` is ever called
// this is because in this function we're marking positions of the items
void BookMetadataCache::createSpineEntry(const std::string& href) {
  if (!buildMode || !spineFile) {
    LOG_DBG("BMC", "createSpineEntry called but not in build mode");
    return;
  }

  const SpineEntry entry(href, 0, -1);
  if (passOut) {
    writeSpineEntryTo(*passOut, entry);
  } else {
    writeSpineEntry(spineFile, entry);
  }
  spineCount++;
}

void BookMetadataCache::createTocEntry(const std::string& title, const std::string& href, const std::string& anchor,
                                       const uint8_t level) {
  if (!buildMode || !tocFile || !spineFile) {
    LOG_DBG("BMC", "createTocEntry called but not in build mode");
    return;
  }

  int16_t spineIndex = -1;

  if (useSpineHrefIndex) {
    uint64_t targetHash = fnvHash64(href);
    uint16_t targetLen = static_cast<uint16_t>(href.size());

    auto it =
        std::lower_bound(spineHrefIndex.begin(), spineHrefIndex.end(), SpineHrefIndexEntry{targetHash, targetLen, 0},
                         [](const SpineHrefIndexEntry& a, const SpineHrefIndexEntry& b) {
                           return a.hrefHash < b.hrefHash || (a.hrefHash == b.hrefHash && a.hrefLen < b.hrefLen);
                         });

    while (it != spineHrefIndex.end() && it->hrefHash == targetHash && it->hrefLen == targetLen) {
      spineIndex = it->spineIndex;
      break;
    }

    if (spineIndex == -1) {
      LOG_DBG("BMC", "createTocEntry: Could not find spine item for TOC href %s", href.c_str());
    }
  } else {
    spineFile.seek(0);
    for (int i = 0; i < spineCount; i++) {
      SpineEntry spineEntry;
      if (!readSpineEntry(spineFile, spineEntry)) {
        LOG_ERR("BMC", "Spine tmp file is truncated while resolving TOC href");
        buildIoFailed = true;
        return;
      }
      if (spineEntry.href == href) {
        spineIndex = static_cast<int16_t>(i);
        break;
      }
    }
    if (spineIndex == -1) {
      LOG_DBG("BMC", "createTocEntry: Could not find spine item for TOC href %s", href.c_str());
    }
  }

  // Compose the title to NFC at index time so the cache stores precomposed glyphs;
  // device fonts have no combining-mark positioning, so NFD titles render broken.
  const TocEntry entry(utf8ComposeNfc(title), href, anchor, level, spineIndex);
  if (passOut) {
    writeTocEntryTo(*passOut, entry);
  } else {
    writeTocEntry(tocFile, entry);
  }
  tocCount++;
}

/* ============= READING / LOADING FUNCTIONS ================ */

bool BookMetadataCache::load() {
  if (!Storage.openFileForRead("BMC", cachePath + bookBinFile, bookFile)) {
    return false;
  }

  uint8_t version = 0;
  if (!serialization::readPod(bookFile, version)) {
    invalidateCorruptCache();
    return false;
  }
  if (version != BOOK_CACHE_VERSION) {
    LOG_DBG("BMC", "Cache version mismatch: expected %d, got %d", BOOK_CACHE_VERSION, version);
    invalidateCorruptCache();
    return false;
  }

  uint32_t nextLutOffset = 0;
  uint16_t nextSpineCount = 0;
  uint16_t nextTocCount = 0;
  if (!serialization::readPod(bookFile, nextLutOffset) || !serialization::readPod(bookFile, nextSpineCount) ||
      !serialization::readPod(bookFile, nextTocCount)) {
    LOG_ERR("BMC", "Cache header is truncated");
    invalidateCorruptCache();
    return false;
  }

  BookMetadata nextMetadata;
  if (!serialization::readString(bookFile, nextMetadata.title, serialization::MAX_TEXT_BYTES) ||
      !serialization::readString(bookFile, nextMetadata.author, serialization::MAX_TEXT_BYTES) ||
      !serialization::readString(bookFile, nextMetadata.language, serialization::MAX_TEXT_BYTES) ||
      !serialization::readString(bookFile, nextMetadata.coverItemHref, serialization::MAX_PATH_BYTES) ||
      !serialization::readString(bookFile, nextMetadata.textReferenceHref, serialization::MAX_PATH_BYTES)) {
    LOG_ERR("BMC", "Cache metadata is truncated or oversized");
    invalidateCorruptCache();
    return false;
  }

  const uint32_t lutSize = (static_cast<uint32_t>(nextSpineCount) + nextTocCount) * sizeof(uint32_t);
  const size_t fileSize = bookFile.size();
  if (nextLutOffset > fileSize || lutSize > fileSize - nextLutOffset) {
    LOG_ERR("BMC", "Cache LUT is outside the file");
    invalidateCorruptCache();
    return false;
  }

  // Cache cumulative spine sizes in RAM. The progress bar (every render) and percent
  // jumps otherwise pay 2 seeks + a heap-allocating SpineEntry read per access. Spine
  // entries are stored contiguously in index order immediately after the LUTs, so read
  // them in a single sequential pass.
  std::unique_ptr<uint32_t[]> nextCumulativeSizes;
  uint16_t nextCumulativeSizeCount = 0;
  if (nextSpineCount > 0 && nextSpineCount <= MAX_CUMULATIVE_SIZE_CACHE_ITEMS) {
    auto sizes = makeUniqueNoThrow<uint32_t[]>(nextSpineCount);
    if (sizes) {
      if (!bookFile.seek(nextLutOffset + lutSize)) {
        LOG_ERR("BMC", "Could not seek to cache spine data");
        invalidateCorruptCache();
        return false;
      }
      for (uint16_t i = 0; i < nextSpineCount; i++) {
        if (!readSpineCumulativeSize(bookFile, sizes[i])) {
          LOG_ERR("BMC", "Cache spine entry %u is truncated", i);
          invalidateCorruptCache();
          return false;
        }
      }
      nextCumulativeSizeCount = nextSpineCount;
      nextCumulativeSizes = std::move(sizes);
    } else {
      LOG_ERR("BMC", "OOM caching %u cumulative spine sizes", nextSpineCount);
    }
  }

  lutOffset = nextLutOffset;
  spineCount = nextSpineCount;
  tocCount = nextTocCount;
  coreMetadata = std::move(nextMetadata);
  cumulativeSizes = std::move(nextCumulativeSizes);
  cumulativeSizeCount = nextCumulativeSizeCount;
  loaded = true;
  LOG_DBG("BMC", "Loaded cache data: %d spine, %d TOC entries", spineCount, tocCount);
  return true;
}

bool BookMetadataCache::getCumulativeSize(const int index, uint32_t& size) const {
  if (!cumulativeSizes || index < 0 || index >= cumulativeSizeCount) return false;
  size = cumulativeSizes[index];
  return true;
}

BookMetadataCache::SpineEntry BookMetadataCache::getSpineEntry(const int index) {
  if (!loaded) {
    LOG_ERR("BMC", "getSpineEntry called but cache not loaded");
    return {};
  }

  if (index < 0 || index >= static_cast<int>(spineCount)) {
    LOG_ERR("BMC", "getSpineEntry index %d out of range", index);
    return {};
  }

  // Seek to spine LUT item, read from LUT and get out data
  uint32_t spineEntryPos = 0;
  if (!bookFile.seek(lutOffset + sizeof(uint32_t) * index) || !serialization::readPod(bookFile, spineEntryPos) ||
      spineEntryPos >= bookFile.size() || !bookFile.seek(spineEntryPos)) {
    LOG_ERR("BMC", "Invalid spine LUT entry %d", index);
    invalidateCorruptCache();
    return {};
  }
  SpineEntry entry;
  if (!readSpineEntry(bookFile, entry)) {
    LOG_ERR("BMC", "Spine cache entry %d is truncated", index);
    invalidateCorruptCache();
    return {};
  }
  return entry;
}

BookMetadataCache::TocEntry BookMetadataCache::getTocEntry(const int index) {
  if (!loaded) {
    LOG_ERR("BMC", "getTocEntry called but cache not loaded");
    return {};
  }

  if (index < 0 || index >= static_cast<int>(tocCount)) {
    LOG_ERR("BMC", "getTocEntry index %d out of range", index);
    return {};
  }

  // Seek to TOC LUT item, read from LUT and get out data
  uint32_t tocEntryPos = 0;
  if (!bookFile.seek(lutOffset + sizeof(uint32_t) * spineCount + sizeof(uint32_t) * index) ||
      !serialization::readPod(bookFile, tocEntryPos) || tocEntryPos >= bookFile.size() || !bookFile.seek(tocEntryPos)) {
    LOG_ERR("BMC", "Invalid TOC LUT entry %d", index);
    invalidateCorruptCache();
    return {};
  }
  TocEntry entry;
  if (!readTocEntry(bookFile, entry)) {
    LOG_ERR("BMC", "TOC cache entry %d is truncated", index);
    invalidateCorruptCache();
    return {};
  }
  return entry;
}

bool BookMetadataCache::readSpineEntry(HalFile& file, SpineEntry& entry) const {
  return readSpineEntryFrom(file, entry);
}

bool BookMetadataCache::readTocEntry(HalFile& file, TocEntry& entry) const { return readTocEntryFrom(file, entry); }

void BookMetadataCache::invalidateCorruptCache() {
  bookFile.close();
  loaded = false;
  cumulativeSizes.reset();
  cumulativeSizeCount = 0;
  Storage.remove((cachePath + bookBinFile).c_str());
}
