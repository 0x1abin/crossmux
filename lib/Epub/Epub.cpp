#include "Epub.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>
#include <Memory.h>
#include <PngToBmpConverter.h>
#include <Utf8.h>
#include <ZipFile.h>

#include <cstring>

#include "Epub/parsers/ContainerParser.h"
#include "Epub/parsers/ContentOpfParser.h"
#include "Epub/parsers/TocNavParser.h"
#include "Epub/parsers/TocNcxParser.h"

namespace {
enum class CoverImageType : uint8_t { None, Jpeg, Png };

CoverImageType coverImageType(const std::string& path) {
  HalFile file;
  uint8_t prefix[8] = {};
  if (!Storage.openFileForRead("EBP", path, file) || file.fileSize64() <= sizeof(prefix) ||
      file.read(prefix, sizeof(prefix)) != static_cast<int>(sizeof(prefix))) {
    return CoverImageType::None;
  }
  static constexpr uint8_t kPngMagic[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (memcmp(prefix, kPngMagic, sizeof(kPngMagic)) == 0) return CoverImageType::Png;
  return prefix[0] == 0xFF && prefix[1] == 0xD8 && prefix[2] == 0xFF ? CoverImageType::Jpeg : CoverImageType::None;
}

template <typename JpegConvert, typename PngConvert>
bool convertCoverFile(const std::string& sourcePath, const CoverImageType type, const std::string& outputPath,
                      const char* outputKind, JpegConvert jpegConvert, PngConvert pngConvert) {
  if (type == CoverImageType::None) return false;
  const char* format = type == CoverImageType::Jpeg ? "JPG" : "PNG";
  LOG_DBG("EBP", "Generating %s BMP from %s cover image", outputKind, format);

  bool success = false;
  {
    HalFile source;
    HalFile output;
    if (Storage.openFileForRead("EBP", sourcePath, source) && Storage.openFileForWrite("EBP", outputPath, output)) {
      success = type == CoverImageType::Jpeg ? jpegConvert(source, output) : pngConvert(source, output);
    }
  }
  if (!success) {
    LOG_ERR("EBP", "Failed to generate %s BMP from %s cover image", outputKind, format);
    Storage.remove(outputPath.c_str());
  }
  return success;
}

template <typename JpegConvert, typename PngConvert>
bool convertExtractedCover(const Epub& epub, const std::string& coverHref, const std::string& outputPath,
                           const char* outputKind, JpegConvert jpegConvert, PngConvert pngConvert) {
  const bool isJpeg = FsHelpers::hasJpgExtension(coverHref);
  const std::string tempPath = epub.getCachePath() + (isJpeg ? "/.cover.jpg" : "/.cover.png");

  if (!epub.extractItemToFile(coverHref, tempPath)) {
    LOG_ERR("EBP", "Failed to extract cover image for %s", outputKind);
    return false;
  }
  const ScopedCleanup removeTemp{[&tempPath] { Storage.remove(tempPath.c_str()); }};
  return convertCoverFile(tempPath, isJpeg ? CoverImageType::Jpeg : CoverImageType::Png, outputPath, outputKind,
                          jpegConvert, pngConvert);
}

template <typename JpegConvert, typename PngConvert>
bool convertOverrideCover(const Epub& epub, const std::string& outputPath, const char* outputKind,
                          JpegConvert jpegConvert, PngConvert pngConvert) {
  const std::string sourcePath = epub.getCoverOverridePath();
  return convertCoverFile(sourcePath, coverImageType(sourcePath), outputPath, outputKind, jpegConvert, pngConvert);
}
}  // namespace

bool Epub::findContentOpfFile(std::string* contentOpfFile) const {
  const auto containerPath = "META-INF/container.xml";
  size_t containerSize;

  // Get file size without loading it all into heap
  if (!getItemSize(containerPath, &containerSize)) {
    LOG_ERR("EBP", "Could not find or size META-INF/container.xml");
    return false;
  }

  ContainerParser containerParser(containerSize);

  if (!containerParser.setup()) {
    return false;
  }

  // Stream read (reusing your existing stream logic)
  if (!readItemContentsToStream(containerPath, containerParser, 512)) {
    LOG_ERR("EBP", "Could not read META-INF/container.xml");
    return false;
  }

  // Extract the result
  if (containerParser.fullPath.empty()) {
    LOG_ERR("EBP", "Could not find valid rootfile in container.xml");
    return false;
  }

  *contentOpfFile = std::move(containerParser.fullPath);
  return true;
}

bool Epub::parseContentOpf(BookMetadataCache::BookMetadata& bookMetadata, const bool writeSpineEntries) {
  std::string contentOpfFilePath;
  if (!findContentOpfFile(&contentOpfFilePath)) {
    LOG_ERR("EBP", "Could not find content.opf in zip");
    return false;
  }

  contentBasePath = contentOpfFilePath.substr(0, contentOpfFilePath.find_last_of('/') + 1);

  LOG_DBG("EBP", "Parsing content.opf: %s", contentOpfFilePath.c_str());

  size_t contentOpfSize;
  if (!getItemSize(contentOpfFilePath, &contentOpfSize)) {
    LOG_ERR("EBP", "Could not get size of content.opf");
    return false;
  }

  ContentOpfParser opfParser(getCachePath(), getBasePath(), contentOpfSize,
                             writeSpineEntries ? bookMetadataCache.get() : nullptr);
  if (!opfParser.setup()) {
    LOG_ERR("EBP", "Could not setup content.opf parser");
    return false;
  }

  if (!readItemContentsToStream(contentOpfFilePath, opfParser, 1024)) {
    LOG_ERR("EBP", "Could not read content.opf");
    return false;
  }

  // Grab data from opfParser into epub. Normalize titles to NFC so NFD (combining
  // mark) text renders correctly — the device fonts have no mark positioning.
  bookMetadata.title = utf8ComposeNfc(opfParser.title);
  bookMetadata.author = opfParser.author;
  bookMetadata.language = opfParser.language;
  bookMetadata.coverItemHref = opfParser.coverItemHref;

  // Guide-based cover fallback: if no cover found via metadata/properties,
  // try extracting the image reference from the guide's cover page XHTML
  if (bookMetadata.coverItemHref.empty() && !opfParser.guideCoverPageHref.empty()) {
    LOG_DBG("EBP", "No cover from metadata, trying guide cover page: %s", opfParser.guideCoverPageHref.c_str());
    size_t coverPageSize;
    uint8_t* coverPageData = readItemContentsToBytes(opfParser.guideCoverPageHref, &coverPageSize, true);
    if (coverPageData) {
      const std::string coverPageHtml(reinterpret_cast<char*>(coverPageData), coverPageSize);
      free(coverPageData);

      // Determine base path of the cover page for resolving relative image references
      std::string coverPageBase;
      const auto lastSlash = opfParser.guideCoverPageHref.rfind('/');
      if (lastSlash != std::string::npos) {
        coverPageBase = opfParser.guideCoverPageHref.substr(0, lastSlash + 1);
      }

      // Search for image references: xlink:href="..." (SVG) and src="..." (img)
      std::string imageRef;
      for (const char* pattern : {"xlink:href=\"", "src=\""}) {
        auto pos = coverPageHtml.find(pattern);
        while (pos != std::string::npos) {
          pos += strlen(pattern);
          const auto endPos = coverPageHtml.find('"', pos);
          if (endPos != std::string::npos) {
            const auto ref = std::string_view{coverPageHtml}.substr(pos, endPos - pos);
            // Cover BMP generation supports JPG/PNG only; skip GIF so an unsupported wrapper image
            // does not block a later supported cover reference.
            if (FsHelpers::hasPngExtension(ref) || FsHelpers::hasJpgExtension(ref)) {
              imageRef = ref;
              break;
            }
          }
          pos = coverPageHtml.find(pattern, pos);
        }
        if (!imageRef.empty()) break;
      }

      if (!imageRef.empty()) {
        bookMetadata.coverItemHref = FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(coverPageBase + imageRef));
        LOG_DBG("EBP", "Found cover image from guide: %s", bookMetadata.coverItemHref.c_str());
      }
    }
  }

