"""Compile the real download presentation code with a small renderer seam."""

import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class WeReadDownloadUiTest(unittest.TestCase):
    def test_stage_progress_title_and_waiting_presentation(self):
        source = (ROOT / "src/activities/apps/weread/webapi/WeReadActivity.cpp").read_text()
        progress = source.split("constexpr uint8_t kDownloadStageCount", 1)[1].split(
            "constexpr int disclaimerActionGap", 1)[0]
        title = source.split("void drawTruncatedProgressTitle", 1)[1].split("void drawProgressStatus", 1)[0]
        render = source.split("void WeReadActivity::render(RenderLock&&)", 1)[1]
        download = render.split("    case State::Downloading: {", 1)[1].split("    case State::Error:", 1)[0]
        waits = "\n".join(re.findall(r"constexpr StrId kPostProcess\w+Lines\[\] = \{.*?\};", source, re.S))
        harness = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
namespace WeReadClient { struct Operation {
  enum class ProgressStage { Chapters, Preparing, Images, Packaging };
}; }
using Stage = WeReadClient::Operation::ProgressStage;
enum class StrId {
  STR_WEREAD_CACHING_CHAPTERS, STR_WEREAD_PREPARING_RESOURCES,
  STR_WEREAD_DOWNLOADING_IMAGES, STR_WEREAD_PACKAGING_BOOK,
  STR_WEREAD_POST_PROCESS_WAIT_LINE_2, STR_WEREAD_POST_PROCESS_LONG_WAIT_LINE_1,
  STR_WEREAD_POST_PROCESS_LONG_WAIT_LINE_2, STR_WEREAD_POST_PROCESS_LONG_WAIT_LINE_3,
  STR_WEREAD_POST_PROCESS_LONG_WAIT_LINE_4
};
enum class PostProcessNotice { None, Waiting, LongWait };
enum class State { Downloading, Error };
struct Rect { int x, y, width, height; };
namespace WeReadStore { struct ShelfRecord { char title[192]{}; }; }
namespace EpdFontFamily { enum Style { BOLD }; }
constexpr int UI_12_FONT_ID = 12;
std::string shownTitle, shownStage, shownStatus;
uint32_t shownTotal;
int shownLineCount;
const StrId* shownLines;
struct GfxRenderer {
  int getTextWidth(int, const char* text, EpdFontFamily::Style) {
    int width = 0;
    for (; *text; ++text) if ((static_cast<unsigned char>(*text) & 0xc0) != 0x80) width += 8;
    return width;
  }
};
namespace UITheme {
void drawCenteredText(GfxRenderer&, const Rect&, int, int, const char* text, bool, EpdFontFamily::Style) {
  shownTitle = text;
}
}
struct { const char* get(StrId) { return "label"; } } I18N;
#define tr(key) "Stage %u/%u: %s"
void drawProgressStatus(GfxRenderer&, const Rect&, const char*, const char* stage, const char* status,
                        uint32_t, uint32_t total, const StrId* lines, int lineCount) {
  shownStage = stage; shownStatus = status ? status : ""; shownTotal = total;
  shownLines = lines; shownLineCount = lineCount;
}
GfxRenderer renderer;
Rect content{0, 0, 480, 700};
WeReadStore::ShelfRecord pendingBook_;
std::atomic<Stage> progressStage_{Stage::Chapters};
std::atomic<uint32_t> progressCompleted_{1}, progressTotal_{10};
std::atomic<PostProcessNotice> postProcessNotice_{PostProcessNotice::None};
'''
        checks = r'''
int main() {
  assert(progressBucket(0, 0, 20) == 0);
  assert(progressBucket(UINT32_MAX, UINT32_MAX, 20) == 20);
  assert(progressBucket(UINT32_MAX, 1, 20) == 20);
  for (uint32_t total : {1u, 20u, 200u}) {
    unsigned refreshes = 1;
    for (uint32_t done = 1; done <= total; ++done)
      refreshes += progressBucket(done - 1, total, 20) != progressBucket(done, total, 20);
    assert(refreshes == std::min(total, 20u) + 1);
  }
  const StrId labels[] = {StrId::STR_WEREAD_CACHING_CHAPTERS, StrId::STR_WEREAD_PREPARING_RESOURCES,
                         StrId::STR_WEREAD_DOWNLOADING_IMAGES, StrId::STR_WEREAD_PACKAGING_BOOK};
  for (int i = 0; i < 4; ++i) {
    const Stage stage = static_cast<Stage>(i);
    assert(downloadStageInfo(stage).number == i + 1 && downloadStageInfo(stage).label == labels[i]);
    for (auto notice : {PostProcessNotice::None, PostProcessNotice::Waiting, PostProcessNotice::LongWait}) {
      progressStage_ = stage; postProcessNotice_ = notice; renderDownload();
      assert(shownStage == "Stage " + std::to_string(i + 1) + "/4: label");
      if (stage == Stage::Preparing || stage == Stage::Packaging) {
        assert(shownTotal == 0 && shownStatus.empty());
        assert(shownLineCount == (notice == PostProcessNotice::LongWait ? 4 : 1));
        if (shownLineCount == 1) assert(shownLines[0] == StrId::STR_WEREAD_POST_PROCESS_WAIT_LINE_2);
      } else assert(shownTotal == 10 && shownStatus == "1/10" && shownLineCount == 0);
    }
  }
  progressStage_ = Stage::Chapters; progressTotal_ = 0; renderDownload();
  assert(shownTotal == 0 && shownStatus.empty() && shownLineCount == 0);
  drawTruncatedProgressTitle(renderer, content, 0, nullptr); assert(shownTitle.empty());
  drawTruncatedProgressTitle(renderer, content, 0, "short"); assert(shownTitle == "short");
  std::string chinese;
  for (int i = 0; i < 63; ++i) chinese += "书";
  for (int width : {440, 760}) {
    content.width = width;
    for (const auto& text : {std::string(191, 'x'), chinese}) {
      drawTruncatedProgressTitle(renderer, content, 0, text.c_str());
      assert(renderer.getTextWidth(12, shownTitle.c_str(), EpdFontFamily::BOLD) <= width);
      if (text == chinese && shownTitle != text) assert((shownTitle.size() - 3) % 3 == 0);
    }
  }
}
'''
        program = (harness + waits + "\nconstexpr uint8_t kDownloadStageCount" + progress
                   + "\nvoid drawTruncatedProgressTitle" + title
                   + "\nvoid renderDownload() { switch (State::Downloading) { case State::Downloading: {"
                   + download + "case State::Error: break; } }\n" + checks)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "download.cpp"
            cpp.write_text(program)
            executable = Path(directory) / "download"
            subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++20", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(executable)
            ], check=True, capture_output=True)
            subprocess.run([str(executable)], check=True)
