"""Compile real statistics types and network release with firmware exception flags."""

from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest

from test_reading_ui_regressions import method

ROOT = Path(__file__).resolve().parents[2]


class ReadingStatsNetworkMemoryTest(unittest.TestCase):
    def test_release_capacity_only_after_successful_save(self):
        header = (ROOT / 'src/ReadingStatsStore.h').read_text()
        types = header[header.index('struct ReadingDayStats'):header.index('class ReadingStatsStore;')]
        source = (ROOT / 'src/ReadingStatsStore.cpp').read_text()
        release = method(source, 'bool ReadingStatsStore::releaseMemoryForNetwork()')
        program = r'''
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
unsigned long millis() { return 123; }
''' + types + r'''
struct ReadingStatsStore {
  std::vector<ReadingBookStats> books;
  std::vector<ReadingDayStats> legacyReadingDays, readingDays;
  std::vector<ReadingSessionLogEntry> sessionLog;
  struct { bool active = true; } activeSession;
  ReadingSessionSnapshot lastSessionSnapshot;
  uint32_t sessionSerialCounter = 9;
  bool dirty = true, saveOk = false;
  unsigned long dirtySinceMs = 1, lastSaveAttemptMs = 1;
  int saves = 0, ends = 0, invalidations = 0;
  void endSession() { activeSession.active = false; ++ends; }
  bool saveToFile() {
    ++saves;
    assert(books.size() == 1 && books[0].bookId == "kept-book");
    assert(!sessionLog.empty());
    return saveOk;
  }
  void invalidateSummaryCache() { ++invalidations; }
  bool releaseMemoryForNetwork();
};
''' + release + r'''
int main() {
  ReadingStatsStore store;
  store.books.reserve(32);
  store.legacyReadingDays.reserve(128);
  store.readingDays.reserve(128);
  store.sessionLog.reserve(512);
  store.books.emplace_back().bookId = "kept-book";
  store.legacyReadingDays.push_back({1, 10});
  store.readingDays.push_back({2, 20});
  store.sessionLog.push_back({2, 20});
  // Failed persistence must retain contents AND allocations for a later retry.
  assert(!store.releaseMemoryForNetwork());
  assert(store.saves == 1 && store.ends == 1 && store.invalidations == 0);
  assert(store.books.size() == 1 && store.books.capacity() == 32);
  assert(store.legacyReadingDays.capacity() == 128 && store.readingDays.capacity() == 128);
  assert(store.sessionLog.size() == 1 && store.sessionLog.capacity() == 512 && store.dirty);
  store.saveOk = true;
  assert(store.releaseMemoryForNetwork());
  assert(store.saves == 2 && store.ends == 1 && store.invalidations == 1);
  assert(store.books.empty() && store.books.capacity() == 0);
  assert(store.legacyReadingDays.empty() && store.legacyReadingDays.capacity() == 0);
  assert(store.readingDays.empty() && store.readingDays.capacity() == 0);
  assert(store.sessionLog.empty() && store.sessionLog.capacity() == 0);
  assert(!store.dirty && store.dirtySinceMs == 0 && store.sessionSerialCounter == 0);
  assert(store.lastSaveAttemptMs == 123);
  // Repeated release must neither save an empty store nor allocate new buffers.
  assert(store.releaseMemoryForNetwork());
  assert(store.saves == 2 && store.books.capacity() == 0 && store.sessionLog.capacity() == 0);
}
'''
        with tempfile.TemporaryDirectory(prefix='stats-network-') as directory:
            cpp = Path(directory) / 'check.cpp'
            binary = Path(directory) / 'check'
            cpp.write_text(program)
            for flags in ([], ['-fno-exceptions']):
                with self.subTest(flags=flags):
                    subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
                        '-std=c++20', '-Wall', '-Wextra', '-Werror', *flags,
                        str(cpp), '-o', str(binary)], check=True)
                    subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