  bookMetadata.textReferenceHref = opfParser.textReferenceHref;

  if (!opfParser.tocNcxPath.empty()) {
    tocNcxItem = opfParser.tocNcxPath;
  }

  if (!opfParser.tocNavPath.empty()) {
    tocNavItem = opfParser.tocNavPath;
  }

  if (!opfParser.cssFiles.empty()) {
    cssFiles = opfParser.cssFiles;
  }

  LOG_DBG("EBP", "Successfully parsed content.opf");
  return true;
}

bool Epub::parseTocNcxFile() const {
  // the ncx file should have been specified in the content.opf file
  if (tocNcxItem.empty()) {
    LOG_DBG("EBP", "No ncx file specified");
    return false;
  }

  LOG_DBG("EBP", "Parsing toc ncx file: %s", tocNcxItem.c_str());

  size_t ncxSize;
  if (!getItemSize(tocNcxItem, &ncxSize)) {
    LOG_ERR("EBP", "Could not get size of toc ncx file");
    return false;
  }

  TocNcxParser ncxParser(contentBasePath, ncxSize, bookMetadataCache.get());

  if (!ncxParser.setup()) {
    LOG_ERR("EBP", "Could not setup toc ncx parser");
    return false;
  }

  // Stream the decompressed NCX straight into the parser instead of round-tripping
  // through a temp file on the SD card (decompress -> write -> reopen -> reread -> delete).
  if (!readItemContentsToStream(tocNcxItem, ncxParser, 1024)) {
    LOG_ERR("EBP", "Could not read toc ncx file");
    return false;
  }

  LOG_DBG("EBP", "Parsed TOC items");
  return true;
}

