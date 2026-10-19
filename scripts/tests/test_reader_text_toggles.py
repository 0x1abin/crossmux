"""Execute production Text-panel dispatch for both boolean rows and enum controls."""
from pathlib import Path
import unittest

from test_reading_ui_regressions import method, run_cpp

ROOT = Path(__file__).resolve().parents[2]


class ReaderTextTogglesTest(unittest.TestCase):
    def test_boolean_rows_apply_live_and_enum_rows_keep_their_pickers(self):
        source = (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
        dispatch = method(source, 'void EpubReaderActivity::showTextRowPopup(')
        program = r'''#include <cassert>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>
enum class StrId { STR_FONT_SIZE, STR_LINE_SPACING, STR_PARA_ALIGNMENT, STR_FIRST_LINE_INDENT };
struct CrossPointSettings {
  enum {LINE_COMPRESSION_COUNT=4,PARAGRAPH_ALIGNMENT_COUNT=5};
  char sdFontFamilyName[32]{};
  uint8_t fontPointSize=12,sdFontFlashPreload=0,lineSpacing=1,paragraphAlignment=0,firstLineIndent=0;
  uint8_t focusReadingEnabled=0,verticalBottomAlign=0;
} SETTINGS;
struct {int registryValue=0;int& registry(){return registryValue;}} sdFontSystem;
std::vector<uint8_t> readerFontPointSizes(int*,const char*){return {12,18};}
uint8_t snapToNearestPointSize(const std::vector<uint8_t>&,uint8_t value){return value;}
constexpr StrId kSpacingIds[]={StrId::STR_LINE_SPACING};
constexpr StrId kAlignIds[]={StrId::STR_PARA_ALIGNMENT};
constexpr StrId kIndentIds[]={StrId::STR_FIRST_LINE_INDENT};
struct Popup {int calls=0;template<class...Args>void show(Args&&...){++calls;}};
struct EpubReaderActivity {
  Popup overlayPopup;int applies=0,paints=0;
  void applyTextSettingLive(){++applies;}
  void paintOverlayPopup(){++paints;}
  void showTextRowPopup(int row);
};
''' + dispatch + r'''
int main() {
  EpubReaderActivity reader;
  for (int row : {5,6,5,6}) reader.showTextRowPopup(row);
  assert(SETTINGS.focusReadingEnabled==0 && SETTINGS.verticalBottomAlign==0);
  assert(reader.applies==4 && reader.paints==0 && reader.overlayPopup.calls==0);
  reader.showTextRowPopup(5);
  assert(SETTINGS.focusReadingEnabled==1 && SETTINGS.verticalBottomAlign==0);
  reader.showTextRowPopup(6);
  assert(SETTINGS.focusReadingEnabled==1 && SETTINGS.verticalBottomAlign==1);
  for (int row : {1,2,3,4}) reader.showTextRowPopup(row);
  assert(reader.applies==6 && reader.paints==4 && reader.overlayPopup.calls==4);
  for (int row : {-1,0,7,999}) reader.showTextRowPopup(row);
  assert(reader.applies==6 && reader.paints==4 && reader.overlayPopup.calls==4);
}
'''
        run_cpp(program)


if __name__ == '__main__':
    unittest.main()
