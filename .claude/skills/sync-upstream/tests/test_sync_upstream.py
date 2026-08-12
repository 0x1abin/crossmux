import importlib.util
import sys
import unittest
from unittest.mock import call, patch
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / "scripts" / "sync_upstream.py"
SPEC = importlib.util.spec_from_file_location("sync_upstream", SCRIPT)
assert SPEC and SPEC.loader
sync_upstream = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = sync_upstream
SPEC.loader.exec_module(sync_upstream)


class SdkSyncStateTest(unittest.TestCase):
    def test_sdk_already_synced(self):
        self.assertEqual(
            sync_upstream.decide_sdk_action(
                official_in_fork=True,
                sdk_pr_open=False,
                upstream_gitlink_in_fork=True,
            ),
            "update-crossmux-gitlink",
        )

    def test_sdk_behind_opens_pr_first(self):
        self.assertEqual(
            sync_upstream.decide_sdk_action(
                official_in_fork=False,
                sdk_pr_open=False,
                upstream_gitlink_in_fork=True,
            ),
            "open-sdk-pr",
        )

    def test_unmerged_sdk_pr_waits(self):
        self.assertEqual(
            sync_upstream.decide_sdk_action(
                official_in_fork=False,
                sdk_pr_open=True,
                upstream_gitlink_in_fork=True,
            ),
            "wait-for-sdk-pr",
        )

    def test_official_gitlink_absent_from_fork_blocks(self):
        self.assertEqual(
            sync_upstream.decide_sdk_action(
                official_in_fork=True,
                sdk_pr_open=False,
                upstream_gitlink_in_fork=False,
            ),
            "official-gitlink-missing",
        )

    @patch.object(sync_upstream, "git")
    def test_second_phase_updates_gitlink(self, git):
        root = Path("/repo")
        sync_upstream.update_sdk_gitlink(root, "fork-sha")
        self.assertEqual(
            git.call_args_list,
            [
                call(root, "config", "-f", ".gitmodules", "submodule.freeink-sdk.url", sync_upstream.SDK_FORK_URL),
                call(root, "config", "-f", ".gitmodules", "submodule.freeink-sdk.branch", "main"),
                call(root, "add", ".gitmodules"),
                call(root / "freeink-sdk", "fetch", sync_upstream.SDK_FORK_URL, "main"),
                call(root / "freeink-sdk", "checkout", "--detach", "fork-sha"),
                call(root, "add", "freeink-sdk"),
            ],
        )

    def test_sdk_candidate_builds_only_available_hardware(self):
        board_config = "FREEINK_DEVICE_EEGO_A4"
        platformio = "[env:eego_a4]\n[env:mofei_m4]\n"
        self.assertEqual(
            sync_upstream.candidate_build_envs(board_config, platformio),
            ("default", "sticky", "eego_a4"),
        )

    def test_extra_build_environments_are_appended(self):
        args = sync_upstream.parse_args(["publish", "--extra-build-env", "gh_weread_pro"])
        self.assertEqual(
            sync_upstream.candidate_build_envs("", "", args.extra_build_env),
            ("default", "sticky", "gh_weread_pro"),
        )


if __name__ == "__main__":
    unittest.main()