bool Epub::parseTocNavFile() const {
  // the nav file should have been specified in the content.opf file (EPUB 3)
  if (tocNavItem.empty()) {
    LOG_DBG("EBP", "No nav file specified");
    return false;
  }

  LOG_DBG("EBP", "Parsing toc nav file: %s", tocNavItem.c_str());

  size_t navSize;
  if (!getItemSize(tocNavItem, &navSize)) {
    LOG_ERR("EBP", "Could not get size of toc nav file");
    return false;
  }

  // Note: We can't use `contentBasePath` here as the nav file may be in a different folder to the content.opf
  // and the HTMLX nav file will have hrefs relative to itself
  const std::string navContentBasePath = tocNavItem.substr(0, tocNavItem.find_last_of('/') + 1);
  TocNavParser navParser(navContentBasePath, navSize, bookMetadataCache.get());

  if (!navParser.setup()) {
    LOG_ERR("EBP", "Could not setup toc nav parser");
    return false;
  }

  // Stream the decompressed nav document straight into the parser instead of round-tripping
  // through a temp file on the SD card (decompress -> write -> reopen -> reread -> delete).
  if (!readItemContentsToStream(tocNavItem, navParser, 1024)) {
    LOG_ERR("EBP", "Could not read toc nav file");
    return false;
  }

  LOG_DBG("EBP", "Parsed TOC nav items");
  return true;
}

void Epub::discoverCssFilesFromZip() {
  const std::string& opfDir = contentBasePath;
  ZipFile zf(filepath);

  if (!zf.enumerateFilePaths([&](std::string_view filePath) {
        if (!opfDir.empty() && filePath.find(opfDir) != 0) {
          return;
        }

        if (!FsHelpers::hasCssExtension(filePath)) {
          return;
        }

        if (std::find(cssFiles.begin(), cssFiles.end(), filePath) != cssFiles.end()) {
          return;
        }

        LOG_DBG("EBP", "Discovered CSS file via ZIP enumeration: %.*s", (int)filePath.size(), filePath.data());
        cssFiles.push_back(std::string{filePath});
      })) {
    LOG_ERR("EBP", "Failed to enumerate ZIP file paths for CSS discovery");
  }
}

