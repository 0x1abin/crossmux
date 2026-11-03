#include <ClippingStore.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include "CrossPointState.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "util/BookCacheUtils.h"
#include "util/BookmarkUtil.h"

namespace {
using Result = ClippingStore::AddResult;
Result add(ClippingStore& store, const std::string& text = "one", const char* title = "Chapter") {
  return store.addClipping(0, 0, 0, 1, 0, 0, 1, title, 0, text, 1, 0, 3);
}
std::string storePath() {
  for (const auto& [path, node] : fake::files)
    if (path.ends_with(".bin")) return path;
  return "";
}
}  // namespace

TEST(ClippingStore, FailedLoadCannotPublishOrOverwriteValidPrefix) {
  fake::reset();
  ClippingStore store;
  ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author", "epub"));
  ASSERT_EQ(add(store), Result::Added);
  ASSERT_EQ(add(store, "two"), Result::Added);
  store.unload();
  auto& bytes = fake::files.at(storePath())->bytes;
  bytes.pop_back();
  const auto damaged = bytes;
  EXPECT_FALSE(store.loadForBook("/book.epub", "Book", "Author", "epub"));
  EXPECT_EQ(store.clippingCount(), 0u);
  EXPECT_EQ(add(store), Result::SaveFailed);
  EXPECT_FALSE(store.saveToFile());
  EXPECT_FALSE(store.prepareSync());
  store.unload();
  EXPECT_EQ(bytes, damaged);
  ASSERT_TRUE(store.loadForBook("/other.epub", "Other", "", "epub"));
  ASSERT_EQ(add(store), Result::Added);
  EXPECT_FALSE(store.loadForBook("/bad.txt", "", "", "unsupported"));
  EXPECT_EQ(store.clippingCount(), 0u);
  EXPECT_EQ(add(store), Result::SaveFailed);
}

TEST(ClippingStore, AllocationFailureKeepsOldIndexAndUnloadReleasesIt) {
  fake::reset();
  ClippingStore store;
  ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author", "epub"));
  for (int i = 0; i < 16; ++i) ASSERT_EQ(add(store), Result::Added);
  fake::failAlloc = 0;
  EXPECT_EQ(add(store), Result::SaveFailed);
  EXPECT_EQ(store.clippingCount(), 16u);
  std::string text;
  EXPECT_TRUE(store.readClippingText(3, text));
  EXPECT_EQ(text, "one");
  store.unload();
  EXPECT_EQ(store.clippingAt(0), nullptr);
  EXPECT_TRUE(store.loadForBook("/book.epub", "Book", "Author", "epub"));
  EXPECT_EQ(store.clippingCount(), 16u);
}

TEST(ClippingStore, CountLimitAndHeaderLimitsRoundTrip) {
  fake::reset();
  ClippingStore store;
  ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author", "epub"));
  for (unsigned i = 0; i < CLIPPING_MAX_PER_BOOK - 1; ++i) ASSERT_EQ(add(store), Result::Added);
  store.unload();
  ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author", "epub"));
  EXPECT_EQ(add(store), Result::Added);
  EXPECT_EQ(add(store), Result::LimitReached);
  store.unload();
  ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author", "epub"));
  EXPECT_EQ(store.clippingCount(), CLIPPING_MAX_PER_BOOK);
  const auto original = fake::files.at(storePath())->bytes;
  store.unload();
  ASSERT_TRUE(store.loadForBook("/book.epub", std::string(4097, 'x'), "Author", "epub"));
  EXPECT_FALSE(store.removeClippingAt(0));
  store.unload();
  EXPECT_EQ(fake::files.at(storePath())->bytes, original);
}

