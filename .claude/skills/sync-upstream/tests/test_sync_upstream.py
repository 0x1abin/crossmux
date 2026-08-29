import argparse
import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import PropertyMock, patch

SCRIPT = Path(__file__).parents[1] / "scripts" / "sync_upstream.py"
SPEC = importlib.util.spec_from_file_location("sync_upstream", SCRIPT)
assert SPEC and SPEC.loader
sync_upstream = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = sync_upstream
SPEC.loader.exec_module(sync_upstream)


def command(root: Path, *args: str) -> str:
    return subprocess.run(
        list(args), cwd=root, check=True, text=True, stdout=subprocess.PIPE
    ).stdout.strip()


def init_repo(path: Path) -> None:
    path.mkdir()
    command(path, "git", "init", "-b", "main")
    command(path, "git", "config", "user.name", "Sync Test")
    command(path, "git", "config", "user.email", "sync@example.com")


def commit_file(root: Path, name: str, content: str, message: str) -> None:
    (root / name).write_text(content, encoding="utf-8")
    command(root, "git", "add", name)
    command(root, "git", "commit", "-m", message)


def status(name: str, action: str) -> object:
    parent_in_fork = action == "up-to-date"
    open_pr = action == "wait-for-pr"
    return sync_upstream.DependencyStatus(
        name, "a" * 40, "b" * 40, parent_in_fork, open_pr, ()
    )


class DependencyStateTest(unittest.TestCase):
    def test_fork_already_contains_parent(self):
        self.assertEqual(
            sync_upstream.decide_dependency_action(True, False), "up-to-date"
        )

    def test_fork_behind_requires_sync(self):
        self.assertEqual(
            sync_upstream.decide_dependency_action(False, False), "sync-required"
        )

    def test_open_pull_request_blocks_next_stage(self):
        self.assertEqual(
            sync_upstream.decide_dependency_action(False, True), "wait-for-pr"
        )

    def test_dependencies_gate_crossmux_in_order(self):
        self.assertEqual(
            sync_upstream.next_component(
                {
                    "sdk": status("sdk", "sync-required"),
                    "simulator": status("simulator", "up-to-date"),
                }
            ),
            "sdk",
        )
        self.assertEqual(
            sync_upstream.next_component(
                {
                    "sdk": status("sdk", "up-to-date"),
                    "simulator": status("simulator", "wait-for-pr"),
                }
            ),
            "simulator",
        )
        self.assertEqual(
            sync_upstream.next_component(
                {
                    "sdk": status("sdk", "up-to-date"),
                    "simulator": status("simulator", "up-to-date"),
                }
            ),
            "crossmux",
        )


class PinUpdateTest(unittest.TestCase):
    def test_updates_both_simulator_pins_to_one_revision(self):
        sha = "c" * 40
        platformio, cmake = sync_upstream.replace_simulator_pins(
            "simulator=https://github.com/0x1abin/crosspoint-simulator.git#" + "a" * 40,
            "GIT_REPOSITORY https://github.com/0x1abin/crosspoint-simulator.git\n  GIT_TAG "
            + "b" * 40,
            sha,
        )
        pins = sync_upstream.simulator_pins(platformio + "\n" + cmake)
        self.assertEqual(pins, (sha, sha))


class ReviewGateTest(unittest.TestCase):
    def test_pr_body_pairs_each_review_item_with_its_decision(self):
        state = {
            "component": "sdk",
            "upstream_sha": "a" * 40,
            "candidate_id": "review-id",
            "review_items": ["one.cpp", "two.cpp"],
        }
        body = sync_upstream.pr_body(
            state,
            ["Keep the local API.", "Use the combined implementation."],
            False,
        )
        self.assertIn("`one.cpp`: Keep the local API.", body)
        self.assertIn("`two.cpp`: Use the combined implementation.", body)

    def test_resolved_conflict_remains_a_review_item(self):
        state = {
            "component": "sdk",
            "conflict_paths": ["shared.cpp"],
            "overlap_paths": [],
            "review_items": ["shared.cpp"],
        }
        with patch.object(
            sync_upstream, "unmerged_paths", return_value=[]
        ), patch.object(sync_upstream, "write_state"):
            refreshed = sync_upstream.refresh_candidate(Path("/tmp/not-used"), state)
        self.assertEqual(refreshed["conflict_paths"], [])
        self.assertEqual(refreshed["review_items"], ["shared.cpp"])

    def test_publish_requires_review_note_for_overlap(self):
        state = {
            "component": "sdk",
            "conflict_paths": [],
            "overlap_paths": ["shared.cpp"],
            "review_items": ["shared.cpp"],
        }
        args = argparse.Namespace(
            candidate="/tmp/not-used",
            component="sdk",
            review_note=[],
            skip_builds=True,
            extra_build_env=[],
        )
        with patch.object(
            sync_upstream, "read_state", return_value=state
        ), patch.object(sync_upstream, "refresh_candidate", return_value=state):
            with self.assertRaisesRegex(RuntimeError, "1 manual review items"):
                sync_upstream.cmd_publish(args)

    def test_publish_requires_one_note_per_review_item(self):
        state = {
            "component": "sdk",
            "conflict_paths": [],
            "overlap_paths": ["one.cpp", "two.cpp"],
            "review_items": ["one.cpp", "two.cpp"],
        }
        args = argparse.Namespace(
            candidate="/tmp/not-used",
            component="sdk",
            review_note=["Approved one.cpp resolution."],
            skip_builds=True,
            extra_build_env=[],
        )
        with patch.object(
            sync_upstream, "read_state", return_value=state
        ), patch.object(sync_upstream, "refresh_candidate", return_value=state):
            with self.assertRaisesRegex(RuntimeError, "2 manual review items"):
                sync_upstream.cmd_publish(args)

    def test_cli_has_no_run_shortcut(self):
        with self.assertRaises(SystemExit):
            sync_upstream.parse_args(["run"])


