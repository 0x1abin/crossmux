#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "activities/plugins/PluginCatalogPaging.h"
#include "util/PluginLocations.h"

TEST(PluginManifestClassification, InstallingMarkerQuarantinesOtherwiseValidPlugins) {
  EXPECT_TRUE(PluginLocations::isDiscoverableEntry(false, false, true, false));
  EXPECT_TRUE(PluginLocations::isDiscoverableEntry(false, true, false, false));
  EXPECT_TRUE(PluginLocations::isDiscoverableEntry(false, false, false, true));
  EXPECT_FALSE(PluginLocations::isDiscoverableEntry(true, true, true, true));
  EXPECT_FALSE(PluginLocations::isDiscoverableEntry(false, false, false, false));
}

TEST(PluginManifestClassification, DirectPluginBundlesMayReuseInstalledRoot) {
  EXPECT_TRUE(PluginLocations::shouldReuseInstalledBundleDir("/.crosspoint/plugins", "foo"));
  EXPECT_TRUE(PluginLocations::shouldReuseInstalledBundleDir("/plugins", "foo"));
  EXPECT_TRUE(PluginLocations::shouldReuseInstalledBundleDir("/.plugins", "foo"));

  EXPECT_FALSE(PluginLocations::shouldReuseInstalledBundleDir("/downloads", "foo"));
  EXPECT_FALSE(PluginLocations::shouldReuseInstalledBundleDir("/.crosspoint/plugins", "foo/assets"));
  EXPECT_FALSE(PluginLocations::shouldReuseInstalledBundleDir("/.crosspoint/plugins", ""));
}

TEST(PluginManifestClassification, BrowseUrlIsCatalog) {
  EXPECT_EQ(PluginLocations::classifyDeviceManifest(true, false), PluginLocations::DeviceKind::Catalog);
  EXPECT_EQ(PluginLocations::classifyDeviceManifest(true, true), PluginLocations::DeviceKind::Catalog);
}

TEST(PluginManifestClassification, EventsWithoutBrowseAreBackgroundOnly) {
  EXPECT_EQ(PluginLocations::classifyDeviceManifest(false, true), PluginLocations::DeviceKind::Background);
}

TEST(PluginManifestClassification, EmptyDeviceManifestIsNotOpenable) {
  EXPECT_EQ(PluginLocations::classifyDeviceManifest(false, false), PluginLocations::DeviceKind::None);
}

TEST(PluginManifestClassification, BackgroundManifestWithReadmeOpensInfo) {
  EXPECT_EQ(PluginLocations::pickerAction(PluginLocations::DeviceKind::Background, true),
            PluginLocations::PickerAction::Readme);
}

TEST(PluginManifestClassification, BackgroundManifestWithoutReadmeStaysInPicker) {
  EXPECT_EQ(PluginLocations::pickerAction(PluginLocations::DeviceKind::Background, false),
            PluginLocations::PickerAction::None);
}

TEST(PluginManifestClassification, WebOnlyPluginWithReadmeOpensInfo) {
  EXPECT_EQ(PluginLocations::pickerAction(PluginLocations::DeviceKind::None, true),
            PluginLocations::PickerAction::Readme);
  EXPECT_EQ(PluginLocations::pickerAction(PluginLocations::DeviceKind::None, false),
            PluginLocations::PickerAction::None);
}

TEST(PluginManifestClassification, CatalogOpensEvenWithReadme) {
  EXPECT_EQ(PluginLocations::pickerAction(PluginLocations::DeviceKind::Catalog, true),
            PluginLocations::PickerAction::Catalog);
}

TEST(PluginManifestClassification, OnlyANewerCatalogVersionIsAnUpdate) {
  EXPECT_TRUE(PluginLocations::isNewerVersion("1.1.0", "1.0.0"));
  EXPECT_TRUE(PluginLocations::isNewerVersion("0.1.10", "0.1.9"));
  EXPECT_TRUE(PluginLocations::isNewerVersion("2.0.0", "1.9.9"));
  EXPECT_FALSE(PluginLocations::isNewerVersion("1.0.0", "1.1.0"));  // no downgrade offers
  EXPECT_FALSE(PluginLocations::isNewerVersion("1.2.0", "1.2.0"));
  EXPECT_TRUE(PluginLocations::isNewerVersion("1.0.0", ""));  // installed copy without a version
  EXPECT_FALSE(PluginLocations::isNewerVersion("", "1.0.0"));
  EXPECT_FALSE(PluginLocations::isNewerVersion("1.2", "1.0.0"));  // catalog must be MAJOR.MINOR.PATCH
}

TEST(PluginManifestClassification, VersionsMustBeExactlyThreeDigitComponents) {
  for (const char* bad :
       {"1.2.3-beta", "1.2.3 ", "1.2.3.4", "-1.2.3", "+1.2.3", " 1.2.3", "1..3", "1.2.", "1.2.99999999999", "v1.2.3"}) {
    EXPECT_FALSE(PluginLocations::isNewerVersion(bad, "0.0.1")) << bad;  // never offered
    EXPECT_TRUE(PluginLocations::isNewerVersion("0.0.1", bad)) << bad;   // installed treated as unversioned
  }
  EXPECT_TRUE(PluginLocations::isNewerVersion("4294967295.0.0", "1.0.0"));  // UINT32_MAX still fits
}

TEST(PluginCatalogPaging, ServerPaginationCanBeSpecifiedInUrlBodyOrHeaders) {
  using plugincatalogpaging::isStaticFeed;
  std::map<std::string, std::string> headers;
  EXPECT_TRUE(isStaticFeed("https://host/static.json", "", headers));
  EXPECT_FALSE(isStaticFeed("https://host/?page={page}", "", headers));
  EXPECT_FALSE(isStaticFeed("https://host", "page={page}", headers));
  headers["X-Page"] = "{page}";
  EXPECT_FALSE(isStaticFeed("https://host", "", headers));
}

TEST(PluginCatalogPaging, StaticPagesCountOnlyCompatibleEntriesAndLookAhead) {
  // Titled compatible rows are 0,2,5,8,9: pages contain {0,2}, {5,8}, {9}.
  const std::vector<bool> compatible{true, false, true, false, false, true, false, false, true, true};
  const std::vector<std::vector<size_t>> expected{{0, 2}, {5, 8}, {9}, {}};
  for (size_t page = 0; page < expected.size(); ++page) {
    plugincatalogpaging::PageWindow window(page * 2, 2);
    std::vector<size_t> selected;
    for (size_t row = 0; row < compatible.size(); ++row)
      if (window.take(compatible[row])) selected.push_back(row);
    EXPECT_EQ(selected, expected[page]);
    EXPECT_EQ(window.hasMore(), page < 2);
  }
}

TEST(PluginCatalogPaging, ServerPageLookaheadUsesRawRowsBeforeCompatibilityFiltering) {
  plugincatalogpaging::PageWindow serverPage(0, 2);
  const std::vector<bool> compatible{false, true, true};
  std::vector<size_t> selected;
  for (size_t row = 0; row < compatible.size(); ++row)
    if (serverPage.take() && compatible[row]) selected.push_back(row);
  EXPECT_EQ(selected, (std::vector<size_t>{1}));
  EXPECT_TRUE(serverPage.hasMore());
}