TEST(ClippingStore, GrowthKeepsRecordsInPlaceAndDeletionRollsBackAcrossBlocks) {
  fake::reset();
  ClippingStore store;
  ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author", "epub"));
  const Clipping* first = nullptr;
  for (unsigned i = 0; i < 33; ++i) {
    Clipping clipping;
    snprintf(clipping.id, sizeof(clipping.id), "clip-%u", i);
    const std::string text = std::to_string(i);
    clipping.textLength = text.size();
    ASSERT_TRUE(store.applyRemote(clipping, text, false));
    if (i == 0) first = store.clippingAt(0);
    EXPECT_EQ(store.clippingAt(0), first);
  }
  const auto path = storePath();
  const auto original = fake::files.at(path)->bytes;
  // The journal succeeds; the store rewrite fails after records have shifted.
  fake::failWrite = 1;
  EXPECT_FALSE(store.removeClippingAt(15));
  ASSERT_EQ(store.clippingCount(), 33u);
  EXPECT_EQ(fake::files.at(path)->bytes, original);
  for (unsigned i = 0; i < 33; ++i) {
    std::string text;
    ASSERT_TRUE(store.readClippingText(i, text));
    EXPECT_EQ(text, std::to_string(i));
  }
  const Clipping removed = *store.clippingAt(15);
  fake::failWrite = 0;
  EXPECT_FALSE(store.applyRemote(removed, "", true));
  EXPECT_EQ(store.clippingCount(), 33u);
  ASSERT_TRUE(store.applyRemote(removed, "", true));
  ASSERT_TRUE(store.removeClippingAt(15));
  store.unload();
  // Fail partway through allocating blocks while loading, then retry.
  fake::failAlloc = 1;
  EXPECT_FALSE(store.loadForBook("/book.epub", "Book", "Author", "epub"));
  EXPECT_EQ(store.clippingCount(), 0u);
  ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author", "epub"));
  ASSERT_EQ(store.clippingCount(), 31u);
  for (unsigned i = 0; i < 31; ++i) {
    std::string text;
    ASSERT_TRUE(store.readClippingText(i, text));
    EXPECT_EQ(text, std::to_string(i < 15 ? i : i + 2));
  }
}

TEST(ClippingStore, OffsetAllocationFailureDoesNotCreateTempFile) {
  fake::reset();
  ClippingStore store;
  ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author", "epub"));
  ASSERT_EQ(add(store), Result::Added);
  const auto path = storePath();
  const auto original = fake::files.at(path)->bytes;
  fake::failAlloc = 0;
  EXPECT_EQ(add(store, "two"), Result::SaveFailed);
  EXPECT_EQ(store.clippingCount(), 1u);
  EXPECT_EQ(fake::files.at(path)->bytes, original);
  EXPECT_FALSE(Storage.exists((path + ".tmp").c_str()));
  EXPECT_EQ(add(store, "two"), Result::Added);
}

TEST(ClippingStore, ShortDeletionAppendRollsBackAndRetryCanSync) {
  for (const bool failedRollback : {false, true}) {
    fake::reset();
    ClippingStore store;
    ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author", "epub"));
    ASSERT_EQ(add(store), Result::Added);
    ASSERT_EQ(add(store, "two"), Result::Added);
    ASSERT_TRUE(store.prepareSync());
    const std::string firstId = store.clippingAt(0)->id;
    const std::string secondId = store.clippingAt(1)->id;
    ASSERT_TRUE(store.removeClippingAt(0));
    const auto journalPath = storePath() + ".deleted";
    const auto original = fake::files.at(journalPath)->bytes;
    fake::shortWrite = 1;
    if (failedRollback) fake::failTruncate = 0;
    EXPECT_FALSE(store.removeClippingAt(0));
    ASSERT_EQ(store.clippingCount(), 1u);
    if (failedRollback) {
      EXPECT_EQ(fake::files.at(journalPath)->bytes.size(), original.size() + 1);
    } else {
      EXPECT_EQ(fake::files.at(journalPath)->bytes, original);
    }
    ASSERT_TRUE(store.removeClippingAt(0));
    store.unload();
    ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author", "epub"));
    uint32_t offset = 0;
    char id[65];
    ASSERT_TRUE(store.nextDeletion(offset, id));
    EXPECT_EQ(id, firstId);
    ASSERT_TRUE(store.nextDeletion(offset, id));
    EXPECT_EQ(id, secondId);
    ASSERT_TRUE(store.nextDeletion(offset, id));
    EXPECT_EQ(id[0], '\0');
    EXPECT_EQ(fake::files.at(journalPath)->bytes.size(), 130u);
    EXPECT_TRUE(store.finishDeletions());
  }
}

