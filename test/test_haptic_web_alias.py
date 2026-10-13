"""Compile the actual legacy POST mapping against ArduinoJson, including type rejection."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]


def run():
    source=(ROOT/"src/network/CrossPointWebServer.cpp").read_text()
    source=source.split("void CrossPointWebServer::handlePostSettings()",1)[1]
    mapping=source[source.index("  int applied = 0;"):source.index("  forEachSettingsListEntry")]
    harness=r"""
#include <ArduinoJson.h>
#include <cassert>
#include <cstdint>
struct CrossPointSettings {
 enum {HAPTIC_FEEDBACK_LEVEL_COUNT=4,VIBRATION_TOUCH_PAGE=1,VIBRATION_OFF=2};
 uint8_t vibration=VIBRATION_OFF,hapticIntensity=2;
} SETTINGS;
int apply(JsonDocument& doc) {
"""+mapping+r"""
 return applied;
}
int main() {
 JsonDocument doc;
 for(int level=0;level<4;++level) {
  SETTINGS={};doc.clear();doc["hapticFeedbackLevel"]=level;
  assert(apply(doc)==1);
  assert(SETTINGS.vibration==(level==0?2:1));
  assert(SETTINGS.hapticIntensity==(level==0?2:level-1));
 }
 for(int level : {-1,4,999}) {
  SETTINGS={};doc.clear();doc["hapticFeedbackLevel"]=level;
  assert(apply(doc)==0 && SETTINGS.vibration==2 && SETTINGS.hapticIntensity==2);
 }
 for(const char* value : {"1","nonsense"}) {
  SETTINGS={};doc.clear();doc["hapticFeedbackLevel"]=value;assert(apply(doc)==0);
 }
 for(const char* key : {"vibration","hapticIntensity"}) {
  SETTINGS={};doc.clear();doc["hapticFeedbackLevel"]=1;doc[key]=0;
  assert(apply(doc)==0 && SETTINGS.vibration==2 && SETTINGS.hapticIntensity==2);
 }
 doc.clear();assert(apply(doc)==0);
}
"""
    with tempfile.TemporaryDirectory(prefix="haptic-web-") as directory:
        tmp=Path(directory);(tmp/"check.cpp").write_text(harness)
        subprocess.run(["c++","-std=c++17","-DFREEINK_CAP_HAPTIC=1","-I"+str(ROOT/".pio/libdeps/default/ArduinoJson/src"),
                        str(tmp/"check.cpp"),"-o",str(tmp/"check")],check=True,env=dict(os.environ,CCACHE_DISABLE="1"))
        subprocess.run([str(tmp/"check")],check=True)
    print("Actual legacy Web haptic mapping, bounds/types and new-key precedence passed")


if __name__=="__main__":
    run()
