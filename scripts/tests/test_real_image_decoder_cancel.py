"""Verify the pinned production decoders stop on callback cancellation (not simulator shims).

Run after pio has installed device libraries:
  python3 scripts/tests/test_real_image_decoder_cancel.py --libdeps .pio/libdeps/default
"""
import argparse
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib

ROOT=Path(__file__).resolve().parents[2]

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--libdeps',type=Path)
    parser.add_argument('--png-source',type=Path)
    parser.add_argument('--jpeg-source',type=Path)
    args=parser.parse_args()
    png=args.png_source or args.libdeps.resolve()/'PNGdec/src'
    jpeg=args.jpeg_source or args.libdeps.resolve()/'JPEGDEC/src'
    host_flags=['-D__LINUX__'] if sys.platform.startswith('linux') else []
    assert (png/'PNGdec.cpp').exists() and (jpeg/'JPEGDEC.cpp').exists()
    with tempfile.TemporaryDirectory(prefix='real-image-cancel-') as tmp:
        d=Path(tmp)
        def chunk(kind,data):
            return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
        data=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',128,128,8,0,0,0,0))
        data+=chunk(b'IDAT',zlib.compress(b''.join(b'\0'+bytes(range(128)) for _ in range(128))))+chunk(b'IEND',b'')
        (d/'test.png').write_bytes(data)
        with zipfile.ZipFile(ROOT/'test/epubs/test_jpeg_images.epub') as book:
            candidates=[n for n in book.namelist() if n.lower().endswith(('.jpg','.jpeg'))]
            name=next(n for n in candidates if n.endswith('/grayscale_test.jpg'))
            (d/'test.jpg').write_bytes(book.read(name))
        cpp=d/'check.cpp'
        cpp.write_text(r'''
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include "PNGdec.h"
#undef REGISTER_WIDTH
#include "JPEGDEC.h"
#include "CancelCheck.h"
int calls=0,stopAt=0;
bool cancelled(void*) {return stopAt && calls>=stopAt;}
CancelCheck cancel{nullptr,cancelled};
int pngDraw(PNGDRAW*) {++calls;return !cancel.isCancelled();}
int jpegDraw(JPEGDRAW*) {++calls;return !cancel.isCancelled();}
std::vector<uint8_t> bytes(const char* path) {
 std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};
}
int main(int argc,char** argv) {
 assert(argc==3);auto p=bytes(argv[1]),j=bytes(argv[2]);
 PNG png;assert(png.openRAM(p.data(),p.size(),pngDraw)==PNG_SUCCESS);
 assert(png.decode(nullptr,0)==PNG_SUCCESS);assert(calls==128);png.close();
 calls=0;stopAt=3;assert(png.openRAM(p.data(),p.size(),pngDraw)==PNG_SUCCESS);
 assert(png.decode(nullptr,0)==PNG_QUIT_EARLY);assert(calls==3);png.close();
 calls=0;stopAt=0;JPEGDEC jpeg;assert(jpeg.openRAM(j.data(),j.size(),jpegDraw)==1);
 jpeg.setPixelType(EIGHT_BIT_GRAYSCALE);
 int scale=jpeg.getJPEGType()==JPEG_MODE_PROGRESSIVE ? JPEG_SCALE_EIGHTH : 0;
 int result=jpeg.decode(0,0,scale);fprintf(stderr,"JPEG result=%d error=%d type=%d size=%dx%d calls=%d\n",result,jpeg.getLastError(),jpeg.getJPEGType(),jpeg.getWidth(),jpeg.getHeight(),calls);assert(result==1);int total=calls;assert(total>3);jpeg.close();
 calls=0;stopAt=3;assert(jpeg.openRAM(j.data(),j.size(),jpegDraw)==1);
 jpeg.setPixelType(EIGHT_BIT_GRAYSCALE);
 (void)jpeg.decode(0,0,scale);assert(calls==3 && calls<total);jpeg.close();
 // JPEG may report success after a callback stop; callers must inspect cancellation.
 assert(cancel.isCancelled());
}
''')
        objects=[]
        for f in png.glob('*.c'):
            o=d/(f.stem+'.o');objects.append(str(o))
            subprocess.run(['cc','-O1',*host_flags,'-I'+str(png),'-c',str(f),'-o',str(o)],check=True)
        exe=d/'check'
        subprocess.run(['c++','-std=c++20','-O1',*host_flags,'-I'+str(png),'-I'+str(jpeg),'-I'+str(ROOT/'lib/Memory'),str(cpp),str(png/'PNGdec.cpp'),str(jpeg/'JPEGDEC.cpp'),*objects,'-o',str(exe)],check=True)
        subprocess.run([str(exe),str(d/'test.png'),str(d/'test.jpg')],check=True)
        print('Pinned PNG/JPEG callback cancellation: PASS')

if __name__=='__main__':
    main()