TEST(ClippingStore, InterruptedDeletionTailRecoversBeforeReading) {
  fake::reset();
  ClippingStore store;
  ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author", "epub"));
  ASSERT_EQ(add(store), Result::Added);
  ASSERT_TRUE(store.prepareSync());
  const std::string expectedId = store.clippingAt(0)->id;
  ASSERT_TRUE(store.removeClippingAt(0));
  const auto journalPath = storePath() + ".deleted";
  const auto original = fake::files.at(journalPath)->bytes;
  fake::files.at(journalPath)->bytes.push_back('x');
  store.unload();
  ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author", "epub"));
  uint32_t offset = 0;
  char id[65];
  fake::failTruncate = 0;
  EXPECT_FALSE(store.nextDeletion(offset, id));
  EXPECT_EQ(offset, 0u);
  ASSERT_TRUE(store.nextDeletion(offset, id));
  EXPECT_EQ(id, expectedId);
  EXPECT_EQ(fake::files.at(journalPath)->bytes, original);
  ASSERT_TRUE(store.nextDeletion(offset, id));
  EXPECT_EQ(id[0], '\0');
  ASSERT_TRUE(store.finishDeletions());
  fake::add(journalPath, "x");
  offset = 0;
  ASSERT_TRUE(store.nextDeletion(offset, id));
  EXPECT_EQ(id[0], '\0');
  EXPECT_TRUE(fake::files.at(journalPath)->bytes.empty());
}

TEST(ClippingStore, Utf8TitleAndFailedDeletionPreserveRecord) {
  fake::reset();
  ClippingStore store;
  ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author", "epub"));
  ASSERT_EQ(add(store, "one", (std::string(46, 'x') + "가").c_str()), Result::Added);
  EXPECT_EQ(std::string(store.clippingAt(0)->chapterTitle), std::string(46, 'x'));
  fake::failWrite = 0;
  EXPECT_FALSE(store.removeClippingAt(0));
  ASSERT_EQ(store.clippingCount(), 1u);
  std::string text;
  EXPECT_TRUE(store.readClippingText(0, text));
  EXPECT_EQ(text, "one");
}

TEST(ClippingStore, MovesFolderWithClipsAndDeletionJournalAndRollsBackFailure) {
  for (const int failure : {-1, 0, 1, 2, 3}) {
    fake::reset();
    fake::add("/books/sub/book.epub");
    ClippingStore store;
    ASSERT_TRUE(store.loadForBook("/books/sub/book.epub", "Book", "Author", "epub"));
    ASSERT_EQ(add(store), Result::Added);
    ASSERT_EQ(add(store, "two"), Result::Added);
    ASSERT_TRUE(store.prepareSync());
    ASSERT_TRUE(store.removeClippingAt(0));
    store.unload();
    const auto oldStore = storePath();
    const auto original = fake::files.at(oldStore)->bytes;
    const auto deleted = fake::files.at(oldStore + ".deleted")->bytes;
    fake::failRename = failure;
    const bool moved = moveBookFile("/books", "/renamed") != BookMoveResult::NotMoved;
    if (moved) {
      EXPECT_FALSE(Storage.exists("/books/sub/book.epub"));
      EXPECT_TRUE(Storage.exists("/renamed/sub/book.epub"));
      EXPECT_FALSE(Storage.exists(oldStore.c_str()));
      ASSERT_TRUE(store.loadForBook("/renamed/sub/book.epub", "", "", "epub"));
      EXPECT_EQ(store.clippingCount(), 1u);
      std::string text;
      EXPECT_TRUE(store.readClippingText(0, text));
      EXPECT_EQ(text, "two");
      uint32_t offset = 0;
      char id[65];
      EXPECT_TRUE(store.nextDeletion(offset, id));
      EXPECT_NE(id[0], 0);
    } else {
      EXPECT_TRUE(Storage.exists("/books/sub/book.epub"));
      EXPECT_FALSE(Storage.exists("/renamed/sub/book.epub"));
      EXPECT_EQ(fake::files.at(oldStore)->bytes, original);
      EXPECT_EQ(fake::files.at(oldStore + ".deleted")->bytes, deleted);
    }
  }
}

