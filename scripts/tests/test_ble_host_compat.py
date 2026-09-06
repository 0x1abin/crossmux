import fnmatch
from pathlib import Path
import runpy
import tempfile
import unittest


class Node:
    def __init__(self, path):
        self.path = Path(path)

    def get_abspath(self):
        return str(self.path)


class BleHostCompatTest(unittest.TestCase):
    def test_build_local_copy_tracks_source_and_never_writes_sdk(self):
        class BuildEnv(dict):
            def subst(self, value):
                return value.replace("$PROJECT_DIR", str(root))

            def AddBuildMiddleware(self, callback, pattern):
                self.callback, self.pattern = callback, pattern

            def Command(self, target, source, action):
                self.source, self.action = source, action
                return [Node(target.replace("$BUILD_DIR", self["BUILD_DIR"]))]

            def Object(self, nodes, **kwargs):
                self.flags = kwargs
                return nodes

            def Depends(self, target, dependency):
                self.setdefault("dependencies", []).append((target, dependency))

        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as directory:
            env = BuildEnv(BUILD_DIR=str(Path(directory) / "build"), CPPPATH=["existing"])
            module = runpy.run_path(
                str(root / "scripts/patch_ble_keyboard_host.py"),
                init_globals={"env": env, "Import": lambda _: None},
            )
            original = (root / "freeink-sdk/libs/network/BleKeyboardHost/src/BleKeyboardHost.cpp").read_text()
            sdk = Path(directory) / "SDK/BleKeyboardHost/src/BleKeyboardHost.cpp"
            sdk.parent.mkdir(parents=True)
            sdk.write_text(original)
            self.assertTrue(fnmatch.fnmatch(str(sdk), env.pattern))
            self.assertFalse(fnmatch.fnmatch("src/BleInput.cpp", env.pattern))
            source = Node(sdk)
            target = env.callback(env, source)
            self.assertIs(env.source, source)
            self.assertEqual(env.flags["CPPPATH"], ["existing", str(sdk.parent)])
            self.assertEqual(env.flags["CCFLAGS"],
                             ["-include", str(root / "src/platform/BtLibraryInUseShim.h")])
            self.assertIn((target, str(root / "src/platform/BtLibraryInUseShim.h")), env["dependencies"])
            self.assertIn(([target], str(root / "scripts/patch_ble_keyboard_host.py")), env["dependencies"])
            self.assertEqual(env["CPPPATH"], ["existing"])
            self.assertTrue(target.path.is_relative_to(Path(directory) / "build"))

            for content in (original, original + "\n// dependency changed\n"):
                sdk.write_text(content)
                env.action([target], [source], env)
                self.assertEqual(sdk.read_text(), content)
                self.assertEqual(target.path.read_text(), content.replace(module["METHOD"], "", 1))

            previous = target.path.read_text()
            for content in ("// unexpected SDK", original + module["METHOD"]):
                sdk.write_text(content)
                with self.assertRaisesRegex(RuntimeError, "Unsupported BleKeyboardHost"):
                    env.action([target], [source], env)
                self.assertEqual(sdk.read_text(), content)
                self.assertEqual(target.path.read_text(), previous)


if __name__ == "__main__":
    unittest.main()
