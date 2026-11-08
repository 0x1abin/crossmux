import runpy
import sys
import types
import unittest
import subprocess
import tempfile
from pathlib import Path
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / "metalio_plus_build_only.py"


class BuildOnlyGuardTest(unittest.TestCase):
    def execute(self, targets):
        class UserError(Exception):
            pass

        modules = {name: types.ModuleType(name) for name in ("SCons", "SCons.Errors", "SCons.Script")}
        modules["SCons.Errors"].UserError = UserError
        modules["SCons.Script"].COMMAND_LINE_TARGETS = targets
        with patch.dict(sys.modules, modules):
            try:
                runpy.run_path(str(SCRIPT))
                return True
            except UserError:
                return False

    def test_builds_and_host_checks_are_allowed(self):
        for targets in ([], ["buildprog", "size"], ["clean"], ["compiledb"], ["__idedata"], ["envdump"], ["unit-tests"]):
            with self.subTest(targets=targets):
                self.assertTrue(self.execute(targets))

    def test_all_programming_and_debug_routes_are_rejected(self):
        for target in ("upload", "uploadfs", "uploadfsota", "erase", "erase_flash", "erase_upload",
                       "program", "debug", "__debug", "__test", "unknown-custom-target"):
            with self.subTest(target=target):
                self.assertFalse(self.execute([target]))
                self.assertFalse(self.execute(["size", target]))

    def test_board_identity_does_not_alias_other_metalio(self):
        repo = SCRIPT.parent.parent
        with tempfile.TemporaryDirectory(prefix="metalio-plus-tag-") as tmp:
            root = Path(tmp)
            (root / "BoardConfig.h").write_text("#define FREEINK_DEVICE_METALIO_EINK4_PLUS 1\n")
            source = root / "test.cpp"
            source.write_text('''#include <FirmwareBoardTag.h>
#include <cassert>
#include <cstring>
int main(){
 const char* self="CROSSPOINT-BOARD-V1:metalio_eink4_plus;";
 assert(std::strcmp(board_tag::TAG,self)==0);
 for(const char* tag:{self,"CROSSPOINT-BOARD-V1:metalio_eink4;","CROSSPOINT-BOARD-V1:readpico;"}){
  board_tag::Scanner scanner;
  for(size_t i=0;i<std::strlen(tag);++i)scanner.feed(reinterpret_cast<const uint8_t*>(tag+i),1);
  assert(scanner.mismatch()==(std::strcmp(tag,self)!=0));
 }
}
'''.replace('#include <cstring>', '#include <cstring>\n#include <initializer_list>'))
            exe = root / "test"
            subprocess.run(["c++", "-std=c++17", "-I"+str(root), "-I"+str(repo / "src/network"),
                            str(repo / "src/network/FirmwareBoardTag.cpp"), str(source), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
