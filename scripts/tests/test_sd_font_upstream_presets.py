"""Offline preset/config checks; --assets also checks real files and the reader."""

import argparse
import ast
import hashlib
import json
import struct
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class SdFontUpstreamPresetsTest(unittest.TestCase):
    def test_latin_ext_includes_combining_marks(self):
        source = (ROOT / "lib/EpdFont/scripts/fontconvert_sdcard.py").read_text()
        presets = next(
            ast.literal_eval(node.value)
            for node in ast.parse(source).body
            if isinstance(node, ast.Assign)
            and any(isinstance(target, ast.Name) and target.id == "INTERVAL_PRESETS" for target in node.targets)
        )
        self.assertEqual(presets["latin-ext"], [
            (0x0020, 0x007E), (0x0080, 0x00FF), (0x0100, 0x024F),
            (0x02B0, 0x02FF), (0x0300, 0x036F), (0x1E00, 0x1EFF),
            (0x2000, 0x206F), (0x20A0, 0x20CF), (0x2100, 0x214F),
            (0x2212, 0x2212), (0xFB00, 0xFB06),
        ])
        self.assertIn("combining diacritical marks", (ROOT / "docs/sd-card-fonts.md").read_text())

    def test_japanese_font_config_and_license(self):
        config = (ROOT / "lib/EpdFont/scripts/sd-fonts.yaml").read_text()
        family = config.split("  - name: NotoSansCJKJP", 1)[1].split("\n  - name:", 1)[0]
        self.assertIn("sizes: [8, 10, 12, 14, 16, 18]", family)
        self.assertIn("scripts: [cjk]", family)
        pin = "165c01b46ea533872e002e0785ff17e44f6d97d8"
        for style in ("Regular", "Bold"):
            self.assertIn(f"{pin}/Sans/OTF/Japanese/NotoSansCJKjp-{style}.otf", family)
        license_text = (ROOT / "licenses/fonts.txt").read_text()
        self.assertIn("NotoSansCJKJP", license_text)
        self.assertIn(f"{pin}/LICENSE", license_text)


def validate_assets(assets: Path) -> None:
    manifest=json.loads((assets/"fonts.json").read_text());families=manifest["families"];assert len(families)==1 and families[0]["name"]=="NotoSansCJKJP"
    family=families[0];assert family["styles"]==["regular","bold"] and family["scripts"]==["cjk"]
    sizes=set();records={}
    toc="<B3xIIBhhHHBBBI4x"
    for file in family["files"]:
        p=assets/"NotoSansCJKJP"/file["name"];data=p.read_bytes();sizes.add(int(p.stem.rsplit("_",1)[1]));assert len(data)==file["size"] and zlib.crc32(data)&0xffffffff==file["crc32"]
        magic,version,flags,count=struct.unpack_from("<8sHHB",data);assert magic==b"CPFONT\x00\x00" and version==4 and count==2
        styles=[]
        for i in range(count):
            entry=struct.unpack_from(toc,data,32+i*32);style,intervals,glyphs=entry[:3];offset=entry[-1]
            assert style==i and 0<intervals<=4096 and glyphs>20000 and offset<len(data)
            for j in range(intervals):
                first,last,index=struct.unpack_from("<III",data,offset+j*12);assert first<=last and index+last-first<glyphs
            styles.append({"style":style,"intervals":intervals,"glyphs":glyphs})
        records[file["name"]]={"sha256":hashlib.sha256(data).hexdigest(),"bytes":len(data),"styles":styles}
    assert sizes=={8,10,12,14,16,18}
    (assets/"validated-artifacts.json").write_text(json.dumps(records,indent=2)+"\n")
    probe=r"""
#include <HalStorage.h>
#include <SdCardFont.h>
#include <SdCardFontCache.h>
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
// This probe exercises SD access; flash partitions are unavailable in its host fixture.
namespace SdCardFontCache {
bool isValidFor(const char*,size_t*){return false;}
bool readAt(size_t,void*,size_t,size_t){assert(false);return false;}
}
int main(int argc,char**argv){
 assert(argc==2);std::ifstream input(argv[1],std::ios::binary);assert(input);
 sdFontTestFile.assign(std::istreambuf_iterator<char>(input),{});
 SdCardFont font;assert(font.load("/fixture.cpfont")&&font.hasStyle(0)&&font.hasStyle(1));
 const char*text="日本語かなカナABCé é";
 assert(font.prewarm(text,3,false,false)==0);
 assert(font.buildAdvanceTable(text,3)==0);
 for(uint8_t style:{uint8_t(0),uint8_t(1)}){
  assert(font.getAdvance(0x65e5,style)>0&&font.getAdvance('A',style)>0);
  const auto*face=font.getEpdFont(style);assert(face&&face->getGlyph(0x65e5)&&face->getGlyph(0x0301));
 }
}
"""
    with tempfile.TemporaryDirectory() as directory:
        cpp=Path(directory)/"font.cpp";binary=Path(directory)/"font";cpp.write_text(probe)
        inc=[ROOT/"test/sd_card_font/stubs",ROOT/"lib/EpdFont",ROOT/"lib/Utf8",ROOT/"lib/Memory",ROOT/"lib/MiniBidi",ROOT/"test/minibidi_arabic/stubs",ROOT/"freeink-sdk/libs/font/FreeInkFont/include"]
        sources=[ROOT/"lib/EpdFont/EpdFont.cpp",ROOT/"lib/EpdFont/SdCardFont.cpp",ROOT/"lib/Utf8/Utf8.cpp",ROOT/"lib/MiniBidi/BidiUtils.cpp"]
        bidi=Path(directory)/"minibidi.o"
        subprocess.run(["cc","-std=c11","-c",str(ROOT/"lib/MiniBidi/minibidi.c"),"-o",str(bidi)],check=True)
        for mode in [[],["-DBOARD_HAS_PSRAM=1","-DSIMULATOR=1"]]:
            subprocess.run(["c++","-std=c++20","-fno-exceptions","-DCROSSPOINT_EMULATED=1",*mode,*("-I"+str(p) for p in inc),str(cpp),*(str(p) for p in sources),str(bidi),"-o",str(binary)],check=True)
            for file in family["files"]:subprocess.run([str(binary),str(assets/"NotoSansCJKJP"/file["name"])],check=True)
    print("Six actual v4 assets, CRC/size/interval/glyph tables and C3/S3 reader load/prewarm/advances pass")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--assets", type=Path)
    args = parser.parse_args()
    if args.assets:
        validate_assets(args.assets)
    unittest.main(argv=[__file__])
