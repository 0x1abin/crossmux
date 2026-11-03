"""Compile production plugin discovery with an in-memory SD card."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PluginDiskDiscoveryTest(unittest.TestCase):
    def test_disabled_plugins_still_have_disk_identity_and_root_precedence(self):
        with tempfile.TemporaryDirectory(prefix="plugin-discovery-", dir=os.environ.get("TMPDIR")) as directory:
            temp = Path(directory)
            (temp / "Arduino.h").write_text("struct {unsigned getFreeHeap(){return 1000000;} unsigned getMaxAllocHeap(){return 1000000;}} ESP;\n")
            (temp / "Logging.h").write_text("#define LOG_ERR(...) ((void)0)\n")
            (temp / "Memory.h").write_text("namespace memory {template<class... T> bool hasAllocationHeadroom(T...){return true;}}\n")
            (temp / "CrossPointSettings.h").write_text("struct {bool pluginsEnabled=false;} SETTINGS;\n")
            (temp / "HalStorage.h").write_text(r'''
#pragma once
#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <vector>
inline std::set<std::string> directories, files;
struct HalFile {
  std::string path; size_t position=0;
  explicit operator bool()const{return directories.count(path)||files.count(path);}
  bool isDirectory()const{return directories.count(path);}
  size_t getName(char* out,size_t size)const{
    auto name=path.substr(path.find_last_of('/')+1);
    if(name.size()+1>size)return 0;
    std::memcpy(out,name.c_str(),name.size()+1);return name.size();
  }
  HalFile openNextFile(){
    std::vector<std::string> entries;
    for(const auto& candidate:directories)
      if(candidate.starts_with(path+"/")&&candidate.find('/',path.size()+1)==std::string::npos)entries.push_back(candidate);
    return position<entries.size()?HalFile{entries[position++]}:HalFile{};
  }
};
struct {bool exists(const char* p){return directories.count(p)||files.count(p);}
        HalFile open(const char* p){return {p};}} Storage;
''')
            harness = temp / "test.cpp"
            harness.write_text('#include "' + str(ROOT / "src/util/PluginLocations.cpp") + '"\n' + r'''
#include <cassert>
int main(){
  directories={"/.crosspoint/plugins","/plugins","/.plugins",
    "/.crosspoint/plugins/first","/plugins/first","/plugins/second","/.plugins/pending"};
  files={"/.crosspoint/plugins/first/device.json","/plugins/first/device.json",
    "/plugins/second/plugin.js","/.plugins/pending/device.json","/.plugins/pending/.installing"};
  assert(PluginLocations::anyPluginInstalledOnDisk());
  for(bool enabled:{false,true}){
    SETTINGS.pluginsEnabled=enabled;
    const auto disk=PluginLocations::scanPluginsOnDisk();
    assert(disk.size()==3);
    assert(disk[0].dir=="/.crosspoint/plugins/first");
    assert(disk[1].dir=="/plugins/second");
    assert(disk[2].dir=="/.plugins/pending");
    assert(PluginLocations::findPluginDirOnDisk("first")=="/.crosspoint/plugins/first");
    assert(PluginLocations::scanPlugins().size()==(enabled?2:0));
    assert(PluginLocations::findPluginDir("first")== (enabled?"/.crosspoint/plugins/first":""));
    assert(PluginLocations::findPluginDir("pending").empty());
  }
  directories.clear();files.clear();assert(!PluginLocations::anyPluginInstalledOnDisk());
}
''')
            binary = temp / "test"
            subprocess.run(["c++", "-std=c++20", "-Wall", "-Wextra", "-I" + str(temp), str(harness), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
