"""Exercise both HAL hash adapters with OpenSSL vectors and injected API failures."""
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FLAGS = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'openssl'], text=True))
with tempfile.TemporaryDirectory(prefix='hal-sha256-') as directory:
    tmp = Path(directory)
    (tmp / 'Logging.h').write_text('#pragma once\n#define LOG_ERR(...) ((void)0)\n')
    (tmp / 'HashModel.h').write_text('''#pragma once
#include <openssl/sha.h>
#include <cstdint>
#include <cstddef>
inline int failure=0, freed=0, updates=0;
struct HashModel {SHA256_CTX value;};
inline int start(HashModel* c){return failure==1?-1:(SHA256_Init(&c->value)==1?0:-1);}
inline int update(HashModel* c,const uint8_t* p,size_t n){++updates;return failure==2?-1:(SHA256_Update(&c->value,p,n)==1?0:-1);}
inline int finish(HashModel* c,uint8_t* p){return failure==3?-1:(SHA256_Final(p,&c->value)==1?0:-1);}
''')
    (tmp / 'test.cpp').write_text('''#include <cassert>
#include <cstring>
#include <cstdio>
#include <HalSha256.h>
static void expect(HalSha256& hash,const char* expected){
 uint8_t digest[32];assert(hash.finish(digest));char hex[65];
 for(int i=0;i<32;++i)std::sprintf(hex+i*2,"%02x",digest[i]);
 assert(std::strcmp(hex,expected)==0);assert(!hash.finish(digest));
}
int main(){
 {HalSha256 h;expect(h,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");}
 {HalSha256 h;assert(h.update((const uint8_t*)"a",1));assert(h.update((const uint8_t*)"bc",2));
  expect(h,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");}
 for(int stage=1;stage<=3;++stage){
  failure=stage;int before=freed;
  {HalSha256 h;uint8_t digest[32];
   if(stage<3){assert(!h.update((const uint8_t*)"abc",3));const auto prior=updates;
              failure=0;assert(!h.update((const uint8_t*)"abc",3));assert(updates==prior);}
   else assert(h.update((const uint8_t*)"abc",3));
   assert(!h.finish(digest));}
  assert(freed==before+1);
 }
}
''')
    for backend in ('mbedtls', 'psa'):
        include = tmp / backend
        include.mkdir()
        if backend == 'mbedtls':
            folder = include / 'mbedtls'
            folder.mkdir()
            (folder / 'sha256.h').write_text('''#pragma once
#include <HashModel.h>
using mbedtls_sha256_context=HashModel;
inline void mbedtls_sha256_init(HashModel*){}
inline int mbedtls_sha256_starts(HashModel* c,int){return start(c);}
inline int mbedtls_sha256_update(HashModel* c,const uint8_t* p,size_t n){return update(c,p,n);}
inline int mbedtls_sha256_finish(HashModel* c,uint8_t* p){return finish(c,p);}
inline void mbedtls_sha256_free(HashModel*){++freed;}
''')
        else:
            folder = include / 'psa'
            folder.mkdir()
            (folder / 'crypto.h').write_text('''#pragma once
#include <HashModel.h>
using psa_hash_operation_t=HashModel;
#define PSA_HASH_OPERATION_INIT {}
#define PSA_SUCCESS 0
#define PSA_ALG_SHA_256 1
inline int psa_crypto_init(){return failure==1?-1:0;}
inline int psa_hash_setup(HashModel* c,int){return start(c);}
inline int psa_hash_update(HashModel* c,const uint8_t* p,size_t n){return update(c,p,n);}
inline int psa_hash_finish(HashModel* c,uint8_t* p,size_t,size_t* n){*n=32;return finish(c,p);}
inline void psa_hash_abort(HashModel*){++freed;}
''')
        binary = tmp / ('test-' + backend)
        subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-deprecated-declarations',
                        '-I' + str(include), '-I' + str(tmp), '-I' + str(ROOT / 'lib/hal'),
                        str(tmp / 'test.cpp'), *FLAGS, '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
print('mbedTLS/PSA SHA256 empty/split vectors, fail-closed behavior and cleanup passed')
