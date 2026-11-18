"""Compile real WebDAV canonicalization/protection with the reviewed native String shim."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
SIM=Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/"build/test/_deps/crosspoint_simulator-src"

def method(source,name):
    start=source.index(name);end=source.index("{",start)+1;depth=1
    while depth:
        depth+=(source[end]=="{")-(source[end]=="}");end+=1
    return source[start:end]

source=(ROOT/"src/network/WebDAVHandler.cpp").read_text()
fs=(ROOT/"lib/FsHelpers/FsHelpers.cpp").read_text()
code=r"""
#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <WString.h>
#include <HiddenItems.h>
#include <network/ProtectedPaths.h>
namespace FsHelpers {
"""+"\n".join(method(fs,name) for name in ["bool isHexDigit(","uint8_t hexValue(","std::string decodeUriEscapes(","std::string normalisePath("])+r"""
}
struct WebServer{
 String request,destination;
 String uri()const{return request;}
 String header(const char*)const{return destination;}
 static String urlDecode(const String& value){return String(FsHelpers::decodeUriEscapes(value.c_str()));}
};
struct WebDAVHandler{
 String getRequestPath(WebServer&)const;
 String getDestinationPath(WebServer&)const;
 bool isProtectedPath(const String&)const;
};
"""+"\n".join(method(source,name) for name in ["String WebDAVHandler::getRequestPath(","String WebDAVHandler::getDestinationPath(","bool WebDAVHandler::isProtectedPath("])+r"""
int main(){
 WebDAVHandler handler;
 for(const char* path:{"/.crosspoint/wifi.json","/.crosspoint/opds.json.bak","/ .crosspoint /KOREADER.JSON. ",
  "/books/../.crosspoint/wifi.json","/%2ecrosspoint/opds.json","/CROSSP~1/WIFI.JSON","/safe/OPDS~1.JSO",
  "/.hidden/file","/system volume information/file","/Books/XTCache/file"}){
  WebServer server;server.request=path;
  assert(handler.isProtectedPath(handler.getRequestPath(server)));
  server.destination=String("https://host.test")+path;
  assert(handler.isProtectedPath(handler.getDestinationPath(server)));
 }
 for(const char* path:{"/","/books/book.epub","/books/fonts/book.epub","/safe/library/BOOK~1.EPU","/myfiles/cover.bmp"}){
  WebServer server;server.request=path;
  assert(!handler.isProtectedPath(handler.getRequestPath(server)));
  server.destination=String("https://host.test")+path;
  assert(!handler.isProtectedPath(handler.getDestinationPath(server)));
 }
}
"""
listing=method(source,"void WebDAVHandler::handlePropfind(")
assert listing.index("if (isProtectedPath(path))") < listing.index("Storage.exists(")
assert "if (!isProtectedPath(childPath))" in listing
for signature in ["void WebDAVHandler::handleGet(","void WebDAVHandler::handlePut(","void WebDAVHandler::handleDelete(","void WebDAVHandler::handleMkcol(","void WebDAVHandler::handleMove(","void WebDAVHandler::handleCopy("]:
    assert "isProtectedPath(" in method(source,signature),signature
with tempfile.TemporaryDirectory() as directory:
    cpp=Path(directory)/"dav.cpp";binary=Path(directory)/"dav"
    cpp.write_text(code)
    subprocess.run(["c++","-std=c++20","-Wall","-Wextra","-Werror","-I",str(SIM/"src"),"-I",str(ROOT/"lib/FsHelpers"),"-I",str(ROOT/"src"),str(cpp),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print("Production WebDAV request/destination canonicalization, sensitive aliases, hidden names and shared listing/operation guards pass")