CssParser::ParseResult Epub::parseCssFiles(const CssParser::CacheStatus existingCacheStatus) const {
  // Maximum CSS file size we'll attempt to parse (uncompressed)
  // Larger files risk memory exhaustion on ESP32
  constexpr size_t MAX_CSS_FILE_SIZE = 128 * 1024;  // 128KB
  // Minimum heap required before attempting CSS parsing
  constexpr size_t MIN_HEAP_FOR_CSS_PARSING = 64 * 1024;  // 64KB

  if (cssFiles.empty()) {
    LOG_DBG("EBP", "No CSS files to parse, but CssParser created for inline styles");
  }

  LOG_DBG("EBP", "CSS files to parse: %zu", cssFiles.size());

  const bool hasPartialCache = existingCacheStatus == CssParser::CacheStatus::Partial;
  cssParser->clear();

  // Some converters emit one byte-identical stylesheet per chapter (100+ .css
  // entries), and each parse costs a zip locate plus an SD extract round-trip.
  // Match each normalized CSS path to its central-directory (CRC32,
  // compressed size) without throwing container allocations, then parse only
  // the first of each identical pair. If scratch allocation fails, parsing all
  // stylesheets is slower but remains correct.
  struct CssDedupEntry {
    uint64_t pathHash = 0;
    uint64_t contentKey = 0;
    size_t cssIndex = 0;
  };
  std::unique_ptr<CssDedupEntry[]> dedupEntries;
  if (cssFiles.size() > 1) {
    dedupEntries = makeUniqueNoThrow<CssDedupEntry[]>(cssFiles.size());
  }
  if (dedupEntries) {
    for (size_t i = 0; i < cssFiles.size(); i++) {
      dedupEntries[i].pathHash = ZipFile::fnvHash64(cssFiles[i].data(), cssFiles[i].size());
      dedupEntries[i].cssIndex = i;
    }
    std::sort(dedupEntries.get(), dedupEntries.get() + cssFiles.size(),
              [](const CssDedupEntry& lhs, const CssDedupEntry& rhs) { return lhs.pathHash < rhs.pathHash; });

    ZipFile(filepath).enumerateFileEntries([&](std::string_view entryPath, uint32_t crc32, uint32_t compressedSize) {
      if (!FsHelpers::hasCssExtension(entryPath)) {
        return;
      }

      const uint64_t pathHash = ZipFile::fnvHash64(entryPath.data(), entryPath.size());
      auto* match = std::lower_bound(
          dedupEntries.get(), dedupEntries.get() + cssFiles.size(), pathHash,
          [](const CssDedupEntry& candidate, const uint64_t hash) { return candidate.pathHash < hash; });
      for (const auto* end = dedupEntries.get() + cssFiles.size(); match != end && match->pathHash == pathHash;
           match++) {
        if (entryPath == cssFiles[match->cssIndex]) {
          match->contentKey = (static_cast<uint64_t>(crc32) << 32) | compressedSize;
          break;
        }
      }
    });
    std::sort(dedupEntries.get(), dedupEntries.get() + cssFiles.size(),
              [](const CssDedupEntry& lhs, const CssDedupEntry& rhs) { return lhs.cssIndex < rhs.cssIndex; });
  } else if (cssFiles.size() > 1) {
    LOG_ERR("EBP", "Insufficient heap for CSS deduplication; parsing every stylesheet");
  }

  size_t skippedDuplicates = 0;
  CssParser::ParseResult parseResult = CssParser::ParseResult::Complete;

  // No cache yet - parse CSS files
  for (size_t cssIndex = 0; cssIndex < cssFiles.size(); cssIndex++) {
    const auto& cssPath = cssFiles[cssIndex];
    const uint64_t dedupKey = dedupEntries ? dedupEntries[cssIndex].contentKey : 0;
    if (dedupKey != 0) {
      const bool seen =
          std::any_of(dedupEntries.get(), dedupEntries.get() + cssIndex,
                      [dedupKey](const CssDedupEntry& candidate) { return candidate.contentKey == dedupKey; });
      if (seen) {
        skippedDuplicates++;
        continue;
      }
    }
    LOG_DBG("EBP", "Parsing CSS file: %s", cssPath.c_str());

    // Check heap before parsing - CSS parsing allocates heavily
    const uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < MIN_HEAP_FOR_CSS_PARSING) {
      LOG_ERR("EBP", "Insufficient heap for CSS parsing (%u bytes free, need %zu), skipping: %s", freeHeap,
              MIN_HEAP_FOR_CSS_PARSING, cssPath.c_str());
      if (parseResult == CssParser::ParseResult::Complete) {
        parseResult = CssParser::ParseResult::Partial;
      }
      continue;
    }

    // Check CSS file size before decompressing - skip files that are too large
    size_t cssFileSize = 0;
    if (getItemSize(cssPath, &cssFileSize)) {
      if (cssFileSize > MAX_CSS_FILE_SIZE) {
        LOG_ERR("EBP", "CSS file too large (%zu bytes > %zu max), skipping: %s", cssFileSize, MAX_CSS_FILE_SIZE,
                cssPath.c_str());
        if (parseResult == CssParser::ParseResult::Complete) {
          parseResult = CssParser::ParseResult::Partial;
        }
        continue;
      }
    }

    // Extract CSS file to temp location
    const auto tmpCssPath = getCachePath() + "/.tmp.css";
    HalFile tempCssFile;
    if (!Storage.openFileForWrite("EBP", tmpCssPath, tempCssFile)) {
      LOG_ERR("EBP", "Could not create temp CSS file");
      parseResult = CssParser::ParseResult::Error;
      continue;
    }
    if (!readItemContentsToStream(cssPath, tempCssFile, 1024)) {
      LOG_ERR("EBP", "Could not read CSS file: %s", cssPath.c_str());
      // Explicitly close() file before calling Storage.remove()
      tempCssFile.close();
      Storage.remove(tmpCssPath.c_str());
      parseResult = CssParser::ParseResult::Error;
      continue;
    }
    // Explicitly close() file before reopening for reading
    tempCssFile.close();

    // Parse the CSS file
    if (!Storage.openFileForRead("EBP", tmpCssPath, tempCssFile)) {
      LOG_ERR("EBP", "Could not open temp CSS file for reading");
      Storage.remove(tmpCssPath.c_str());
      parseResult = CssParser::ParseResult::Error;
      continue;
    }
    const CssParser::ParseResult streamResult = cssParser->loadFromStream(tempCssFile);
    // Explicitly close() file before calling Storage.remove()
    tempCssFile.close();
    Storage.remove(tmpCssPath.c_str());
    if (streamResult == CssParser::ParseResult::Error) {
      parseResult = CssParser::ParseResult::Error;
    } else if (streamResult == CssParser::ParseResult::Partial && parseResult == CssParser::ParseResult::Complete) {
      parseResult = CssParser::ParseResult::Partial;
    }
  }

  if (parseResult == CssParser::ParseResult::Error) {
    LOG_ERR("EBP", "CSS parse failed; preserving any previous cache for a later retry");
    cssParser->clear();
    return parseResult;
  }

  if (parseResult == CssParser::ParseResult::Partial && cssParser->empty()) {
    LOG_ERR("EBP", "CSS parsing stopped before any usable rules were loaded; cache will not be replaced");
    cssParser->clear();
    return CssParser::ParseResult::Error;
  }

  if (parseResult == CssParser::ParseResult::Partial && hasPartialCache) {
    LOG_DBG("EBP", "CSS retry remained partial; preserving the previous partial cache");
    cssParser->clear();
    return parseResult;
  }

  // A partial cache remains useful for this session, but its header ensures a
  // later EPUB load retries the source stylesheets when more heap is available.
  if (!cssParser->saveToCache(parseResult == CssParser::ParseResult::Complete)) {
    LOG_ERR("EBP", "Failed to save CSS rules to cache");
    cssParser->clear();
    return CssParser::ParseResult::Error;
  }

  LOG_DBG("EBP", "Loaded %zu CSS style rules from %zu files (%zu identical duplicates skipped, %s)",
          cssParser->ruleCount(), cssFiles.size(), skippedDuplicates,
          parseResult == CssParser::ParseResult::Complete ? "complete" : "partial");
  cssParser->clear();
  return parseResult;
}

