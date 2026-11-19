"""Compile production progress auth-header helpers with the real header validator."""
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def method(source,name):
    start=source.index(name);end=source.index("{",start)+1;depth=1
    while depth:
        depth+=(source[end]=="{")-(source[end]=="}");end+=1
    return source[start:end]

source=(ROOT/"lib/KOReaderSync/KOReaderSyncClient.cpp").read_text()
code=r"""
#include <algorithm>
#include <cassert>
#include <string>
#include <strings.h>
#include <utility>
#include <vector>
#include <HttpHeader.h>
struct Credentials{
 std::vector<HttpHeader> custom;
 const auto& getCustomHeaders()const{return custom;}
 std::string getUsername()const{return "fixture-user";}
 std::string getMd5Password()const{return "fixture-md5";}
 std::string getBasicAuthorization()const{return "Basic fixture";}
} KOREADER_STORE;
namespace freeink{struct SecureHttpClient{
 std::vector<std::pair<std::string,std::string>> headers;
 void addHeader(const std::string& name,const std::string& value){headers.emplace_back(name,value);}
};}
"""+method(source,"void applyCustomHeaders(")+"\n"+method(source,"void applyAuthHeaders(")+r"""
int main(){
 for(const auto& custom:std::vector<HttpHeader>{{},{"Authorization","Bearer valid"},
 {"authorization","Bearer valid"},{"AUTHORIZATION","bad\r\nInjected: value"},
 {"Bad Name","value"},{"X-Custom","ordinary"}}){
  KOREADER_STORE.custom={custom};freeink::SecureHttpClient client;
  applyCustomHeaders(client);applyAuthHeaders(client);
  std::vector<std::string> auth;
  for(const auto& [name,value]:client.headers){
   assert(isValidHttpHeader(name,value));
   if(strcasecmp(name.c_str(),"Authorization")==0)auth.push_back(value);
  }
  const bool valid=isValidHttpHeader(custom)&&strcasecmp(custom.name.c_str(),"Authorization")==0;
  assert(auth==std::vector<std::string>{valid?"Bearer valid":"Basic fixture"});
  assert(std::find(client.headers.begin(),client.headers.end(),std::pair<std::string,std::string>{"x-auth-user","fixture-user"})!=client.headers.end());
 }
}
"""
with tempfile.TemporaryDirectory() as directory:
    cpp=Path(directory)/"auth.cpp";binary=Path(directory)/"auth"
    cpp.write_text(code)
    subprocess.run(["c++","-std=c++20","-Wall","-Wextra","-Werror","-I",str(ROOT/"lib/HttpHeader"),"-I",str(ROOT/"lib/AsciiText"),str(cpp),str(ROOT/"lib/HttpHeader/HttpHeader.cpp"),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print("Production progress auth keeps one valid custom Authorization or Basic fallback and rejects invalid headers")
