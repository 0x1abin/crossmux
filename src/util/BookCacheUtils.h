#pragma once

#include <string>

// Clears the reading cache for a book file if its extension is recognised
// (EPUB, XTC, TXT, or Markdown). Does nothing for other file types.
bool clearBookCache(const std::string& path);

// Keep saved positions and user data when replacing a reflowable book.
bool clearBookDerivedCache(const std::string& path);

// Returns the path-keyed cache directory, or an empty string for non-book files.
std::string bookCachePath(const std::string& path);

enum class BookMoveResult { NotMoved, Complete, MovedDataIncomplete };
// Coordinate backing paths, saved data, rebuildable caches and references, including folder overwrite rollback.
BookMoveResult moveBookFile(const std::string& oldPath, const std::string& newPath, bool overwrite = false);

// Returns true if the directory name matches a book cache entry.
bool isBookCacheDirectoryName(const char* name);

// Delete a local book and its local clipping state after the file is removed.
bool removeBookFile(const std::string& path);
