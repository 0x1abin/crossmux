#pragma once

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace WeReadXhtmlCodec {

bool writeLiteral(HalFile& output, const char* text);
bool writeXmlText(HalFile& output, const char* text);

bool sanitizeChapter(const std::string& inputPath, const std::string& outputPath, const std::string& imageIndexPath,
                     uint32_t chapterIndex, const char* title, bool plainText, uint8_t* readBuffer,
                     size_t readBufferSize, char* tagBuffer, size_t tagBufferSize);

bool visibleToNativeOffset(const std::string& path, uint32_t visibleOffset, uint32_t& nativeOffset);
bool nativeToVisibleOffset(const std::string& path, uint32_t nativeOffset, uint32_t& visibleOffset);

}  // namespace WeReadXhtmlCodec
