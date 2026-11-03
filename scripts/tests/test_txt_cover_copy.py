"""Execute the complete TXT cover method with short/error/stalled file I/O."""
import unittest

from test_reading_ui_regressions import ROOT, method, run_cpp


class TxtCoverCopyTest(unittest.TestCase):
    def test_complete_copy_or_cleanup_and_retry(self):
        source = (ROOT / 'lib/Txt/Txt.cpp').read_text()
        program = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
enum class Fault {None, ReadError, EarlyEof, ShortWrite, ShortReads, ZeroRead, OpenSource, OpenDestination};
Fault fault=Fault::None;
std::vector<uint8_t> sourceBytes(2305), output;
bool cached=false, sourceOpen=false, destinationOpen=false;
int reads=0, failures=0;
struct HalFile {
 bool input=false, opened=false; size_t position=0;
 ~HalFile() {close();}
 uint64_t fileSize64() const {return input?sourceBytes.size():output.size();}
 int read(void* data, size_t count) {
  assert(input && opened && ++reads<1000 && count<=1024);
  switch(fault) {
   case Fault::ReadError: if(position>=1024) return -1; break;
   case Fault::EarlyEof: if(position>=1024) return 0; break;
   case Fault::ZeroRead: return 0;
   case Fault::ShortReads: count=std::min(count,size_t(7)); break;
   case Fault::None: case Fault::ShortWrite: case Fault::OpenSource: case Fault::OpenDestination: break;
  }
  count=std::min(count,sourceBytes.size()-position);
  std::memcpy(data,sourceBytes.data()+position,count); position+=count;
  return static_cast<int>(count);
 }
 size_t write(const void* data, size_t count) {
  assert(!input && opened && count<=1024);
  if(fault==Fault::ShortWrite && output.size()>=1024) --count;
  const auto* bytes=static_cast<const uint8_t*>(data);
  output.insert(output.end(),bytes,bytes+count); return count;
 }
 void flush() {}
 void close() {
  if(!opened) return;
  opened=false;
  if(input) sourceOpen=false; else destinationOpen=false;
 }
};
struct {
 bool exists(const char*) {return cached;}
 bool openFileForRead(const char*,const std::string&,HalFile& file) {
  if(fault==Fault::OpenSource) return false;
  file.input=true; file.opened=true; sourceOpen=true; return true;
 }
 bool openFileForWrite(const char*,const std::string&,HalFile& file) {
  if(fault==Fault::OpenDestination) return false;
  file.opened=true; destinationOpen=true; cached=true; output.clear(); return true;
 }
 bool remove(const char*) {
  assert(!destinationOpen); cached=false; output.clear(); return true;
 }
} Storage;
namespace FsHelpers {
bool hasBmpExtension(const std::string&) {return true;}
bool hasJpgExtension(const std::string&) {return false;}
}
struct JpegToBmpConverter {static bool jpegFileToBmpStream(HalFile&,HalFile&) {return false;}};
struct Txt {
 std::string getCoverBmpPath() const {return "/cache.bmp";}
 std::string findCoverImage() const {return "/source.bmp";}
 void setupCacheDir() const {}
 bool generateCoverBmp() const;
};
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
''' + method(source, 'bool Txt::generateCoverBmp(') + r'''
int main() {
 for(size_t i=0;i<sourceBytes.size();++i) sourceBytes[i]=static_cast<uint8_t>(i*37+11);
 constexpr std::array faults={Fault::None,Fault::ReadError,Fault::EarlyEof,Fault::ShortWrite,
                             Fault::ShortReads,Fault::ZeroRead,Fault::OpenSource,Fault::OpenDestination};
 for(const auto scenario:faults) {
  fault=scenario; cached=false; sourceOpen=false; destinationOpen=false; reads=0; output.clear();
  Txt txt;
  const bool succeeded=txt.generateCoverBmp();
  const bool expected=scenario==Fault::None || scenario==Fault::ShortReads;
  const bool correct=succeeded==expected && !sourceOpen && !destinationOpen &&
                     (expected ? cached && output==sourceBytes : !cached && output.empty());
  std::printf("scenario=%u complete-or-cleaned=%s\n",unsigned(scenario),correct?"PASS":"FAIL");
  failures+=!correct;
  fault=Fault::None;
  const int previousReads=reads;
  const bool retried=txt.generateCoverBmp() && cached && output==sourceBytes;
  const bool reused=!expected || reads==previousReads;
  std::printf("scenario=%u retry-or-reuse=%s\n",unsigned(scenario),retried&&reused?"PASS":"FAIL");
  failures+=!(retried&&reused);
 }
 return failures?1:0;
}
'''
        run_cpp(program)


if __name__ == '__main__':
    unittest.main()