// load in the meta data for the epub file
bool Epub::load(const bool buildIfMissing, const bool skipLoadingCss) {
  LOG_DBG("EBP", "Loading ePub: %s", filepath.c_str());

  // Initialize spine/TOC cache
  bookMetadataCache = makeUniqueNoThrow<BookMetadataCache>(cachePath);
  // Always create CssParser - needed for inline style parsing even without CSS files
  cssParser = makeUniqueNoThrow<CssParser>(cachePath);
  if (!bookMetadataCache || !cssParser) {
    LOG_ERR("EBP", "OOM: EPUB metadata helpers");
    return false;
  }

  // Try to load existing cache first
  if (bookMetadataCache->load()) {
    if (!skipLoadingCss) {
      const CssParser::CacheStatus cacheStatus = cssParser->inspectCache();
      CssParser::CacheLoadResult cacheLoadResult = CssParser::CacheLoadResult::Invalid;
      if (cacheStatus == CssParser::CacheStatus::Complete) {
        cacheLoadResult = cssParser->loadFromCache();
      }

      if (cacheLoadResult == CssParser::CacheLoadResult::LowMemory) {
        LOG_ERR("EBP", "Insufficient heap to load CSS cache; keeping it for a later retry");
      } else if (cacheLoadResult != CssParser::CacheLoadResult::Complete) {
        LOG_DBG("EBP", "CSS cache missing, partial, or invalid; attempting to parse source stylesheets");
        if (cacheStatus == CssParser::CacheStatus::Invalid ||
            (cacheStatus == CssParser::CacheStatus::Complete &&
             cacheLoadResult == CssParser::CacheLoadResult::Invalid)) {
          cssParser->deleteCache();
        }

        BookMetadataCache::BookMetadata cachedMetadata = bookMetadataCache->coreMetadata;
        CssParser::ParseResult cssParseResult = CssParser::ParseResult::Error;
        if (!parseContentOpf(cachedMetadata, /*writeSpineEntries=*/false)) {
          LOG_ERR("EBP", "Could not parse content.opf from cached bookMetadata for CSS files");
        } else {
          discoverCssFilesFromZip();
          bookMetadataCache.reset();
          cssParseResult = parseCssFiles(cacheStatus);
        }
        bookMetadataCache.reset();
        bookMetadataCache = makeUniqueNoThrow<BookMetadataCache>(cachePath);
        if (!bookMetadataCache) {
          LOG_ERR("EBP", "OOM: BookMetadataCache (%u bytes)", static_cast<unsigned>(sizeof(BookMetadataCache)));
          return false;
        }
        if (!bookMetadataCache->load()) {
          LOG_ERR("EBP", "Failed to reload cache after CSS rebuild");
          return false;
        }
        const bool cssCacheChanged =
            cssParseResult == CssParser::ParseResult::Complete ||
            (cssParseResult == CssParser::ParseResult::Partial && cacheStatus != CssParser::CacheStatus::Partial);
        if (cssCacheChanged) {
          // The CSS cache changed, so section caches must use the same rule set.
          Storage.removeDir((cachePath + "/sections").c_str());
        }
      }
    }
    // Release the resolved CSS rule map: it is only needed transiently while building
    // section caches, and createSectionFile reloads it from cache on demand. Holding it
    // resident pins tens of KB for the whole reading session (more on warm resume into
    // an already-cached chapter, where createSectionFile never runs to clear it).
    cssParser->clear();
    LOG_DBG("EBP", "Loaded ePub: %s", filepath.c_str());
    return true;
  }

  // If we didn't load from cache above and we aren't allowed to build, fail now
  if (!buildIfMissing) {
    return false;
  }

  // Cache doesn't exist or is invalid, build it
  LOG_DBG("EBP", "Cache not found, building spine/TOC cache");
  setupCacheDir();

  const uint32_t indexingStart = millis();

  // Begin building cache - stream entries to disk immediately
  if (!bookMetadataCache->beginWrite()) {
    LOG_ERR("EBP", "Could not begin writing cache");
    return false;
  }

  // OPF Pass
  const uint32_t opfStart = millis();
  BookMetadataCache::BookMetadata bookMetadata;
  if (!bookMetadataCache->beginContentOpfPass()) {
    LOG_ERR("EBP", "Could not begin writing content.opf pass");
    return false;
  }
  if (!parseContentOpf(bookMetadata)) {
    LOG_ERR("EBP", "Could not parse content.opf");
    return false;
  }
  discoverCssFilesFromZip();
  if (!bookMetadataCache->endContentOpfPass()) {
    LOG_ERR("EBP", "Could not end writing content.opf pass");
    return false;
  }
  LOG_DBG("EBP", "OPF pass completed in %lu ms", millis() - opfStart);

  // TOC Pass - try EPUB 3 nav first, fall back to NCX
  const uint32_t tocStart = millis();
  if (!bookMetadataCache->beginTocPass()) {
    LOG_ERR("EBP", "Could not begin writing toc pass");
    return false;
  }

  bool tocParsed = false;

  // Try EPUB 3 nav document first (preferred)
  if (!tocNavItem.empty()) {
    LOG_DBG("EBP", "Attempting to parse EPUB 3 nav document");
    tocParsed = parseTocNavFile();
  }

  // Fall back to NCX if nav parsing failed or wasn't available
  if (!tocParsed && !tocNcxItem.empty()) {
    LOG_DBG("EBP", "Falling back to NCX TOC");
    tocParsed = parseTocNcxFile();
  }

  if (!tocParsed) {
    LOG_ERR("EBP", "Warning: Could not parse any TOC format");
    // Continue anyway - book will work without TOC
  }

  if (!bookMetadataCache->endTocPass()) {
    LOG_ERR("EBP", "Could not end writing toc pass");
    return false;
  }
  LOG_DBG("EBP", "TOC pass completed in %lu ms", millis() - tocStart);

  // Close the cache files
  if (!bookMetadataCache->endWrite()) {
    LOG_ERR("EBP", "Could not end writing cache");
    return false;
  }

  // Build final book.bin
  const uint32_t buildStart = millis();
  if (!bookMetadataCache->buildBookBin(filepath, bookMetadata)) {
    LOG_ERR("EBP", "Could not update mappings and sizes");
    return false;
  }
  LOG_DBG("EBP", "buildBookBin completed in %lu ms", millis() - buildStart);
  LOG_DBG("EBP", "Total indexing completed in %lu ms", millis() - indexingStart);

  if (!bookMetadataCache->cleanupTmpFiles()) {
    LOG_DBG("EBP", "Could not cleanup tmp files - ignoring");
  }

  if (!skipLoadingCss) {
    // Parse CSS before reloading book.bin to leave more heap for CSS rule-table growth.
    bookMetadataCache.reset();
    if (parseCssFiles(cssParser->inspectCache()) != CssParser::ParseResult::Error) {
      Storage.removeDir((cachePath + "/sections").c_str());
    }
  }

  // Reload the cache from disk so it's in the correct state
  bookMetadataCache = makeUniqueNoThrow<BookMetadataCache>(cachePath);
  if (!bookMetadataCache) {
    LOG_ERR("EBP", "OOM: BookMetadataCache (%u bytes)", static_cast<unsigned>(sizeof(BookMetadataCache)));
    return false;
  }
  if (!bookMetadataCache->load()) {
    LOG_ERR("EBP", "Failed to reload cache after writing");
    return false;
  }

  LOG_DBG("EBP", "Loaded ePub: %s", filepath.c_str());
  return true;
}

