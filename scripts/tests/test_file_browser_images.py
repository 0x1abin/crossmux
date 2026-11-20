"""Exercise production image filtering, routing, preview failures and cover saving."""
from pathlib import Path
import unittest
from test_reading_ui_regressions import method, run_cpp

ROOT = Path(__file__).resolve().parents[2]


class FileBrowserImagesTest(unittest.TestCase):
    def test_mixed_images_and_failed_previews(self):
        fs = (ROOT / 'lib/FsHelpers/FsHelpers.cpp').read_text()
        viewer = (ROOT / 'src/activities/util/ImageViewerActivity.cpp').read_text()
        browser = (ROOT / 'src/activities/home/FileBrowserActivity.cpp').read_text()
        manager = (ROOT / 'src/activities/ActivityManager.cpp').read_text()
        theme = (ROOT / 'src/components/UITheme.cpp').read_text()
        helpers = '\n'.join(method(fs, name) for name in (
            'bool checkFileExtension(', 'bool hasJpgExtension(', 'bool hasPngExtension(',
            'bool hasBmpExtension(', 'bool hasImageExtension(', 'bool hasEpubExtension(',
            'bool hasXtcExtension(', 'bool hasTxtExtension(', 'bool hasMarkdownExtension(',
            'bool naturalLess(', 'void sortFileList(', 'std::string extractFolderPath(',
            'bool isProtectedPathComponent(', 'bool isSameOrDescendantPath('))
        cover_source = (ROOT / 'src/util/SleepCover.cpp').read_text()
        cover = cover_source[cover_source.index('namespace sleepCover {'):]
        cover = cover.replace(method(cover, 'bool validImage('), 'bool validImage(const char* path) { return Storage.exists(path); }')
        cover = '#include "' + str(ROOT / 'src/util/SleepCover.h') + '"\n' + cover
        methods = '\n'.join((
            method(browser, 'void FileBrowserActivity::loadFiles()'),
            method(browser, 'void FileBrowserActivity::finishEdit('),
            method(browser, 'size_t FileBrowserActivity::findEntry('),
            method(manager, 'void ActivityManager::goToReader('),
            method(theme, 'UIIcon UITheme::getFileIcon('),
            *(method(viewer, name) for name in (
                'void ImageViewerActivity::loadSiblingImages()', 'bool ImageViewerActivity::isPng()',
                'bool ImageViewerActivity::preparePreview()',
                'void ImageViewerActivity::showSleepCoverOptions()',
                'void ImageViewerActivity::doSetSleepCover('))))
        run_cpp(r'''
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#define LOG_ERR(...) ((void)0)
namespace FsHelpers {
''' + helpers + r'''
}
constexpr size_t NAME_BUFFER_SIZE = 500;
constexpr char MOVE_HERE_ENTRY[] = "\x01";
constexpr const char* IMAGE_PREVIEW_PATH = "/.crosspoint/image_preview.bmp";
constexpr const char* TRANSPARENT_PREVIEW_PATH = "/.crosspoint/image_preview.transparent.bmp";
constexpr const char* SLEEP_IMAGE_PATH = "/sleep.bmp";
constexpr const char* SLEEP_IMAGE_PART_PATH = "/sleep.bmp.part";
constexpr const char* SLEEP_IMAGE_BACKUP_PATH = "/sleep.bmp.bak";
struct Entry { std::string name; bool directory = false; };
std::vector<Entry> entries;
struct HalFile {
 bool valid = false, directory = false; size_t index = 0, position = 0;
 std::string name; std::string* bytes = nullptr;
 explicit operator bool() const { return valid; }
 bool isDirectory() const { return directory; }
 void rewindDirectory() { index = 0; }
 HalFile openNextFile() {
   if (index == entries.size()) return {};
   const auto& e = entries[index++];
   HalFile f; f.valid = true; f.directory = e.directory; f.name = e.name; return f;
 }
 void getName(char* out, size_t size) { std::strncpy(out, name.c_str(), size); }
 bool close() { valid = false; return true; }
 void flush() {}
 uint64_t fileSize64() const { return bytes->size(); }
 int read(void* out, size_t size) {
   size = std::min(size, bytes->size() - position);
   std::memcpy(out, bytes->data() + position, size); position += size; return size;
 }
 int write(const void* in, size_t size) { bytes->append(static_cast<const char*>(in), size); return size; }
};
struct StorageMock {
 std::map<std::string, std::string> data;
 bool removeFails = false, writeFails = false, directoryFails = false;
 std::string failRenameFrom;
 HalFile open(const char*) { HalFile f; f.valid = true; f.directory = true; return f; }
 bool ensureDirectoryExists(const char*) { return !directoryFails; }
 bool exists(const char* path) { return data.contains(path); }
 bool remove(const char* path) { if (removeFails) return false; return data.erase(path); }
 bool openFileForRead(const char*, const char* path, HalFile& f) {
   if (!exists(path)) return false;
   f.valid = true; f.bytes = &data[path]; return true;
 }
 bool openFileForWrite(const char*, const char* path, HalFile& f) {
   if (writeFails) return false;
   data[path].clear(); f.valid = true; f.bytes = &data[path]; return true;
 }
 bool rename(const char* from, const char* to) {
   if (!exists(from) || from == failRenameFrom) return false;
   data[to] = data[from]; data.erase(from); return true;
 }
} Storage;
struct CrossPointSettings {
 enum SLEEP_SCREEN_MODE { CUSTOM, TRANSPARENT, COVER_TRANSPARENT, SLEEP_SCREEN_MODE_COUNT };
 enum SLEEP_SCREEN_COVER_FILTER { NO_FILTER, BLACK_AND_WHITE, INVERTED_BLACK_AND_WHITE };
 bool showHiddenFiles = false; uint8_t sleepScreen = CUSTOM; uint8_t sleepScreenCoverFilter = NO_FILTER;
 int failedSaves = 0;
 bool saveToFile() { if (failedSaves > 0) { --failedSaves; return false; } return true; }
} SETTINGS;
struct GfxRenderer {
 int width = 480, height = 800;
 int getScreenWidth() const { return width; }
 int getScreenHeight() const { return height; }
 struct FrameBufferLoan { explicit FrameBufferLoan(GfxRenderer&) {} };
 // The threshold predicates live in util/SleepGrayscale.h; this harness only
 // needs the same call shape, and pins the legacy branch (unsupported panel).
 struct GrayscaleCapabilitiesStub { bool supported() const { return false; } };
 GrayscaleCapabilitiesStub grayscaleCapabilities(int) const { return {}; }
};
int sleepGrayscaleMode(const GfxRenderer&) { return 0; }
bool useOriginalGrayscaleThresholds() { return false; }
bool conversionFails = false;
int jpegWidth = 0, jpegHeight = 0, pngConversions = 0;
struct JpegToBmpConverter {
 static bool jpegFileToBmpStreamWithSize(HalFile&, HalFile& out, int w, int h, bool crop) {
   assert(!crop); jpegWidth = w; jpegHeight = h;
   out.write("JPEG-BMP", 8); return !conversionFails;
 }
};
struct PngToBmpConverter {
 static bool pngFileToBmpFile(const char* input, const char* out, bool, bool) {
   ++pngConversions; Storage.data[out] = "PNG-BMP"; return !conversionFails && !Storage.writeFails && Storage.exists(input);
 }
 static bool pngFileToTransparentBmpFile(const char*, const char* out, bool) {
   Storage.data[out] = "ALPHA-BMP"; return !conversionFails;
 }
};
enum class StrId { STR_NORMAL, STR_TRANSPARENT, STR_SET_SLEEP_COVER, STR_SET_SLEEP_COVER_CONFIRM };
constexpr int STR_LOADING_POPUP = 0, STR_DONE = 1, STR_FAILED_LOWER = 2, STR_SET_SLEEP_COVER_CONFIRM = 3;
const char* tr(int) { return ""; }
struct Gui { void drawPopup(GfxRenderer&, const char*) {} } GUI;
void delay(int) {}
struct Popup {
 int choice = 0;
 template<class Callback> void show(StrId, const StrId*, int, int, Callback callback) { callback(choice); }
};
struct ActivityResult { bool isCancelled = false; };
bool cancelConfirmation = false, confirmationOom = false;
struct ConfirmationActivity {
 template<class... Args> explicit ConfirmationActivity(Args&&...) {}
};
template<class T, class... Args> std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
 return confirmationOom ? nullptr : std::make_unique<T>(std::forward<Args>(args)...);
}
struct ImageViewerActivity {
 int mappedInput = 0, entries = 0;
 void onEnter() { ++entries; }
 template<class Handler> void startActivityForResult(std::unique_ptr<ConfirmationActivity>, Handler handler) {
   handler(ActivityResult{cancelConfirmation});
 }
 std::string filePath; GfxRenderer renderer;
 std::vector<std::string> siblingImages; int currentImageIndex = -1;
 bool imageReady = false; Popup sleepCoverPopup;
 void requestUpdate() {}
 void loadSiblingImages(); bool isPng() const; bool preparePreview();
 bool prepareJpegSleepCover() {
   if (conversionFails || !Storage.exists(filePath.c_str())) return false;
   jpegWidth=renderer.getScreenWidth(); jpegHeight=renderer.getScreenHeight();
   Storage.data[SLEEP_IMAGE_PART_PATH]="JPEG-2BIT"; return true;
 }
 void showSleepCoverOptions(); void doSetSleepCover(const char*, bool);
};
std::string joinPath(const std::string& parent, const std::string& name) { return parent + "/" + name; }
struct RenderLock {
 template<class T>explicit RenderLock(T&) {}
 void unlock() {}
};
struct FileBrowserActivity {
 enum class Mode { Books, PickFirmware, PickPng };
 enum class BrowserState { Browsing, ChoosingMoveDestination };
 Mode mode = Mode::Books; BrowserState browserState = BrowserState::Browsing;
 int prewarmedStart = -1;
 std::string basepath = "/", moveSourcePath;
 std::vector<std::string> files;
 std::unique_ptr<char[]> fileNameBuffer = std::make_unique<char[]>(NAME_BUFFER_SIZE);
 struct { int selected=0;void follow(int) {} } nav;
 bool selected=false;int updates=0;
 int listCount() const {return files.size();}
 void clearContentSelection() {selected=false;}
 void establishContentSelection(int index) {assert(index>=0 && index<listCount());selected=true;}
 void requestUpdate(bool) {++updates;}
 size_t findEntry(const std::string&) const;
 void finishEdit(const std::string&,int,const std::string& = {});
 void rebuildRowItems() {} void loadFiles();
};
struct ReaderActivity { static int create(GfxRenderer&, int&, std::string, bool) { return 1; } };
struct ActivityManager {
 GfxRenderer renderer; int mappedInput = 0; bool image = false, reader = false;
 void goToFileBrowser(const char*) {}
 template<class T> void replaceActivityWith(std::string) { image = true; }
 void replaceActivity(int) { reader = true; }
 void goToReader(std::string, bool = false);
};
enum UIIcon { Folder, Book, Text, Image, File };
struct UITheme { static UIIcon getFileIcon(const std::string&); };
''' + cover + methods + r'''
int main() {
 for (const char* extension : {"bmp", "BMP", "jpg", "JPG", "jpeg", "JpEg", "png", "PnG"}) {
   std::string path = std::string("/AirPage/picture.") + extension;
   assert(FsHelpers::hasImageExtension(path));
   ActivityManager manager; manager.goToReader(path);
   assert(manager.image && !manager.reader);
   assert(UITheme::getFileIcon(path) == Image);
 }
 for (const char* name : {"", "x", "image.gif", "image.jpg.part", "image.png.bak", "image.jpeg/"})
   assert(!FsHelpers::hasImageExtension(name));
 entries = {{"10.JPEG"}, {"2.bmp"}, {"3.PNG"}, {"4.jpg"}, {"ignore.gif"},
            {".hidden.jpeg"}, {"update.bin"}, {"book.epub"}, {"nested", true}};
 for (const char* dir : {"/", "/Pictures", "/AirPage"}) {
   FileBrowserActivity browser; browser.basepath = dir; browser.loadFiles();
   assert((browser.files == std::vector<std::string>{"nested/", "2.bmp", "3.PNG", "4.jpg", "10.JPEG", "book.epub"}));
   browser.mode = FileBrowserActivity::Mode::PickPng; browser.loadFiles();
   assert((browser.files == std::vector<std::string>{"nested/", "3.PNG"}));
   browser.mode = FileBrowserActivity::Mode::PickFirmware; browser.loadFiles();
   assert((browser.files == std::vector<std::string>{"nested/", "update.bin"}));
   ImageViewerActivity viewer; viewer.filePath = std::string(dir) + "/4.jpg";
   viewer.loadSiblingImages();
   assert((viewer.siblingImages == std::vector<std::string>{"2.bmp", "3.PNG", "4.jpg", "10.JPEG"}));
   assert(viewer.currentImageIndex == 2);
 }
 // Reloads must never highlight a fallback neighbor after deletion or a missing entry.
 FileBrowserActivity edited;edited.selected=true;edited.nav.selected=4;
 edited.loadFiles();assert(!edited.selected);
 edited.finishEdit("/Pictures",4,"3.PNG");
 assert(edited.selected && edited.files[edited.nav.selected]=="3.PNG" && edited.updates==1);
 edited.finishEdit("/Pictures",4);
 assert(!edited.selected && edited.nav.selected==4 && edited.updates==2);
 edited.finishEdit("/Pictures",4,"removed.PNG");
 assert(!edited.selected && edited.nav.selected==0 && edited.updates==3);
 entries.clear();edited.finishEdit("/Pictures",4,"3.PNG");
 assert(!edited.selected && edited.nav.selected==0 && edited.files.empty());
 ImageViewerActivity viewer; viewer.filePath = "/AirPage/4.jpg";
 Storage.data[viewer.filePath] = "original-jpeg";
 Storage.data[SLEEP_IMAGE_PATH] = "existing-cover";
 viewer.imageReady = true;
 for (bool landscape : {false, true}) {
   viewer.renderer.width = landscape ? 800 : 480; viewer.renderer.height = landscape ? 480 : 800;
   viewer.showSleepCoverOptions();
   assert(jpegWidth == viewer.renderer.width && jpegHeight == viewer.renderer.height);
   assert(Storage.data[SLEEP_IMAGE_PATH] == "JPEG-2BIT");
   assert(!Storage.exists(SLEEP_IMAGE_PART_PATH));
 }
 assert(Storage.data[viewer.filePath] == "original-jpeg");
 viewer.filePath = "/AirPage/3.PNG"; Storage.data[viewer.filePath] = "original-png";
 assert(viewer.preparePreview()); assert(pngConversions == 1);
 viewer.showSleepCoverOptions(); assert(Storage.data[SLEEP_IMAGE_PATH] == "PNG-BMP");
 viewer.sleepCoverPopup.choice = 1; viewer.showSleepCoverOptions();
 assert(Storage.data[SLEEP_IMAGE_PATH] == "ALPHA-BMP");
 viewer.filePath = "/2.bmp"; Storage.data[viewer.filePath] = "original-bmp";
 viewer.showSleepCoverOptions(); assert(Storage.data[SLEEP_IMAGE_PATH] == "original-bmp");
 for (const char* path : {"/AirPage/4.jpg", "/AirPage/3.PNG"}) {
   viewer.filePath = path; conversionFails = true;
   if (viewer.isPng()) {
     Storage.data[IMAGE_PREVIEW_PATH] = "stale-preview";
     assert(!viewer.preparePreview()); assert(!Storage.exists(IMAGE_PREVIEW_PATH));
   } else {
     viewer.imageReady=true; viewer.showSleepCoverOptions();
     assert(Storage.data[SLEEP_IMAGE_PATH]=="original-bmp");
   }
   viewer.imageReady = false; viewer.showSleepCoverOptions();
   viewer.doSetSleepCover(viewer.filePath.c_str(), false);
   assert(Storage.data[SLEEP_IMAGE_PATH] == "original-bmp");
 }
 // Confirmation cancellation and allocation failure never overwrite an installed cover.
 conversionFails = false; viewer.filePath = "/2.bmp"; viewer.imageReady = true;
 cancelConfirmation = true; viewer.showSleepCoverOptions();
 assert(Storage.data[SLEEP_IMAGE_PATH] == "original-bmp");
 cancelConfirmation = false; confirmationOom = true; viewer.showSleepCoverOptions();
 assert(Storage.data[SLEEP_IMAGE_PATH] == "original-bmp"); confirmationOom = false;
 // Cover+Transparent keeps its mode and writes only its overlay target.
 SETTINGS.sleepScreen = CrossPointSettings::COVER_TRANSPARENT;
 viewer.filePath = "/AirPage/3.PNG"; assert(viewer.preparePreview());
 viewer.sleepCoverPopup.choice = 1; viewer.showSleepCoverOptions();
 assert(Storage.data[sleepCover::overlayImagePath] == "ALPHA-BMP");
 assert(Storage.data[SLEEP_IMAGE_PATH] == "original-bmp");
 assert(SETTINGS.sleepScreen == CrossPointSettings::COVER_TRANSPARENT);
 viewer.filePath = "/2.bmp"; viewer.showSleepCoverOptions();
 assert(Storage.data[sleepCover::overlayImagePath] == "original-bmp");
 // Failed swap and settings persistence restore the previous overlay and mode.
 Storage.data[sleepCover::overlayPartPath] = "replacement";
 Storage.failRenameFrom = sleepCover::overlayPartPath;
 assert(!sleepCover::commit(CrossPointSettings::COVER_TRANSPARENT));
 assert(Storage.data[sleepCover::overlayImagePath] == "original-bmp");
 assert(Storage.data[SLEEP_IMAGE_PATH] == "original-bmp");
 Storage.failRenameFrom.clear(); Storage.data[sleepCover::overlayPartPath] = "replacement";
 SETTINGS.failedSaves = 1; assert(!sleepCover::commit(CrossPointSettings::COVER_TRANSPARENT));
 assert(Storage.data[sleepCover::overlayImagePath] == "original-bmp");
 assert(SETTINGS.sleepScreen == CrossPointSettings::COVER_TRANSPARENT);
 // Crash recovery uses the existing three-byte marker separately for each target.
 Storage.data["/sleep-overlay.bmp.bak"] = "recover-overlay";
 Storage.data["/sleep-overlay.bmp"] = "partial-overlay";
 Storage.data["/sleep-overlay.bmp.txn"] = std::string({char(1), char(CrossPointSettings::COVER_TRANSPARENT), char(1)});
 assert(sleepCover::recover());
 assert(Storage.data[sleepCover::overlayImagePath] == "recover-overlay");
 assert(Storage.data[SLEEP_IMAGE_PATH] == "original-bmp");
 assert(!Storage.exists("/sleep-overlay.bmp.bak") && !Storage.exists("/sleep-overlay.bmp.txn"));
 Storage.data["/sleep.bmp.bak"] = "legacy-cover";
 Storage.data["/sleep.bmp.txn"] = std::string({char(1), char(CrossPointSettings::CUSTOM), char(1)});
 assert(sleepCover::recover()); assert(Storage.data[SLEEP_IMAGE_PATH] == "legacy-cover");
 assert(SETTINGS.sleepScreen == CrossPointSettings::CUSTOM);
 conversionFails = false; viewer.filePath = "/AirPage/missing.PNG";
 assert(!viewer.preparePreview()); assert(!Storage.exists(IMAGE_PREVIEW_PATH));
 Storage.data[IMAGE_PREVIEW_PATH] = "stale-preview"; Storage.removeFails = true;
 assert(!viewer.preparePreview()); assert(Storage.data[IMAGE_PREVIEW_PATH] == "stale-preview");
 Storage.removeFails = false; Storage.writeFails = true; viewer.filePath = "/AirPage/3.PNG";
 assert(!viewer.preparePreview()); assert(!Storage.exists(IMAGE_PREVIEW_PATH));
}
''')


if __name__ == '__main__':
    unittest.main()