class IsolatedMergeDryRunTest(unittest.TestCase):
    def test_start_prepares_candidate_without_commit_push_or_pr(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            temp = Path(temp_dir)
            base = temp / "base"
            init_repo(base)
            commit_file(base, "shared.txt", "value=0\n", "base")
            fork = temp / "fork"
            parent = temp / "parent"
            command(temp, "git", "clone", str(base), str(fork))
            command(temp, "git", "clone", str(base), str(parent))
            for repo in (fork, parent):
                command(repo, "git", "config", "user.name", "Sync Test")
                command(repo, "git", "config", "user.email", "sync@example.com")
            commit_file(fork, "fork.txt", "fork\n", "fork change")
            commit_file(parent, "parent.txt", "parent\n", "parent change")
            fork_sha = command(fork, "git", "rev-parse", "HEAD")
            parent_sha = command(parent, "git", "rev-parse", "HEAD")
            statuses = {
                "sdk": sync_upstream.DependencyStatus(
                    "sdk", parent_sha, fork_sha, False, False, ()
                ),
                "simulator": status("simulator", "up-to-date"),
            }
            args = argparse.Namespace(
                component="sdk",
                candidate_root=str(temp / "candidates"),
                behavior_overlap=[],
            )
            context = sync_upstream.Context(
                base, "main", "upstream", "origin", "develop"
            )
            with patch.object(
                sync_upstream, "build_context", return_value=context
            ), patch.object(
                sync_upstream, "inspect_dependencies", return_value=statuses
            ), patch.object(
                sync_upstream.Dependency,
                "fork_url",
                new_callable=PropertyMock,
                return_value=str(fork),
            ), patch.object(
                sync_upstream.Dependency,
                "parent_url",
                new_callable=PropertyMock,
                return_value=str(parent),
            ), patch.object(
                sync_upstream, "push_and_create_pr"
            ) as publish:
                self.assertEqual(sync_upstream.cmd_start(args), 0)
                publish.assert_not_called()

            candidate = sync_upstream.candidate_path(
                "sdk", parent_sha, str(temp / "candidates")
            )
            self.assertEqual(command(candidate, "git", "rev-parse", "HEAD"), fork_sha)
            self.assertTrue(sync_upstream.has_merge_head(candidate))
            self.assertEqual(command(fork, "git", "branch", "--show-current"), "main")

    def test_clean_same_file_overlap_stays_uncommitted(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            base = Path(temp_dir) / "base"
            init_repo(base)
            commit_file(
                base,
                "shared.txt",
                "local=0\nkeep=1\nkeep=2\nkeep=3\nkeep=4\nupstream=0\n",
                "base",
            )

            fork = Path(temp_dir) / "fork"
            upstream = Path(temp_dir) / "upstream"
            command(Path(temp_dir), "git", "clone", str(base), str(fork))
            command(Path(temp_dir), "git", "clone", str(base), str(upstream))
            for repo in (fork, upstream):
                command(repo, "git", "config", "user.name", "Sync Test")
                command(repo, "git", "config", "user.email", "sync@example.com")
            commit_file(
                fork,
                "shared.txt",
                "local=1\nkeep=1\nkeep=2\nkeep=3\nkeep=4\nupstream=0\n",
                "fork change",
            )
            commit_file(
                upstream,
                "shared.txt",
                "local=0\nkeep=1\nkeep=2\nkeep=3\nkeep=4\nupstream=1\n",
                "upstream change",
            )
            command(fork, "git", "remote", "add", "upstream", str(upstream))
            command(fork, "git", "fetch", "upstream", "main")

            head_before = command(fork, "git", "rev-parse", "HEAD")
            self.assertEqual(
                sync_upstream.overlap_paths(fork, "HEAD", "upstream/main"),
                ("shared.txt",),
            )
            self.assertEqual(
                sync_upstream.merge_without_commit(fork, "upstream/main", "test merge"),
                [],
            )
            self.assertEqual(command(fork, "git", "rev-parse", "HEAD"), head_before)
            self.assertTrue(sync_upstream.has_merge_head(fork))

    def test_git_conflict_is_left_for_manual_review(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            base = Path(temp_dir) / "base"
            init_repo(base)
            commit_file(base, "shared.txt", "value=0\n", "base")
            fork = Path(temp_dir) / "fork"
            upstream = Path(temp_dir) / "upstream"
            command(Path(temp_dir), "git", "clone", str(base), str(fork))
            command(Path(temp_dir), "git", "clone", str(base), str(upstream))
            for repo in (fork, upstream):
                command(repo, "git", "config", "user.name", "Sync Test")
                command(repo, "git", "config", "user.email", "sync@example.com")
            commit_file(fork, "shared.txt", "value=local\n", "fork change")
            commit_file(upstream, "shared.txt", "value=upstream\n", "upstream change")
            command(fork, "git", "remote", "add", "upstream", str(upstream))
            command(fork, "git", "fetch", "upstream", "main")

            self.assertEqual(
                sync_upstream.merge_without_commit(fork, "upstream/main", "test merge"),
                ["shared.txt"],
            )
            self.assertEqual(sync_upstream.unmerged_paths(fork), ["shared.txt"])


if __name__ == "__main__":
    unittest.main()