bool Epub::clearCache() const {
  if (!Storage.exists(cachePath.c_str())) {
    LOG_DBG("EPB", "Cache does not exist, no action needed");
    return true;
  }

  if (!Storage.removeDir(cachePath.c_str())) {
    LOG_ERR("EPB", "Failed to clear cache");
    return false;
  }

  LOG_DBG("EPB", "Cache cleared successfully");
  return true;
}

void Epub::setupCacheDir() const {
  if (Storage.exists(cachePath.c_str())) {
    return;
  }

  Storage.mkdir(cachePath.c_str());
}

const std::string& Epub::getCachePath() const { return cachePath; }

const std::string& Epub::getPath() const { return filepath; }

const std::string& Epub::getTitle() const {
  static std::string blank;
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return blank;
  }

  return bookMetadataCache->coreMetadata.title;
}

const std::string& Epub::getAuthor() const {
  static std::string blank;
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return blank;
  }

  return bookMetadataCache->coreMetadata.author;
}

const std::string& Epub::getLanguage() const {
  static std::string blank;
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return blank;
  }

  return bookMetadataCache->coreMetadata.language;
}

std::string Epub::getCoverOverridePath() const { return cachePath + "/cover.override"; }

bool Epub::hasCoverOverride() const { return coverImageType(getCoverOverridePath()) != CoverImageType::None; }

std::string Epub::getCoverBmpPath(bool cropped) const {
  const auto coverFileName = std::string("cover") + (cropped ? "_crop" : "");
  return cachePath + "/" + coverFileName + ".bmp";
}