TEST(ClippingStore, MoveRefusesExistingDestinationHistoryAndDeleteCleansSidecars) {
  fake::reset();
  fake::add("/one.epub");
  ClippingStore store;
  ASSERT_TRUE(store.loadForBook("/one.epub", "One", "", "epub"));
  ASSERT_EQ(add(store), Result::Added);
  store.unload();
  ASSERT_TRUE(store.loadForBook("/two.epub", "Two", "", "epub"));
  ASSERT_EQ(add(store, "two"), Result::Added);
  store.unload();
  EXPECT_EQ(moveBookFile("/one.epub", "/two.epub"), BookMoveResult::NotMoved);
  EXPECT_TRUE(Storage.exists("/one.epub"));
  EXPECT_TRUE(ClippingStore::deleteForFilePath("/two.epub", "epub"));
  EXPECT_EQ(moveBookFile("/one.epub", "/two.epub"), BookMoveResult::Complete);
  const auto path = storePath();
  fake::add(path + ".deleted", std::string(65, 'x'));
  fake::add(path + ".bak");
  fake::add(path + ".tmp");
  EXPECT_TRUE(ClippingStore::deleteForFilePath("/two.epub", "epub"));
  EXPECT_FALSE(Storage.exists(path.c_str()));
  EXPECT_FALSE(Storage.exists((path + ".deleted").c_str()));
  EXPECT_FALSE(Storage.exists((path + ".bak").c_str()));
  EXPECT_FALSE(Storage.exists((path + ".tmp").c_str()));
}

TEST(ClippingStore, RelocateAfterBackingMoveIsIdempotentAndDoesNotOverwriteDestination) {
  fake::reset();
  ClippingStore store;
  ASSERT_TRUE(store.loadForBook("/old.epub", "", "", "epub"));
  ASSERT_EQ(add(store), Result::Added);
  store.unload();
  const auto oldStore = storePath();
  EXPECT_TRUE(ClippingStore::relocateForFilePath("/old.epub", "/new.epub"));
  EXPECT_FALSE(Storage.exists(oldStore.c_str()));
  EXPECT_TRUE(ClippingStore::relocateForFilePath("/old.epub", "/new.epub"));
  ASSERT_TRUE(store.loadForBook("/new.epub", "", "", "epub"));
  EXPECT_EQ(store.clippingCount(), 1u);
  std::string movedText;
  ASSERT_TRUE(store.readClippingText(0, movedText));
  EXPECT_EQ(movedText, "one");
  store.unload();
  const auto newStore = storePath();
  const auto destinationBytes = fake::files.at(newStore)->bytes;
  ASSERT_TRUE(store.loadForBook("/old.epub", "", "", "epub"));
  ASSERT_EQ(add(store, "other"), Result::Added);
  store.unload();
  const auto bytes = fake::files.at(oldStore)->bytes;
  EXPECT_FALSE(ClippingStore::relocateForFilePath("/old.epub", "/new.epub"));
  EXPECT_EQ(fake::files.at(oldStore)->bytes, bytes);
  EXPECT_EQ(fake::files.at(newStore)->bytes, destinationBytes);
}

