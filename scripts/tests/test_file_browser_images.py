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
        methods = '\n'.join((
            method(browser, 'void FileBrowserActivity::loadFiles()'),
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
 void close() { valid = false; }
 void flush() {}
 uint64_t fileSize64() const { return bytes->size(); }
 int read(char* out, size_t size) {
   size = std::min(size, bytes->size() - position);
   std::memcpy(out, bytes->data() + position, size); position += size; return size;
 }
 int write(const char* in, size_t size) { bytes->append(in, size); return size; }
};
struct StorageMock {
 std::map<std::string, std::string> data;
 bool removeFails = false, writeFails = false, directoryFails = false;
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
   if (!exists(from)) return false;
   data[to] = data[from]; data.erase(from); return true;
 }
} Storage;
struct CrossPointSettings {
 enum SLEEP_SCREEN_MODE { CUSTOM, TRANSPARENT };
 bool showHiddenFiles = false; uint8_t sleepScreen = CUSTOM;
 bool saveToFile() { return true; }
} SETTINGS;
struct GfxRenderer {
 int width = 480, height = 800;
 int getScreenWidth() const { return width; }
 int getScreenHeight() const { return height; }
 struct FrameBufferLoan { explicit FrameBufferLoan(GfxRenderer&) {} };
};
bool conversionFails = false;
int jpegWidth = 0, jpegHeight = 0, pngConversions = 0;
struct JpegToBmpConverter {
 static bool jpegFileToBmpStreamWithSize(HalFile&, HalFile& out, int w, int h, bool crop) {
   assert(!crop); jpegWidth = w; jpegHeight = h;
   out.write("JPEG-BMP", 8); return !conversionFails;
 }
};
struct PngToBmpConverter {
 static bool pngFileToBmpFile(const char*, const char* out, bool) {
   ++pngConversions; Storage.data[out] = "PNG-BMP"; return !conversionFails;
 }
 static bool pngFileToTransparentBmpFile(const char*, const char* out, bool) {
   Storage.data[out] = "ALPHA-BMP"; return !conversionFails;
 }
};
enum class StrId { STR_NORMAL, STR_TRANSPARENT, STR_SET_SLEEP_COVER };
constexpr int STR_LOADING_POPUP = 0, STR_DONE = 1, STR_FAILED_LOWER = 2;
const char* tr(int) { return ""; }
struct Gui { void drawPopup(GfxRenderer&, const char*) {} } GUI;
void delay(int) {}
struct Popup {
 int choice = 0;
 template<class Callback> void show(StrId, const StrId*, int, int, Callback callback) { callback(choice); }
};
struct ImageViewerActivity {
 std::string filePath; GfxRenderer renderer;
 std::vector<std::string> siblingImages; int currentImageIndex = -1;
 bool imageReady = false; Popup sleepCoverPopup;
 void requestUpdate() {}
 void loadSiblingImages(); bool isPng() const; bool preparePreview();
 void showSleepCoverOptions(); void doSetSleepCover(const char*, bool);
};
std::string joinPath(const std::string& parent, const std::string& name) { return parent + "/" + name; }
struct FileBrowserActivity {
 enum class Mode { Books, PickFirmware, PickPng };
 enum class BrowserState { Browsing, ChoosingMoveDestination };
 Mode mode = Mode::Books; BrowserState browserState = BrowserState::Browsing;
 int prewarmedStart = -1;
 std::string basepath = "/", moveSourcePath;
 std::vector<std::string> files;
 std::unique_ptr<char[]> fileNameBuffer = std::make_unique<char[]>(NAME_BUFFER_SIZE);
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
''' + methods + r'''
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
 ImageViewerActivity viewer; viewer.filePath = "/AirPage/4.jpg";
 Storage.data[viewer.filePath] = "original-jpeg";
 Storage.data[SLEEP_IMAGE_PATH] = "existing-cover";
 for (bool landscape : {false, true}) {
   viewer.renderer.width = landscape ? 800 : 480; viewer.renderer.height = landscape ? 480 : 800;
   Storage.data[IMAGE_PREVIEW_PATH] = "stale-preview";
   assert(viewer.preparePreview());
   assert(jpegWidth == viewer.renderer.width && jpegHeight == viewer.renderer.height);
   assert(Storage.data[IMAGE_PREVIEW_PATH] == "JPEG-BMP");
 }
 viewer.imageReady = true; viewer.showSleepCoverOptions();
 assert(Storage.data[SLEEP_IMAGE_PATH] == "JPEG-BMP");
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
   Storage.data[IMAGE_PREVIEW_PATH] = "stale-preview";
   assert(!viewer.preparePreview()); assert(!Storage.exists(IMAGE_PREVIEW_PATH));
   viewer.imageReady = false; viewer.showSleepCoverOptions();
   viewer.doSetSleepCover(viewer.filePath.c_str(), false);
   assert(Storage.data[SLEEP_IMAGE_PATH] == "original-bmp");
 }
 conversionFails = false; viewer.filePath = "/AirPage/missing.jpeg";
 assert(!viewer.preparePreview()); assert(!Storage.exists(IMAGE_PREVIEW_PATH));
 Storage.data[IMAGE_PREVIEW_PATH] = "stale-preview"; Storage.removeFails = true;
 assert(!viewer.preparePreview()); assert(Storage.data[IMAGE_PREVIEW_PATH] == "stale-preview");
 Storage.removeFails = false; Storage.writeFails = true; viewer.filePath = "/AirPage/4.jpg";
 assert(!viewer.preparePreview()); assert(!Storage.exists(IMAGE_PREVIEW_PATH));
}
''')


if __name__ == '__main__':
    unittest.main()
