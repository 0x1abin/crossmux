#include <HalStorage.h>
#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "SystemSettingsReset.h"

namespace {

constexpr const char* RESET_PATHS[] = {
    "/.crosspoint/settings.json",    "/.crosspoint/settings.json.tmp", "/.crosspoint/settings.bin",
    "/.crosspoint/settings.bin.bak", "/.crosspoint/language.bin",      "/.crosspoint/language.bin.bak",
    "/.crosspoint/wifi.json",        "/.crosspoint/wifi.json.tmp",     "/.crosspoint/opds.json",
    "/.crosspoint/opds.json.tmp",    "/.crosspoint/koreader.json",     "/.crosspoint/koreader.json.tmp",
};

constexpr const char* PRESERVED_PATHS[] = {
    "/.crosspoint/state.json",          "/.crosspoint/recent.json", "/.crosspoint/reading_stats.json",
    "/.crosspoint/bookmarks/book.json", "/Books/book.epub",
};

class SystemSettingsResetTest : public ::testing::Test {
 protected:
  void SetUp() override {
    static std::atomic<unsigned> serial{0};
    root_ = std::filesystem::temp_directory_path() / ("crossmux-settings-reset-" + std::to_string(serial.fetch_add(1)));
    std::error_code error;
    std::filesystem::remove_all(root_, error);
    ASSERT_TRUE(std::filesystem::create_directories(root_, error));
    ASSERT_FALSE(error);
    ASSERT_EQ(setenv("CROSSPOINT_SIM_SD", root_.c_str(), 1), 0);
    ASSERT_TRUE(Storage.begin());
  }

  void TearDown() override {
    unsetenv("CROSSPOINT_SIM_SD");
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  std::filesystem::path hostPath(const char* sdPath) const {
    while (*sdPath == '/') ++sdPath;
    return root_ / sdPath;
  }

  void writeFile(const char* path, const char* contents = "data") {
    const auto target = hostPath(path);
    ASSERT_TRUE(std::filesystem::create_directories(target.parent_path()) ||
                std::filesystem::exists(target.parent_path()));
    std::ofstream output(target, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.good());
    output << contents;
    ASSERT_TRUE(output.good());
  }

  std::filesystem::path root_;
};

TEST_F(SystemSettingsResetTest, ClearsSettingsAndCredentialsButPreservesReadingData) {
  for (const char* path : RESET_PATHS) writeFile(path);
  writeFile("/.crosspoint/settings.json", R"({"contentProfile":1,"onboardingVersion":1})");
  for (const char* path : PRESERVED_PATHS) writeFile(path);

  ASSERT_TRUE(systemSettingsReset::clearPersistedSettings());

  for (const char* path : RESET_PATHS) EXPECT_FALSE(Storage.exists(path));
  for (const char* path : PRESERVED_PATHS) EXPECT_TRUE(Storage.exists(path));
}

TEST_F(SystemSettingsResetTest, ReportsFailureAndContinuesClearingLaterFiles) {
  const auto blockedPath = hostPath("/.crosspoint/wifi.json");
  ASSERT_TRUE(std::filesystem::create_directories(blockedPath / "child"));
  writeFile("/.crosspoint/opds.json");

  EXPECT_FALSE(systemSettingsReset::clearPersistedSettings());
  EXPECT_TRUE(Storage.exists("/.crosspoint/wifi.json"));
  EXPECT_FALSE(Storage.exists("/.crosspoint/opds.json"));
}

}  // namespace