namespace {
void resetReferences() {
  hostState = {};
  hostRecent = {};
  hostStats = {};
}
void savedBook(const std::string& path, const std::string& text) {
  fake::add(path, text);
  fake::add(path + ".key", text + "-key");
  fake::add(path + ".rights", text + "-rights");
  fake::add(bookCachePath(path) + "/progress.bin", text + "-progress");
  fake::add(bookCachePath(path) + "/links.bin", text + "-links");
  fake::add(BookmarkUtil::getBookmarkPath(path), text + "-bookmark");
  ClippingStore store;
  EXPECT_TRUE(store.loadForBook(path, text, "", "epub"));
  EXPECT_EQ(add(store, text), Result::Added);
  store.unload();
}
auto snapshot() {
  std::map<std::string, std::pair<bool, std::vector<uint8_t>>> result;
  for (const auto& [path, node] : fake::files) result[path] = {node->directory, node->bytes};
  return result;
}
}  // namespace

TEST(BookMove, ProtectedWholeBookOverwriteRollsBackEveryRenameFailure) {
  unsigned failures = 0, successes = 0;
  for (int failure = -1; failure < 32; ++failure) {
    fake::reset();
    resetReferences();
    savedBook("/from.epub", "source");
    savedBook("/to.epub", "target");
    const auto original = snapshot();
    fake::failRename = failure;
    const auto result = moveBookFile("/from.epub", "/to.epub", true);
    if (result == BookMoveResult::NotMoved) {
      ++failures;
      EXPECT_EQ(snapshot(), original) << failure;
    } else {
      ++successes;
      EXPECT_FALSE(Storage.exists("/from.epub"));
      EXPECT_EQ(fake::files.at("/to.epub")->bytes, original.at("/from.epub").second);
      EXPECT_EQ(fake::files.at("/to.epub.key")->bytes, original.at("/from.epub.key").second);
      EXPECT_EQ(fake::files.at((bookCachePath("/to.epub") + "/progress.bin"))->bytes,
                original.at(bookCachePath("/from.epub") + "/progress.bin").second);
      ClippingStore store;
      ASSERT_TRUE(store.loadForBook("/to.epub", "", "", "epub"));
      std::string text;
      ASSERT_TRUE(store.readClippingText(0, text));
      EXPECT_EQ(text, "source");
      store.unload();
    }
  }
  EXPECT_GT(failures, 10u);
  EXPECT_GT(successes, 0u);
}

TEST(BookMove, DeleteRetiresKeysAndHistoryAndAllowsSameNameMove) {
  fake::reset();
  resetReferences();
  savedBook("/old.epub", "old");
  savedBook("/new.epub", "new");
  EXPECT_TRUE(removeBookFile("/old.epub"));
  EXPECT_FALSE(Storage.exists("/old.epub.key"));
  EXPECT_FALSE(Storage.exists("/old.epub.rights"));
  EXPECT_EQ(moveBookFile("/new.epub", "/old.epub"), BookMoveResult::Complete);
}

TEST(BookMove, RebuildableCacheFailureStillRepairsEveryReference) {
  fake::reset();
  resetReferences();
  fake::add("/from.epub");
  fake::add(bookCachePath("/from.epub") + "/cover.bmp");
  hostRecent.books["/from.epub"] = "source";
  hostRecent.fail = true;
  hostStats.books["/from.epub"] = {bookCachePath("/from.epub") + "/cover.bmp"};
  hostState.openEpubPath = "/from.epub";
  fake::failRename = 0;
  EXPECT_EQ(moveBookFile("/from.epub", "/to.epub"), BookMoveResult::MovedDataIncomplete);
  EXPECT_TRUE(Storage.exists("/to.epub"));
  EXPECT_GT(hostRecent.updates, 0);
  EXPECT_GT(hostStats.updates, 0);
  EXPECT_TRUE(hostStats.books.count("/to.epub"));
  EXPECT_EQ(hostState.openEpubPath, "/to.epub");
}

