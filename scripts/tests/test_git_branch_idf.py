import runpy
from pathlib import Path
import unittest

REPO = Path(__file__).resolve().parents[2]
inject_version = runpy.run_path(str(REPO / "scripts/git_branch.py"))["inject_version"]


class Env(dict):
    def GetProjectOption(self, name, default=None):
        return self.get(name, default)

    def AddBuildMiddleware(self, callback):
        self.setdefault("middlewares", []).append(callback)

    def Append(self, **kwargs):
        for key, values in kwargs.items():
            self.setdefault(key, []).extend(values)


class VersionInjectionTest(unittest.TestCase):
    def test_native_idf_sources_receive_version_without_an_object_middleware(self):
        env = Env(PROJECT_DIR=str(REPO), custom_build_mode="dev", PIOFRAMEWORK=["arduino", "espidf"])
        inject_version(env)
        self.assertFalse(env.get("middlewares"))
        defines = dict(env.get("CPPDEFINES", []))
        self.assertIn("CROSSPOINT_VERSION", defines)
        self.assertIn("-dev-", defines["CROSSPOINT_VERSION"])

    def test_existing_arduino_build_keeps_per_file_version_injection(self):
        env = Env(PROJECT_DIR=str(REPO), custom_build_mode="dev", PIOFRAMEWORK=["arduino"])
        inject_version(env)
        self.assertEqual(len(env["middlewares"]), 1)
        self.assertNotIn("CROSSPOINT_VERSION", dict(env.get("CPPDEFINES", [])))


if __name__ == "__main__":
    unittest.main()
