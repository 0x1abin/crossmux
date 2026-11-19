"""Execute the actual CI dependency block with fake package/download commands."""
from pathlib import Path
import hashlib
import os
import re
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[2]
path=".github/workflows/ci.yml"
source=(ROOT/path).read_text()
original=subprocess.check_output(["git","show","HEAD:"+path],cwd=ROOT,text=True)
def block(text):
    start=text.index("        run: |\n",text.index("      - name: Install cppcheck runtime dependency"))+len("        run: |\n")
    end=text.index("      - name:",start)
    return start,end,"\n".join(line[10:] for line in text[start:end].splitlines() if line.strip())+"\n"
a,b,script=block(source);x,y,_=block(original)
assert source[:a]==original[:x] and source[b:]==original[y:],"unrelated CI configuration changed"
assert "runs-on: ubuntu-24.04" in source and "PLATFORMIO_CORE_DIR" in source
pin="0d1eb051047eb0c637c893f618f45e1d419863bfa3bc821897ee05ad418ba22e"
assert pin in script and 'https://security.ubuntu.com/ubuntu/pool/main/p/pcre3/' in script
# Only the test's expected digest changes; real sha256sum verifies fixture bytes.
payload=b"fixed test archive\n"
script=script.replace(pin,hashlib.sha256(payload).hexdigest())
with tempfile.TemporaryDirectory() as directory:
    root=Path(directory);bin=root/"bin";bin.mkdir();runner=root/"runner with spaces";runner.mkdir()
    archive=root/"archive.deb";archive.write_bytes(payload)
    mock=r"""import os,sys,pathlib,shutil
scenario=os.environ['PCRE_SCENARIO'];args=sys.argv[1:]
with open(os.environ['PCRE_LOG'],'a') as f:f.write(pathlib.Path(sys.argv[0]).name+' '+repr(args)+'\n')
if pathlib.Path(sys.argv[0]).name=='sudo':
 if args[:2]==['apt-get','update']:sys.exit(1 if scenario=='update-fail' else 0)
 if args[:2]==['apt-get','install']:sys.exit(0 if scenario=='apt-success' else 1)
 if args[:2]==['dpkg','-i']:
  assert pathlib.Path(args[2]).read_bytes()==b'fixed test archive\n'
  sys.exit(1 if scenario=='install-fail' else 0)
 raise AssertionError(args)
assert '-O' in args
out=pathlib.Path(args[args.index('-O')+1]);assert out.parent.parent==pathlib.Path(os.environ['RUNNER_TEMP'])
assert out.parent.stat().st_mode&0o777==0o700
if scenario=='download-fail':sys.exit(1)
shutil.copyfile(os.environ['PCRE_ARCHIVE'],out)
if scenario=='tampered':out.write_bytes(b'changed archive')
"""
    for name in ["sudo","wget"]:
        p=bin/name;p.write_text('#!'+sys.executable+'\n'+mock);p.chmod(0o755)
    for scenario in ["apt-success","fallback-success","update-fail","download-fail","tampered","install-fail"]:
        log=root/(scenario+".log")
        env=dict(os.environ,PATH=str(bin)+os.pathsep+os.environ["PATH"],RUNNER_TEMP=str(runner),PCRE_SCENARIO=scenario,PCRE_LOG=str(log),PCRE_ARCHIVE=str(archive))
        result=subprocess.run(["bash","-euo","pipefail","-c",script],env=env,capture_output=True,text=True)
        assert (result.returncode==0)==(scenario in ["apt-success","fallback-success"]),(scenario,result.stderr)
        calls=log.read_text();assert ('dpkg' in calls)==(scenario in ["fallback-success","install-fail"]),(scenario,calls)
        assert ('wget' in calls)==(scenario not in ["apt-success","update-fail"])
        assert not list(runner.iterdir()),(scenario,'private directory was not cleaned')
print("Actual CI apt/fallback/download/hash/install branches fail closed and clean runner-private files; other jobs unchanged")