TEST(BookMove, DirectorySharesOneTransactionForProtectedChildBooksAndReferences) {
  fake::reset();
  resetReferences();
  savedBook("/books/sub/a.epub", "one");
  savedBook("/books/sub/b.epub", "two");
  hostRecent.books["/books/sub/a.epub"] = "one";
  hostStats.books["/books/sub/b.epub"] = {};
  hostState.openEpubPath = "/books/sub/b.epub";
  EXPECT_EQ(moveBookFile("/books", "/renamed"), BookMoveResult::Complete);
  EXPECT_TRUE(Storage.exists("/renamed/sub/a.epub.key"));
  EXPECT_TRUE(Storage.exists("/renamed/sub/b.epub.rights"));
  EXPECT_TRUE(Storage.exists((bookCachePath("/renamed/sub/b.epub") + "/progress.bin").c_str()));
  EXPECT_EQ(hostState.openEpubPath, "/renamed/sub/b.epub");
  EXPECT_TRUE(hostRecent.books.count("/renamed/sub/a.epub"));
  EXPECT_TRUE(hostStats.books.count("/renamed/sub/b.epub"));
}

TEST(BookMove, FolderWorkspaceAllocationFailureLeavesEverythingInPlace) {
  for (int fail : {0, 1}) {
    fake::reset();
    resetReferences();
    savedBook("/books/a.epub", "book");
    const auto original = snapshot();
    fake::failAlloc = fail;
    EXPECT_EQ(moveBookFile("/books", "/new"), BookMoveResult::NotMoved);
    EXPECT_EQ(snapshot(), original);
  }
}

TEST(BookMove, FailedDestinationRollbackRetainsItsRecoverableBundle) {
  bool retained = false;
  for (int failure = 0; failure < 24; ++failure) {
    fake::reset();
    resetReferences();
    savedBook("/from.epub", "source");
    savedBook("/to.epub", "target");
    const std::string backup = "/.crossmux-move-" + std::to_string(std::hash<std::string>{}("/to.epub")) + ".epub";
    fake::failRename = failure;
    fake::failRenamePath = backup;
    const auto moved = moveBookFile("/from.epub", "/to.epub", true);
    if (moved == BookMoveResult::NotMoved && Storage.exists(backup.c_str())) {
      retained = true;
      EXPECT_TRUE(Storage.exists("/from.epub"));
      EXPECT_TRUE(Storage.exists((backup + ".key").c_str()));
      EXPECT_TRUE(Storage.exists((bookCachePath(backup) + "/progress.bin").c_str()));
    }
  }
  EXPECT_TRUE(retained);
}

TEST(BookMove, DeleteFailureBeforeRetirementRestoresBookAndAllSavedData) {
  unsigned failures = 0;
  for (int failure = 0; failure < 12; ++failure) {
    fake::reset();
    resetReferences();
    savedBook("/book.epub", "book");
    const auto before = snapshot();
    fake::failRename = failure;
    if (!removeBookFile("/book.epub")) {
      ++failures;
      EXPECT_EQ(snapshot(), before);
    }
  }
  EXPECT_GT(failures, 5u);
}

TEST(BookMove, DirectoryDepthPreflightRejectsAnUnboundedWorkload) {
  fake::reset();
  resetReferences();
  std::string path = "/books";
  for (int i = 0; i < 33; ++i) path += "/sub";
  savedBook(path + "/book.epub", "book");
  const auto before = snapshot();
  EXPECT_EQ(moveBookFile("/books", "/moved"), BookMoveResult::NotMoved);
  EXPECT_EQ(snapshot(), before);
}

TEST(BookMove, LoadedClippingStoreFollowsMoveAndReleasesAfterDeletion) {
  fake::reset();
  resetReferences();
  fake::add("/loaded.epub");
  ASSERT_TRUE(CLIPPINGS.loadForBook("/loaded.epub", "Book", "", "epub"));
  ASSERT_EQ(add(CLIPPINGS), Result::Added);
  EXPECT_EQ(moveBookFile("/loaded.epub", "/moved.epub"), BookMoveResult::Complete);
  EXPECT_TRUE(CLIPPINGS.hasClippings());
  ASSERT_EQ(add(CLIPPINGS, "two"), Result::Added);
  EXPECT_TRUE(removeBookFile("/moved.epub"));
  EXPECT_FALSE(CLIPPINGS.hasClippings());
  EXPECT_EQ(add(CLIPPINGS, "after delete"), Result::SaveFailed);
}
