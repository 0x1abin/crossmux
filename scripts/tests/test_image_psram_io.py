"""Exercise production PNG preloading and ZIP streaming with fault-injectable heaps/files."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_reading_ui_regressions import method

ROOT = Path(__file__).resolve().parents[2]

class ImagePsramIoTest(unittest.TestCase):
    def test_fallbacks_and_stream_contract(self):
        png = (ROOT / 'lib/Epub/Epub/converters/PngToFramebufferConverter.cpp').read_text()
        zip_source = (ROOT / 'lib/ZipFile/ZipFile.cpp').read_text()
        buffers = method(zip_source, 'struct StreamBuffers') + ';'
        preload = method(png, '[[maybe_unused]] static memory::ByteBuffer readPngIntoPsram')
        stream = method(zip_source, 'bool ZipFile::readFileToStream(')
        program = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include <zlib.h>
#include "CancelCheck.h"
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
namespace memory {
bool headroom=true;
int attempts=0, failAt=0, live=0;
size_t budget=0;
struct Free { void operator()(uint8_t* p) const { std::free(p); } };
using ByteBuffer=std::unique_ptr<uint8_t,Free>;
bool psramHasHeadroom(size_t total,size_t,size_t) {budget=total;return headroom;}
ByteBuffer makePsramByteBufferUninitializedNoThrow(size_t n) {
  if (++attempts==failAt) return {};
  return ByteBuffer(static_cast<uint8_t*>(std::malloc(n)));
}
}
struct HalFile {
 std::vector<uint8_t> data;
 size_t pos=0, readLimit=SIZE_MAX, largestRead=0;
 size_t size() const {return data.size();}
 bool seek(size_t n) {pos=n;return n<=data.size();}
 size_t read(uint8_t* p,size_t n) {
  largestRead=std::max(largestRead,n);
  n=std::min({n,data.size()-pos,readLimit});
  std::memcpy(p,data.data()+pos,n);pos+=n;return n;
 }
};
struct StorageType {
 size_t size=100000, limit=SIZE_MAX;
 bool openFileForRead(const char*,const std::string&,HalFile& f) {
  f.data.assign(size,0x5a);f.readLimit=limit;return true;
 }
} Storage;
constexpr size_t PNG_PSRAM_MAX_BYTES=2*1024*1024;
'''+preload+buffers+r'''
struct Print {
 std::vector<uint8_t> data;
 size_t limit=SIZE_MAX;
 size_t write(const uint8_t* p,size_t n) {
  n=std::min(n,limit);data.insert(data.end(),p,p+n);return n;
 }
};
struct ZipInflateCtx {HalFile* file;size_t fileRemaining;uint8_t* readBuf;size_t readBufSize;CancelCheck cancellation;};
int zipFillCallback(void*,uint8_t*,int) {return 0;}
struct InflateStream {
 enum class Status {Ok,Done,Error};
 z_stream z{};ZipInflateCtx* ctx=nullptr;std::vector<uint8_t> input;
 bool init(bool) {return inflateInit2(&z,-15)==Z_OK;}
 void setFill(int(*)(void*,uint8_t*,int),ZipInflateCtx* c) {
  ctx=c; input.resize(c->fileRemaining);
  size_t done=0;
  while(done<input.size()) {
   size_t n=c->file->read(c->readBuf,std::min(c->readBufSize,input.size()-done));
   if(!n) break;
   std::memcpy(input.data()+done,c->readBuf,n);done+=n;
  }
  z.next_in=input.data();z.avail_in=done;
 }
 Status readAtMost(uint8_t* out,size_t n,size_t* produced) {
  z.next_out=out;z.avail_out=n;int rc=inflate(&z,Z_NO_FLUSH);*produced=n-z.avail_out;
  return rc==Z_STREAM_END?Status::Done:rc==Z_OK?Status::Ok:Status::Error;
 }
 ~InflateStream() {inflateEnd(&z);}
};
constexpr int ZIP_METHOD_STORED=0,ZIP_METHOD_DEFLATED=8;
int earlyStops=0;
bool streamDeflatedPrefix(HalFile&,Print&,size_t,size_t,size_t,CancelCheck={}) {++earlyStops;return true;}
namespace zipParsing {bool rangeWithin(size_t offset,size_t count,size_t size) {return offset<=size&&count<=size-offset;}}
struct ZipFile {
 struct FileStatSlim {int method;size_t compressedSize,uncompressedSize;} stat;
 HalFile file;
 bool loadFileStatSlim(const char*,FileStatSlim* out) {*out=stat;return true;}
 long getDataOffset(const FileStatSlim&) {return 0;}
 bool readFileToStream(const char*,Print&,size_t,bool=false,size_t=0,CancelCheck={});
};
struct ScopedOpenClose {explicit ScopedOpenClose(ZipFile&){} explicit operator bool() const{return true;}};
'''+stream+r'''
int main() {
 size_t size=0;
 memory::headroom=false;memory::attempts=0;
 assert(!readPngIntoPsram("x",size)&&memory::attempts==0);
 memory::headroom=true;memory::failAt=1;
 assert(!readPngIntoPsram("x",size));
 memory::failAt=0;Storage.limit=10;
 assert(!readPngIntoPsram("x",size));
 Storage.limit=SIZE_MAX;Storage.size=PNG_PSRAM_MAX_BYTES+1;memory::attempts=0;
 assert(!readPngIntoPsram("x",size)&&memory::attempts==0);
 Storage.size=100000;auto png=readPngIntoPsram("x",size);assert(png&&size==100000&&png.get()[99999]==0x5a);
 std::vector<uint8_t> plain(180000);for(size_t i=0;i<plain.size();++i)plain[i]=i*17;
 for(bool psram:{false,true}) for(int method:{0,8}) for(int fail:{0,1,2}) {
  memory::headroom=psram;memory::failAt=fail;memory::attempts=0;
  ZipFile zip;zip.stat.method=method;zip.stat.uncompressedSize=plain.size();
  if(method==0) zip.file.data=plain;
  else {
   zip.file.data.resize(compressBound(plain.size()));z_stream z{};
   assert(deflateInit2(&z,6,Z_DEFLATED,-15,8,Z_DEFAULT_STRATEGY)==Z_OK);
   z.next_in=plain.data();z.avail_in=plain.size();z.next_out=zip.file.data.data();z.avail_out=zip.file.data.size();
   assert(deflate(&z,Z_FINISH)==Z_STREAM_END);zip.file.data.resize(z.total_out);deflateEnd(&z);
  }
  zip.stat.compressedSize=zip.file.size();Print out;
  assert(zip.readFileToStream("x",out,8192,false,65536));assert(out.data==plain);
  bool large=psram&&fail!=1&&!(method==8&&fail==2);
  assert(zip.file.largestRead<=(large?65536u:8192u));
  zip.file.seek(0);Print shortOut;shortOut.limit=1;
  assert(!zip.readFileToStream("x",shortOut,8192,false,65536));
  zip.file.seek(0);assert(zip.readFileToStream("x",shortOut,8192,true,65536));
  zip.stat.compressedSize=zip.file.size()+1;assert(!zip.readFileToStream("x",out,8192,false,65536));
 }
 assert(earlyStops>0);
 struct Cancellation {int checks=0;int at=2;} state;
 CancelCheck cancel{&state,[](void* p){auto& c=*static_cast<Cancellation*>(p);return ++c.checks>=c.at;}};
 Storage.size=100000;Storage.limit=SIZE_MAX;memory::headroom=true;memory::failAt=0;
 assert(!readPngIntoPsram("x",size,cancel));
 state.checks=0;state.at=1;ZipFile z;Print sink;
 assert(!z.readFileToStream("x",sink,8192,false,65536,cancel));
 state.checks=0;state.at=3;z.stat={0,plain.size(),plain.size()};z.file.data=plain;
 assert(!z.readFileToStream("x",sink,8192,false,65536,cancel));
 assert(z.file.largestRead<=16384);

}
'''
        with tempfile.TemporaryDirectory() as d:
            cpp=Path(d)/'check.cpp';exe=Path(d)/'check';cpp.write_text(program)
            subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror','-I'+str(ROOT/'lib/Memory'),str(cpp),'-lz','-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
        dimensions=method(png,'bool PngToFramebufferConverter::getDimensionsStatic(')
        self.assertNotIn('readPngIntoPsram',dimensions)
        self.assertNotIn('openRAM',dimensions)

if __name__=='__main__':
    unittest.main()