bool Epub::generateCoverBmp(bool cropped) const {
  // Already generated, return true
  if (Storage.exists(getCoverBmpPath(cropped).c_str())) {
    return true;
  }

  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR("EBP", "Cannot generate cover BMP, cache not loaded");
    return false;
  }

  const auto coverImageHref = bookMetadataCache->coreMetadata.coverItemHref;
  if (FsHelpers::hasJpgExtension(coverImageHref) || FsHelpers::hasPngExtension(coverImageHref)) {
    const std::string outputPath = getCoverBmpPath(cropped);
    if (convertExtractedCover(
            *this, coverImageHref, outputPath, cropped ? "cropped cover" : "cover",
            [cropped](HalFile& source, HalFile& output) {
              return JpegToBmpConverter::jpegFileToBmpStream(source, output, cropped);
            },
            [cropped](HalFile& source, HalFile& output) {
              return PngToBmpConverter::pngFileToBmpStream(source, output, cropped);
            })) {
      return true;
    }
  }

  const std::string outputPath = getCoverBmpPath(cropped);
  return convertOverrideCover(
      *this, outputPath, cropped ? "cropped cover" : "cover",
      [cropped](HalFile& source, HalFile& output) {
        return JpegToBmpConverter::jpegFileToBmpStream(source, output, cropped);
      },
      [cropped](HalFile& source, HalFile& output) {
        return PngToBmpConverter::pngFileToBmpStream(source, output, cropped);
      });
}

std::string Epub::getThumbBmpPath() const { return cachePath + "/thumb_[HEIGHT].bmp"; }
std::string Epub::getThumbBmpPath(int height) const { return cachePath + "/thumb_" + std::to_string(height) + ".bmp"; }

bool Epub::generateThumbBmp(int height) const {
  // Already generated, return true
  if (Storage.exists(getThumbBmpPath(height).c_str())) {
    return true;
  }

  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR("EBP", "Cannot generate thumb BMP, cache not loaded");
    return false;
  }

  const auto coverImageHref = bookMetadataCache->coreMetadata.coverItemHref;
  const int targetWidth = height * 3 / 5;
  const std::string outputPath = getThumbBmpPath(height);
  if (FsHelpers::hasJpgExtension(coverImageHref) || FsHelpers::hasPngExtension(coverImageHref)) {
    if (convertExtractedCover(
            *this, coverImageHref, outputPath, "thumbnail",
            [targetWidth, height](HalFile& source, HalFile& output) {
              return JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(source, output, targetWidth, height);
            },
            [targetWidth, height](HalFile& source, HalFile& output) {
              return PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(source, output, targetWidth, height);
            })) {
      return true;
    }
  }
  if (convertOverrideCover(
          *this, outputPath, "thumbnail",
          [targetWidth, height](HalFile& source, HalFile& output) {
            return JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(source, output, targetWidth, height);
          },
          [targetWidth, height](HalFile& source, HalFile& output) {
            return PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(source, output, targetWidth, height);
          })) {
    return true;
  }

  // Write an empty bmp file to avoid generation attempts in the future
  HalFile thumbBmp;
  Storage.openFileForWrite("EBP", getThumbBmpPath(height), thumbBmp);
  return false;
}

uint8_t* Epub::readItemContentsToBytes(const std::string& itemHref, size_t* size, const bool trailingNullByte) const {
  if (itemHref.empty()) {
    LOG_DBG("EBP", "Failed to read item, empty href");
    return nullptr;
  }

  const std::string path = FsHelpers::normalisePath(itemHref);

  const auto content = ZipFile(filepath).readFileToMemory(path.c_str(), size, trailingNullByte);
  if (!content) {
    LOG_DBG("EBP", "Failed to read item %s", path.c_str());
    return nullptr;
  }

  return content;
}

bool Epub::readItemContentsToStream(const std::string& itemHref, Print& out, const size_t chunkSize,
                                    const bool allowEarlyStop) const {
  if (itemHref.empty()) {
    LOG_DBG("EBP", "Failed to read item, empty href");
    return false;
  }

  const std::string path = FsHelpers::normalisePath(itemHref);
  return ZipFile(filepath).readFileToStream(path.c_str(), out, chunkSize, allowEarlyStop);
}

bool Epub::extractItemToFile(const std::string& itemHref, const std::string& destPath) const {
  HalFile out;
  if (!Storage.openFileForWrite("EBP", destPath, out)) {
    return false;
  }
  // Large images dominate lazy extraction. Match the section streamer size to
  // halve SD read/write calls while adding only 8 KB of transient ZIP buffers.
  const bool ok = readItemContentsToStream(itemHref, out, 8192);
  out.flush();
  out.close();
  if (!ok) {
    Storage.remove(destPath.c_str());
  }
  return ok;
}

bool Epub::getItemSize(const std::string& itemHref, size_t* size) const {
  const std::string path = FsHelpers::normalisePath(itemHref);
  return ZipFile(filepath).getInflatedFileSize(path.c_str(), size);
}

int Epub::getSpineItemsCount() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return 0;
  }
  return bookMetadataCache->getSpineCount();
}

size_t Epub::getCumulativeSpineItemSize(const int spineIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return 0;
  }
  uint32_t cumulativeSize = 0;
  if (bookMetadataCache->getCumulativeSize(spineIndex, cumulativeSize)) return cumulativeSize;
  return getSpineItem(spineIndex).cumulativeSize;
}

