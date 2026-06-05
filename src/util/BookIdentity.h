#pragma once

#include <string>

namespace BookIdentity {

std::string normalizePath(const std::string& path);
std::string getFileExtensionLower(const std::string& path);

std::string calculateContentBookId(const std::string& path);
std::string resolveStableBookId(const std::string& path);

bool isLegacyBookId(const std::string& bookId);

std::string getStableDataDir(const std::string& bookId);
std::string getStableDataFilePath(const std::string& bookId, const std::string& filename);
void ensureStableDataDir(const std::string& bookId);

}  // namespace BookIdentity
