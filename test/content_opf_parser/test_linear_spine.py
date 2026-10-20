"""Exercise production metadata IO, linear navigation and progress mapping on host storage."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def method(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


ZIP_STUB = r'''
#pragma once
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <tuple>
class ZipFile {
 public:
  struct SizeTarget { uint64_t hash; uint16_t len; uint16_t index; };
  inline static std::map<std::string, uint32_t> sizes;
  explicit ZipFile(const std::string&) {}
  bool open() { return true; }
  void close() {}
  static uint64_t fnvHash64(const char* text, size_t size) {
    uint64_t hash=14695981039346656037ULL;
    for(size_t i=0;i<size;++i) {hash^=static_cast<uint8_t>(text[i]);hash*=1099511628211ULL;}
    return hash;
  }
  bool getInflatedFileSize(const char* path,size_t* out) const {
    auto found=sizes.find(path);if(found==sizes.end()) return false;*out=found->second;return true;
  }
  int fillUncompressedSizes(const std::deque<SizeTarget>& targets,std::deque<uint32_t>& out) const {
    int count=0;
    for(const auto& target:targets) for(const auto& [name,size]:sizes) {
      if(name.size()==target.len && fnvHash64(name.c_str(),name.size())==target.hash) {
        out[target.index]=size;++count;break;
      }
    }
    return count;
  }
};
'''

PROGRAM = r'''
#include <HardwareSerial.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "Epub/BookMetadataCache.h"
#include "FsHelpers.h"
#include "ZipFile.h"
#include "CrossPointPosition.h"
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
HardwareSerial Serial;

// Use real metadata/SD IO. ZIP central-directory size lookup is the only storage seam.
void build(const std::string& cache,const std::vector<bool>& flags,const std::vector<uint32_t>& sizes) {
  assert(flags.size()==sizes.size());
  assert(Storage.ensureDirectoryExists(cache.c_str()));
  ZipFile::sizes.clear();
  BookMetadataCache metadata(cache);
  assert(metadata.beginWrite());assert(metadata.beginContentOpfPass());
  for(size_t i=0;i<flags.size();++i) {
    const auto name="chapter"+std::to_string(i)+".xhtml";
    ZipFile::sizes[name]=sizes[i];metadata.createSpineEntry(name,flags[i]);
  }
  assert(metadata.endContentOpfPass());assert(metadata.beginTocPass());
  assert(metadata.endTocPass());assert(metadata.endWrite());
  assert(metadata.buildBookBin("/fixture.epub",{}));assert(metadata.cleanupTmpFiles());
}

// The facade compiles the production Epub method bodies below against the real cache.
class Epub {
 public:
  std::unique_ptr<BookMetadataCache> bookMetadataCache;
  explicit Epub(const std::string& cache):bookMetadataCache(std::make_unique<BookMetadataCache>(cache)) {
    assert(bookMetadataCache->load());
  }
  bool isLinearSpineItem(int) const;
  int getSpineItemsCount() const;
  BookMetadataCache::SpineEntry getSpineItem(int) const;
  size_t getCumulativeSpineItemSize(int) const;
  size_t getBookSize() const;
  int getSpineIndexForTextReference() const;
  int getLinearSpineItemsCount() const;
  int getSpineIndexForLinearIndex(int) const;
  int getLinearIndexForSpineIndex(int) const;
  int getNextLinearSpineIndex(int) const;
  int getPreviousLinearSpineIndex(int) const;
  float calculateProgress(int,float) const;
  bool findSpineForProgress(float,int&,float&) const;
  int resolveHrefToSpineIndex(const std::string&) const;
};
@EPUB_METHODS@

struct GfxRenderer {};
struct SavedProgressPosition { std::string xpath; float percentage; };
class ProgressMapper {
 public:
  static CrossPointPosition toCrossPointImpl(const std::shared_ptr<Epub>&,
      const SavedProgressPosition&,GfxRenderer&,int,int,int,int,float);
};
namespace {
@XPATH_HELPERS@
// These tests map chapter-start XPath/spine and percentages; no text streaming is needed.
struct ParagraphStreamer {
  template<class... Args> explicit ParagraphStreamer(Args...) {}
  size_t getTargetVisChars() const {return 0;}
  int getParagraphAtMatch() const {return 0;}
  int getListItemAtMatch() const {return 0;}
  const char* getCapturedAnchorId() const {return nullptr;}
  bool found() const {return false;}
};
bool streamSpine(const std::shared_ptr<Epub>&,int,ParagraphStreamer&) {return false;}
}
struct Section {
  Section(const std::shared_ptr<Epub>&,int,GfxRenderer&) {}
  std::optional<int> getCachedPageCount() {return 11;}
  std::optional<int> getPageForVisibleTextOffset(uint32_t,bool) {return std::nullopt;}
  std::optional<int> getPageForListItemIndex(uint16_t) {return std::nullopt;}
  std::optional<int> getPageForAnchor(const std::string&) {return std::nullopt;}
  std::optional<int> getPageForParagraphIndex(uint16_t) {return std::nullopt;}
};
@MAPPER_METHOD@

uint32_t read32(std::fstream& file,size_t offset) {
  uint32_t value=0;file.seekg(offset);file.read(reinterpret_cast<char*>(&value),4);assert(file);return value;
}
void corrupt(const std::filesystem::path& root,const std::string& cache,int kind) {
  const auto path=root/cache.substr(1)/"book.bin";
  if(kind==2) {std::filesystem::resize_file(path,std::filesystem::file_size(path)-1);return;}
  std::fstream file(path,std::ios::in|std::ios::out|std::ios::binary);
  if(kind==0) {file.seekp(0);file.put(11);return;}
  const uint32_t lut=read32(file,1),entry=read32(file,lut),length=read32(file,entry);
  file.seekp(entry+4+length+4+2);file.put(2);
}
int main(int argc,char** argv) {
  assert(argc==2);setenv("CROSSPOINT_SIM_SD",argv[1],1);assert(Storage.begin());
  const std::filesystem::path root=argv[1];
  const std::vector<bool> flags={false,true,false,true,true,false};
  const std::vector<uint32_t> sizes={20,100,80,0,100,200};
  build("/linear",flags,sizes);
  auto book=std::make_shared<Epub>("/linear");
  assert(book->getSpineItemsCount()==6);assert(book->getLinearSpineItemsCount()==3);
  assert(book->getBookSize()==200);
  const std::vector<uint32_t> cumulative={0,100,100,100,200,200};
  for(int i=0;i<6;++i) {
    bool cached=!flags[i];assert(book->bookMetadataCache->getLinear(i,cached));assert(cached==flags[i]);
    assert(book->getSpineItem(i).linear==flags[i]);assert(book->getCumulativeSpineItemSize(i)==cumulative[i]);
  }
  assert(book->getSpineIndexForLinearIndex(0)==1);assert(book->getSpineIndexForLinearIndex(1)==3);
  assert(book->getSpineIndexForLinearIndex(2)==4);assert(book->getSpineIndexForLinearIndex(3)==-1);
  assert(book->getSpineIndexForLinearIndex(-1)==-1);
  assert(book->getLinearIndexForSpineIndex(1)==0);assert(book->getLinearIndexForSpineIndex(3)==1);
  assert(book->getLinearIndexForSpineIndex(4)==2);assert(book->getLinearIndexForSpineIndex(2)==-1);
  assert(book->getNextLinearSpineIndex(-1)==1);assert(book->getNextLinearSpineIndex(1)==3);
  assert(book->getNextLinearSpineIndex(4)==-1);assert(book->getNextLinearSpineIndex(-2)==-1);
  assert(book->getPreviousLinearSpineIndex(6)==4);assert(book->getPreviousLinearSpineIndex(4)==3);
  assert(book->getPreviousLinearSpineIndex(1)==-1);assert(book->getPreviousLinearSpineIndex(7)==-1);
  assert(book->getSpineIndexForTextReference()==1);
  book->bookMetadataCache->coreMetadata.textReferenceHref="chapter0.xhtml";
  assert(book->getSpineIndexForTextReference()==1);
  book->bookMetadataCache->coreMetadata.textReferenceHref="chapter4.xhtml";
  assert(book->getSpineIndexForTextReference()==4);
  assert(book->resolveHrefToSpineIndex("chapter2.xhtml#note")==2);
  assert(book->calculateProgress(1,0)==0);assert(book->calculateProgress(1,1)==.5f);
  int spine=-7;float intra=-7;
  for(const auto [fraction,expectedSpine,expectedIntra]:
      {std::tuple{-1.f,1,0.f},std::tuple{0.f,1,0.f},std::tuple{.25f,1,.5f},
       std::tuple{.5f,4,0.f},std::tuple{.75f,4,.5f},std::tuple{1.f,4,1.f},std::tuple{2.f,4,1.f}}) {
    assert(book->findSpineForProgress(fraction,spine,intra));assert(spine==expectedSpine);
    assert(intra==expectedIntra);
  }
  spine=-7;intra=-7;assert(!book->findSpineForProgress(NAN,spine,intra));assert(spine==-7&&intra==-7);
  GfxRenderer renderer;
  for(const auto [fraction,expectedSpine,expectedPage]:
      {std::tuple{0.f,1,0},std::tuple{.5f,4,0},std::tuple{1.f,4,10}}) {
    auto position=ProgressMapper::toCrossPointImpl(book,{"",fraction},renderer,-1,0,0,-1,-1);
    assert(position.spineIndex==expectedSpine&&position.pageNumber==expectedPage);
  }
  auto note=ProgressMapper::toCrossPointImpl(book,{"/body/DocFragment[3]",.9f},renderer,-1,0,0,-1,-1);
  assert(note.spineIndex==2&&note.hasResolvedSpineIndex&&note.hasVisibleTextOffset&&note.visibleTextOffset==0);
  note=ProgressMapper::toCrossPointImpl(book,{"",.9f},renderer,-1,0,0,2,.5f);
  assert(note.spineIndex==2&&note.pageNumber==5);
  for(int kind=0;kind<3;++kind) {
    build("/corrupt",flags,sizes);corrupt(root,"/corrupt",kind);
    BookMetadataCache cache("/corrupt");assert(!cache.load());
    assert(!std::filesystem::exists(root/"corrupt/book.bin"));
  }
  for(const size_t count:{size_t{70},size_t{1025}}) {
    std::vector<bool> manyFlags(count);std::vector<uint32_t> manySizes(count,1);
    for(size_t i=0;i<count;++i) manyFlags[i]=(i%3)==1;
    build("/many",manyFlags,manySizes);Epub many("/many");
    assert(many.getLinearSpineItemsCount()==static_cast<int>((count+1)/3));
    for(size_t i=0;i<count;++i) assert(many.getSpineItem(i).linear==manyFlags[i]);
    bool value=false;assert(many.bookMetadataCache->getLinear(32,value)==(count<=1024));
  }
  build("/notes-only",{false,false},{10,20});auto notes=std::make_shared<Epub>("/notes-only");
  assert(notes->getLinearSpineItemsCount()==0&&notes->getSpineIndexForTextReference()==-1);
  assert(notes->getNextLinearSpineIndex(-1)==-1&&notes->getPreviousLinearSpineIndex(2)==-1);
  spine=-7;intra=-7;assert(!notes->findSpineForProgress(0,spine,intra));assert(spine==-7&&intra==-7);
  note=ProgressMapper::toCrossPointImpl(notes,{"/body/DocFragment[2]",0},renderer,-1,0,0,-1,-1);
  assert(note.spineIndex==1&&note.hasResolvedSpineIndex);
  puts("metadata v12, linear navigation, zero-weight progress and explicit notes: OK");
}
'''


def main():
    simulator, compiler = Path(sys.argv[1]), sys.argv[2]
    epub = (ROOT / 'lib/Epub/Epub.cpp').read_text()
    mapper = (ROOT / 'lib/KOReaderSync/ProgressMapper.cpp').read_text()
    names = ('int Epub::getSpineItemsCount(', 'bool Epub::isLinearSpineItem(',
             'BookMetadataCache::SpineEntry Epub::getSpineItem(',
             'size_t Epub::getCumulativeSpineItemSize(', 'size_t Epub::getBookSize(',
             'int Epub::getSpineIndexForTextReference(', 'int Epub::getLinearSpineItemsCount(',
             'int Epub::getSpineIndexForLinearIndex(', 'int Epub::getLinearIndexForSpineIndex(',
             'int Epub::getNextLinearSpineIndex(', 'int Epub::getPreviousLinearSpineIndex(',
             'float Epub::calculateProgress(', 'bool Epub::findSpineForProgress(',
             'int Epub::resolveHrefToSpineIndex(')
    helpers = mapper[mapper.index('namespace {') + len('namespace {'):mapper.index('class ParagraphStreamer')]
    source = (PROGRAM.replace('@EPUB_METHODS@', '\n'.join(method(epub, name) for name in names))
              .replace('@XPATH_HELPERS@', helpers)
              .replace('@MAPPER_METHOD@', method(mapper, 'CrossPointPosition ProgressMapper::toCrossPointImpl(')))
    with tempfile.TemporaryDirectory(prefix='epub-linear-') as directory:
        work = Path(directory)
        (work / 'ZipFile.h').write_text(ZIP_STUB)
        (work / 'check.cpp').write_text(source)
        includes = (work, simulator / 'src', ROOT / 'lib/Epub', ROOT / 'lib/Serialization',
                    ROOT / 'lib/Memory', ROOT / 'lib/Logging', ROOT / 'lib/Utf8',
                    ROOT / 'lib/FsHelpers', ROOT / 'lib/KOReaderSync')
        command = [compiler, '-std=c++20', '-DCROSSPOINT_EMULATED=1', '-DDESTRUCTOR_CLOSES_FILE=1',
                   *('-I' + str(path) for path in includes), str(work / 'check.cpp'),
                   str(ROOT / 'lib/Epub/Epub/BookMetadataCache.cpp'),
                   str(ROOT / 'lib/FsHelpers/FsHelpers.cpp'), str(ROOT / 'lib/Utf8/Utf8.cpp'),
                   str(simulator / 'src/HalStorage.cpp'), '-o', str(work / 'check')]
        subprocess.run(command, check=True)
        (work / 'sd').mkdir()
        subprocess.run([str(work / 'check'), str(work / 'sd')], check=True)


if __name__ == '__main__':
    main()