BookMetadataCache::SpineEntry Epub::getSpineItem(const int spineIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR("EBP", "getSpineItem called but cache not loaded");
    return {};
  }

  if (spineIndex < 0 || spineIndex >= bookMetadataCache->getSpineCount()) {
    LOG_ERR("EBP", "getSpineItem index:%d is out of range", spineIndex);
    return bookMetadataCache->getSpineEntry(0);
  }

  return bookMetadataCache->getSpineEntry(spineIndex);
}

BookMetadataCache::TocEntry Epub::getTocItem(const int tocIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_DBG("EBP", "getTocItem called but cache not loaded");
    return {};
  }

  if (tocIndex < 0 || tocIndex >= bookMetadataCache->getTocCount()) {
    LOG_DBG("EBP", "getTocItem index:%d is out of range", tocIndex);
    return {};
  }

  return bookMetadataCache->getTocEntry(tocIndex);
}

int Epub::getTocItemsCount() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return 0;
  }

  return bookMetadataCache->getTocCount();
}

// work out the section index for a toc index
int Epub::getSpineIndexForTocIndex(const int tocIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR("EBP", "getSpineIndexForTocIndex called but cache not loaded");
    return 0;
  }

  if (tocIndex < 0 || tocIndex >= bookMetadataCache->getTocCount()) {
    LOG_ERR("EBP", "getSpineIndexForTocIndex: tocIndex %d out of range", tocIndex);
    return 0;
  }

  const int spineIndex = bookMetadataCache->getTocEntry(tocIndex).spineIndex;
  if (spineIndex < 0) {
    LOG_DBG("EBP", "Section not found for TOC index %d", tocIndex);
    return 0;
  }

  return spineIndex;
}

int Epub::getTocIndexForSpineIndex(const int spineIndex) const { return getSpineItem(spineIndex).tocIndex; }

size_t Epub::getBookSize() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded() || bookMetadataCache->getSpineCount() == 0) {
    return 0;
  }
  return getCumulativeSpineItemSize(getSpineItemsCount() - 1);
}

int Epub::getSpineIndexForTextReference() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR("EBP", "getSpineIndexForTextReference called but cache not loaded");
    return 0;
  }
  LOG_DBG("EBP", "Core Metadata: cover(%d)=%s, textReference(%d)=%s",
          bookMetadataCache->coreMetadata.coverItemHref.size(), bookMetadataCache->coreMetadata.coverItemHref.c_str(),
          bookMetadataCache->coreMetadata.textReferenceHref.size(),
          bookMetadataCache->coreMetadata.textReferenceHref.c_str());

  if (bookMetadataCache->coreMetadata.textReferenceHref.empty()) {
    // there was no textReference in epub, so we return 0 (the first chapter)
    return 0;
  }

  // loop through spine items to get the correct index matching the text href
  for (size_t i = 0; i < getSpineItemsCount(); i++) {
    if (getSpineItem(i).href == bookMetadataCache->coreMetadata.textReferenceHref) {
      LOG_DBG("EBP", "Text reference %s found at index %d", bookMetadataCache->coreMetadata.textReferenceHref.c_str(),
              i);
      return i;
    }
  }
  // This should not happen, as we checked for empty textReferenceHref earlier
  LOG_DBG("EBP", "Section not found for text reference");
  return 0;
}

// Calculate progress in book (returns 0.0-1.0)
float Epub::calculateProgress(const int currentSpineIndex, const float currentSpineRead) const {
  const size_t bookSize = getBookSize();
  if (bookSize == 0) {
    return 0.0f;
  }
  const size_t prevChapterSize = (currentSpineIndex >= 1) ? getCumulativeSpineItemSize(currentSpineIndex - 1) : 0;
  const size_t curChapterSize = getCumulativeSpineItemSize(currentSpineIndex) - prevChapterSize;
  const float sectionProgSize = currentSpineRead * static_cast<float>(curChapterSize);
  const float totalProgress = static_cast<float>(prevChapterSize) + sectionProgSize;
  return totalProgress / static_cast<float>(bookSize);
}

int Epub::resolveHrefToSpineIndex(const std::string& href) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return -1;

  // Split before decoding so escaped '#' characters in filenames stay part of the path.
  const size_t hashPos = href.find('#');
  const std::string rawTarget = hashPos != std::string::npos ? href.substr(0, hashPos) : href;
  const std::string target = FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(rawTarget));

  // Same-file reference (anchor-only)
  if (target.empty()) return -1;

  // Extract just the filename for comparison
  size_t targetSlash = target.find_last_of('/');
  std::string targetFilename = (targetSlash != std::string::npos) ? target.substr(targetSlash + 1) : target;

  for (int i = 0; i < getSpineItemsCount(); i++) {
    const auto& spineHref = getSpineItem(i).href;
    // Try exact match first
    if (spineHref == target) return i;
    // Then filename-only match
    size_t spineSlash = spineHref.find_last_of('/');
    std::string spineFilename = (spineSlash != std::string::npos) ? spineHref.substr(spineSlash + 1) : spineHref;
    if (spineFilename == targetFilename) return i;
  }
  return -1;
}
