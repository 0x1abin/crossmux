#!/usr/bin/env python3
"""Inspect, rehearse, and publish approval-gated three-repository syncs."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence, cast

DEFAULT_UPSTREAM_BRANCH = "develop"
DEFAULT_BRANCH_PREFIX = "codex/sync-upstream"
STATE_FILE = "sync-upstream-review.json"
SIMULATOR_ENVS = (
    "simulator",
    "simulator_x3",
    "simulator_eego_a4",
    "simulator_murphy_m4",
)
SDK_HOST_TESTS = (
    "libs/book/ContentProtection/test/host/run.sh",
    "libs/book/FreeInkBook/test/host/run.sh",
    "libs/hardware/InputManager/test/host/run.sh",
    "libs/ui/FreeInkUI/test/host/run.sh",
)


@dataclass(frozen=True)
class Dependency:
    name: str
    parent_repo: str
    fork_repo: str

    @property
    def parent_url(self) -> str:
        return f"https://github.com/{self.parent_repo}.git"

    @property
    def fork_url(self) -> str:
        return f"https://github.com/{self.fork_repo}.git"

    @property
    def branch_prefix(self) -> str:
        return f"codex/sync-{self.name}-main"


DEPENDENCIES = {
    "sdk": Dependency("sdk", "Free-Ink/freeink-sdk", "0x1abin/freeink-sdk"),
    "simulator": Dependency(
        "simulator",
        "crosspoint-reader/crosspoint-simulator",
        "0x1abin/crosspoint-simulator",
    ),
}


class CommandError(RuntimeError):
    def __init__(self, cmd: Sequence[str], returncode: int, output: str = "") -> None:
        self.cmd = list(cmd)
        self.returncode = returncode
        self.output = output
        super().__init__(f"{' '.join(cmd)} failed with exit code {returncode}")


@dataclass(frozen=True)
class Context:
    root: Path
    base_branch: str
    upstream_remote: str
    origin_remote: str
    upstream_branch: str

    @property
    def upstream_ref(self) -> str:
        return f"refs/remotes/{self.upstream_remote}/{self.upstream_branch}"

    @property
    def origin_base_ref(self) -> str:
        return f"refs/remotes/{self.origin_remote}/{self.base_branch}"


@dataclass(frozen=True)
class DependencyStatus:
    name: str
    parent_sha: str
    fork_sha: str
    parent_in_fork: bool
    open_pr: bool
    overlap_paths: tuple[str, ...]
    latest_parent_sha: str | None = None

    @property
    def action(self) -> str:
        return decide_dependency_action(self.parent_in_fork, self.open_pr)


def run(
    cmd: Sequence[str],
    *,
    cwd: Path,
    check: bool = True,
    capture: bool = True,
    input_text: str | None = None,
    log: bool = True,
) -> subprocess.CompletedProcess[str]:
    if log:
        print(f"$ {' '.join(cmd)}")
    try:
        completed = subprocess.run(
            list(cmd),
            cwd=str(cwd),
            text=True,
            input=input_text,
            stdout=subprocess.PIPE if capture else None,
            stderr=subprocess.STDOUT if capture else None,
        )
    except FileNotFoundError as exc:
        message = f"command not found: {cmd[0]}"
        if check:
            raise RuntimeError(message) from exc
        return subprocess.CompletedProcess(list(cmd), 127, message + "\n")
    if log and capture and completed.stdout:
        print(completed.stdout, end="" if completed.stdout.endswith("\n") else "\n")
    if check and completed.returncode != 0:
        raise CommandError(cmd, completed.returncode, completed.stdout or "")
    return completed


def git(
    root: Path,
    *args: str,
    check: bool = True,
    capture: bool = True,
    log: bool = True,
) -> subprocess.CompletedProcess[str]:
    return run(["git", *args], cwd=root, check=check, capture=capture, log=log)


def git_output(root: Path, *args: str, check: bool = True) -> str:
    return git(root, *args, check=check, log=False).stdout.strip()


def git_success(root: Path, *args: str) -> bool:
    return (
        subprocess.run(
            ["git", *args],
            cwd=str(root),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ).returncode
        == 0
    )


def repo_root() -> Path:
    return Path(
        run(["git", "rev-parse", "--show-toplevel"], cwd=Path.cwd()).stdout.strip()
    )


def sc_target_branch(root: Path) -> str | None:
    if shutil.which("sc") is None:
        return None
    completed = run(["sc", "worktree", "status", "--json"], cwd=root, check=False)
    if completed.returncode != 0 or not completed.stdout.strip():
        return None
    try:
        target = json.loads(completed.stdout).get("response", {}).get("target_branch")
    except json.JSONDecodeError:
        return None
    return target if isinstance(target, str) and target else None


def remote_head_branch(root: Path, remote: str) -> str | None:
    ref = git_output(root, "symbolic-ref", f"refs/remotes/{remote}/HEAD", check=False)
    prefix = f"refs/remotes/{remote}/"
    if ref.startswith(prefix):
        return ref[len(prefix) :]
    completed = git(root, "ls-remote", "--symref", remote, "HEAD", check=False)
    for line in completed.stdout.splitlines():
        match = re.match(r"ref: refs/heads/([^ \t]+)[ \t]+HEAD$", line)
        if match:
            return match.group(1)
    return None


def remote_branch_exists(root: Path, remote: str, branch: str) -> bool:
    return git_success(root, "show-ref", "--verify", f"refs/remotes/{remote}/{branch}")


def choose_base_branch(root: Path, origin_remote: str, requested: str) -> str:
    if requested != "auto":
        return requested
    target = sc_target_branch(root)
    if target:
        return target
    head = remote_head_branch(root, origin_remote)
    if head:
        return head
    for candidate in ("main", "master"):
        if remote_branch_exists(root, origin_remote, candidate):
            return candidate
    raise RuntimeError(
        "Could not determine base branch; pass --base-branch explicitly."
    )


def choose_upstream_branch(root: Path, upstream_remote: str, requested: str) -> str:
    if requested != "auto":
        return requested
    if remote_branch_exists(root, upstream_remote, DEFAULT_UPSTREAM_BRANCH):
        return DEFAULT_UPSTREAM_BRANCH
    head = remote_head_branch(root, upstream_remote)
    if head:
        return head
    raise RuntimeError(
        "Could not determine upstream branch; pass --upstream-branch explicitly."
    )


def build_context(args: argparse.Namespace) -> Context:
    root = repo_root()
    return Context(
        root=root,
        base_branch=choose_base_branch(root, args.origin_remote, args.base_branch),
        upstream_remote=args.upstream_remote,
        origin_remote=args.origin_remote,
        upstream_branch=choose_upstream_branch(
            root, args.upstream_remote, args.upstream_branch
        ),
    )


def fetch_branch(root: Path, remote: str, branch: str) -> None:
    git(root, "fetch", remote, f"+refs/heads/{branch}:refs/remotes/{remote}/{branch}")


def fetch_url_ref(root: Path, url: str, branch: str, ref: str) -> None:
    git(root, "fetch", url, f"+refs/heads/{branch}:{ref}")


def full_sha(root: Path, ref: str) -> str:
    return git_output(root, "rev-parse", ref)


def short_sha(root: Path, ref: str) -> str:
    return git_output(root, "rev-parse", "--short=8", ref)


def is_ancestor(root: Path, ancestor: str, descendant: str) -> bool:
    return git_success(root, "merge-base", "--is-ancestor", ancestor, descendant)


def select_upstream_sha(
    root: Path, latest_ref: str, pinned_sha: str | None, component: str
) -> tuple[str, str]:
    latest_sha = full_sha(root, latest_ref)
    if pinned_sha is None:
        return latest_sha, latest_sha
    if not re.fullmatch(r"[0-9a-f]{40}", pinned_sha) or not is_ancestor(
        root, pinned_sha, latest_ref
    ):
        raise RuntimeError(
            f"Pinned {component} SHA {pinned_sha} is not an ancestor of its upstream branch."
        )
    return pinned_sha, latest_sha


def changed_paths(root: Path, base: str, tip: str) -> set[str]:
    output = git_output(root, "diff", "--name-only", f"{base}..{tip}")
    return {line for line in output.splitlines() if line}


def overlap_paths(root: Path, left: str, right: str) -> tuple[str, ...]:
    base = git_output(root, "merge-base", left, right, check=False)
    if not base:
        return ()
    return tuple(
        sorted(changed_paths(root, base, left) & changed_paths(root, base, right))
    )


def unmerged_paths(root: Path) -> list[str]:
    output = git_output(root, "diff", "--name-only", "--diff-filter=U", check=False)
    return [line for line in output.splitlines() if line]


def staged_paths(root: Path) -> list[str]:
    return [
        line
        for line in git_output(root, "diff", "--cached", "--name-only").splitlines()
        if line
    ]


def unstaged_tracked_paths(root: Path) -> list[str]:
    return [
        line for line in git_output(root, "diff", "--name-only").splitlines() if line
    ]


def has_merge_head(root: Path) -> bool:
    return git_success(root, "rev-parse", "-q", "--verify", "MERGE_HEAD")


def decide_dependency_action(parent_in_fork: bool, open_pr: bool) -> str:
    if parent_in_fork:
        return "up-to-date"
    return "wait-for-pr" if open_pr else "sync-required"


def next_component(statuses: dict[str, DependencyStatus]) -> str:
    for name in ("sdk", "simulator"):
        if statuses[name].action != "up-to-date":
            return name
    return "crossmux"


def sync_pr_open(root: Path, dependency: Dependency, parent_sha: str) -> bool:
    expected = f"{dependency.branch_prefix}-{parent_sha[:8]}"
    completed = run(
        [
            "gh",
            "pr",
            "list",
            "--repo",
            dependency.fork_repo,
            "--state",
            "open",
            "--json",
            "headRefName",
            "--limit",
            "100",
        ],
        cwd=root,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(f"Could not inspect open PRs for {dependency.fork_repo}.")
    try:
        return any(
            item.get("headRefName") == expected
            for item in json.loads(completed.stdout or "[]")
        )
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"Invalid PR response for {dependency.fork_repo}.") from exc


def inspect_dependency(
    root: Path,
    dependency: Dependency,
    *,
    check_pr: bool = True,
    upstream_pin: str | None = None,
) -> DependencyStatus:
    parent_ref = f"refs/sync-upstream/{dependency.name}/parent-main"
    fork_ref = f"refs/sync-upstream/{dependency.name}/fork-main"
    fetch_url_ref(root, dependency.parent_url, "main", parent_ref)
    fetch_url_ref(root, dependency.fork_url, "main", fork_ref)
    parent_sha, latest_parent_sha = select_upstream_sha(
        root, parent_ref, upstream_pin, dependency.name
    )
    return DependencyStatus(
        name=dependency.name,
        parent_sha=parent_sha,
        fork_sha=full_sha(root, fork_ref),
        parent_in_fork=is_ancestor(root, parent_sha, fork_ref),
        open_pr=sync_pr_open(root, dependency, parent_sha) if check_pr else False,
        overlap_paths=overlap_paths(root, fork_ref, parent_sha),
        latest_parent_sha=latest_parent_sha,
    )


def inspect_dependencies(
    root: Path,
    *,
    check_pr: bool = True,
    upstream_pins: dict[str, str] | None = None,
) -> dict[str, DependencyStatus]:
    pins = upstream_pins or {}
    return {
        name: inspect_dependency(
            root, dependency, check_pr=check_pr, upstream_pin=pins.get(name)
        )
        for name, dependency in DEPENDENCIES.items()
    }


def candidate_path(
    component: str, upstream_sha: str, override: str | None = None
) -> Path:
    base = (
        Path(override)
        if override
        else Path(tempfile.gettempdir()) / "crossmux-sync-upstream"
    )
    return base / f"{component}-{upstream_sha[:8]}"


def state_path(candidate: Path) -> Path:
    git_dir = Path(git_output(candidate, "rev-parse", "--git-dir"))
    if not git_dir.is_absolute():
        git_dir = candidate / git_dir
    return git_dir / STATE_FILE


def write_state(candidate: Path, state: dict[str, object]) -> None:
    state_path(candidate).write_text(
        json.dumps(state, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def read_state(candidate: Path) -> dict[str, object]:
    path = state_path(candidate)
    if not path.exists():
        raise RuntimeError(f"Candidate has no review state: {candidate}")
    return json.loads(path.read_text(encoding="utf-8"))


def candidate_id(component: str, base_sha: str, upstream_sha: str) -> str:
    return hashlib.sha256(
        f"{component}:{base_sha}:{upstream_sha}".encode()
    ).hexdigest()[:16]


def branch_name(component: str, upstream_sha: str) -> str:
    prefix = (
        DEFAULT_BRANCH_PREFIX
        if component == "crossmux"
        else DEPENDENCIES[component].branch_prefix
    )
    return f"{prefix}-{upstream_sha[:8]}"


def merge_without_commit(candidate: Path, upstream_ref: str, title: str) -> list[str]:
    completed = git(
        candidate,
        "merge",
        "--no-ff",
        "--no-commit",
        "-m",
        title,
        upstream_ref,
        check=False,
    )
    conflicts = unmerged_paths(candidate)
    if completed.returncode != 0 and not conflicts:
        raise CommandError(["git", "merge"], completed.returncode, completed.stdout)
    return conflicts


def clone_dependency_candidate(
    root: Path,
    dependency: Dependency,
    status: DependencyStatus,
    candidate: Path,
    behavior_overlaps: Sequence[str],
) -> dict[str, object]:
    candidate.parent.mkdir(parents=True, exist_ok=True)
    run(["git", "clone", dependency.fork_url, str(candidate)], cwd=root)
    git(candidate, "remote", "add", "upstream", dependency.parent_url)
    git(candidate, "fetch", "upstream", "main")
    branch = branch_name(dependency.name, status.parent_sha)
    git(candidate, "switch", "-c", branch, "origin/main")
    overlaps = sorted(
        set(overlap_paths(candidate, "origin/main", status.parent_sha))
        | set(behavior_overlaps)
    )
    conflicts = merge_without_commit(
        candidate,
        status.parent_sha,
        f"chore: sync {dependency.name} upstream main ({status.parent_sha[:8]})",
    )
    state: dict[str, object] = {
        "base_branch": "main",
        "base_sha": status.fork_sha,
        "branch": branch,
        "candidate_id": candidate_id(
            dependency.name, status.fork_sha, status.parent_sha
        ),
        "component": dependency.name,
        "conflict_paths": conflicts,
        "overlap_paths": overlaps,
        "review_items": sorted(set(conflicts) | set(overlaps)),
        "upstream_sha": status.parent_sha,
    }
    write_state(candidate, state)
    return state


def clone_crossmux_candidate(
    ctx: Context,
    statuses: dict[str, DependencyStatus],
    candidate: Path,
    behavior_overlaps: Sequence[str],
    upstream_sha: str,
) -> dict[str, object]:
    candidate.parent.mkdir(parents=True, exist_ok=True)
    run(["git", "clone", str(ctx.root), str(candidate)], cwd=ctx.root)
    origin_url = git_output(ctx.root, "remote", "get-url", ctx.origin_remote)
    upstream_url = git_output(ctx.root, "remote", "get-url", ctx.upstream_remote)
    git(candidate, "remote", "set-url", "origin", origin_url)
    git(candidate, "remote", "add", "upstream", upstream_url)
    fetch_branch(candidate, "origin", ctx.base_branch)
    fetch_branch(candidate, "upstream", ctx.upstream_branch)
    base_ref = f"refs/remotes/origin/{ctx.base_branch}"
    base_sha = full_sha(candidate, base_ref)
    branch = branch_name("crossmux", upstream_sha)
    git(candidate, "switch", "-C", branch, base_ref)
    overlaps = sorted(
        set(overlap_paths(candidate, base_ref, upstream_sha)) | set(behavior_overlaps)
    )
    conflicts = merge_without_commit(
        candidate,
        upstream_sha,
        f"chore: sync upstream {ctx.upstream_branch} into {ctx.base_branch} ({upstream_sha[:8]})",
    )
    if not conflicts:
        update_crossmux_dependency_pins(
            candidate, statuses["sdk"].fork_sha, statuses["simulator"].fork_sha
        )
    state: dict[str, object] = {
        "base_branch": ctx.base_branch,
        "base_sha": base_sha,
        "branch": branch,
        "candidate_id": candidate_id("crossmux", base_sha, upstream_sha),
        "component": "crossmux",
        "conflict_paths": conflicts,
        "overlap_paths": overlaps,
        "review_items": sorted(set(conflicts) | set(overlaps)),
        "sdk_sha": statuses["sdk"].fork_sha,
        "simulator_sha": statuses["simulator"].fork_sha,
        "upstream_sha": upstream_sha,
    }
    write_state(candidate, state)
    return state


def replace_simulator_pins(platformio: str, cmake: str, sha: str) -> tuple[str, str]:
    platform_pattern = r"(simulator=https://github\.com/0x1abin/crosspoint-simulator\.git#)[0-9a-f]{40}"
    cmake_pattern = r"(GIT_TAG )[0-9a-f]{40}"
    platformio, platform_count = re.subn(
        platform_pattern, rf"\g<1>{sha}", platformio, count=1
    )
    cmake, cmake_count = re.subn(cmake_pattern, rf"\g<1>{sha}", cmake, count=1)
    if platform_count != 1 or cmake_count != 1:
        raise RuntimeError("Could not update both CrossPoint Simulator pins.")
    return platformio, cmake


def update_crossmux_dependency_pins(
    root: Path, sdk_sha: str, simulator_sha: str
) -> None:
    git(
        root,
        "config",
        "-f",
        ".gitmodules",
        "submodule.freeink-sdk.url",
        DEPENDENCIES["sdk"].fork_url,
    )
    git(root, "config", "-f", ".gitmodules", "submodule.freeink-sdk.branch", "main")
    git(root, "submodule", "sync", "--", "freeink-sdk")
    git(root, "submodule", "update", "--init", "--", "freeink-sdk")
    sdk_root = root / "freeink-sdk"
    git(sdk_root, "fetch", DEPENDENCIES["sdk"].fork_url, sdk_sha)
    git(sdk_root, "checkout", "--detach", sdk_sha)
    platform_path = root / "platformio.ini"
    cmake_path = root / "test/CMakeLists.txt"
    platformio, cmake = replace_simulator_pins(
        platform_path.read_text(encoding="utf-8"),
        cmake_path.read_text(encoding="utf-8"),
        simulator_sha,
    )
    platform_path.write_text(platformio, encoding="utf-8")
    cmake_path.write_text(cmake, encoding="utf-8")
    git(
        root,
        "add",
        ".gitmodules",
        "freeink-sdk",
        "platformio.ini",
        "test/CMakeLists.txt",
    )


def summarize_state(candidate: Path, state: dict[str, object]) -> None:
    summary = dict(state)
    summary["candidate"] = str(candidate)
    summary["requires_manual_review"] = bool(state["review_items"])
    print(json.dumps(summary, indent=2, sort_keys=True))


def refresh_candidate(candidate: Path, state: dict[str, object]) -> dict[str, object]:
    conflicts = unmerged_paths(candidate)
    prior_items = cast(
        list[str],
        state.get(
            "review_items",
            list(cast(list[str], state["conflict_paths"]))
            + list(cast(list[str], state["overlap_paths"])),
        ),
    )
    state["conflict_paths"] = conflicts
    state["review_items"] = sorted(set(prior_items) | set(conflicts))
    write_state(candidate, state)
    return state


def gitlink_sha(root: Path, treeish: str) -> str:
    entry = git_output(root, "ls-tree", treeish, "freeink-sdk")
    match = re.match(r"160000 commit ([0-9a-f]{40})\tfreeink-sdk$", entry)
    if not match:
        raise RuntimeError(f"{treeish} does not contain the freeink-sdk gitlink.")
    return match.group(1)


def indexed_gitlink_sha(root: Path) -> str:
    entry = git_output(root, "ls-files", "-s", "--", "freeink-sdk")
    match = re.match(r"160000 ([0-9a-f]{40}) 0\tfreeink-sdk$", entry)
    if not match:
        raise RuntimeError("The index does not contain the freeink-sdk gitlink.")
    return match.group(1)


def simulator_pins(text: str) -> tuple[str, ...]:
    platform = re.search(r"crosspoint-simulator\.git#([0-9a-f]{40})", text)
    cmake = re.search(
        r"GIT_REPOSITORY\s+https://github\.com/0x1abin/crosspoint-simulator\.git\s+"
        r"GIT_TAG\s+([0-9a-f]{40})",
        text,
    )
    return tuple(match.group(1) for match in (platform, cmake) if match)


def inspect_crossmux(
    ctx: Context,
    statuses: dict[str, DependencyStatus],
    upstream_pin: str | None = None,
) -> dict[str, object]:
    fetch_branch(ctx.root, ctx.origin_remote, ctx.base_branch)
    fetch_branch(ctx.root, ctx.upstream_remote, ctx.upstream_branch)
    base_sha = full_sha(ctx.root, ctx.origin_base_ref)
    upstream_sha, latest_upstream_sha = select_upstream_sha(
        ctx.root, ctx.upstream_ref, upstream_pin, "crossmux"
    )
    platformio = git_output(ctx.root, "show", f"{ctx.origin_base_ref}:platformio.ini")
    cmake = git_output(ctx.root, "show", f"{ctx.origin_base_ref}:test/CMakeLists.txt")
    pins = simulator_pins(platformio + "\n" + cmake)
    dependencies_ready = all(
        status.action == "up-to-date" for status in statuses.values()
    )
    fully_synced = (
        dependencies_ready
        and is_ancestor(ctx.root, upstream_sha, ctx.origin_base_ref)
        and gitlink_sha(ctx.root, ctx.origin_base_ref) == statuses["sdk"].fork_sha
        and len(pins) == 2
        and all(pin == statuses["simulator"].fork_sha for pin in pins)
    )
    return {
        "action": (
            "up-to-date"
            if fully_synced
            else ("ready" if dependencies_ready else "blocked-by-dependencies")
        ),
        "base_sha": base_sha,
        "latest_upstream_sha": latest_upstream_sha,
        "overlap_paths": overlap_paths(ctx.root, ctx.origin_base_ref, upstream_sha),
        "simulator_pins": pins,
        "upstream_sha": upstream_sha,
    }


def check_conflict_markers(root: Path) -> None:
    completed = git(
        root, "grep", "-n", "-E", f"{'<' * 7}|{'>' * 7}", "--", ".", check=False
    )
    if completed.returncode == 0:
        raise RuntimeError("Conflict markers found in tracked files.")
    if completed.returncode not in (0, 1):
        raise CommandError(["git", "grep"], completed.returncode, completed.stdout)


def validate_index(root: Path) -> None:
    conflicts = unmerged_paths(root)
    if conflicts:
        raise RuntimeError("Unresolved conflicts remain:\n" + "\n".join(conflicts))
    unstaged = unstaged_tracked_paths(root)
    if unstaged:
        raise RuntimeError(
            "Tracked files have unstaged changes; stage them before publishing:\n"
            + "\n".join(unstaged)
        )
    git(root, "diff", "--check")
    git(root, "diff", "--cached", "--check")
    check_conflict_markers(root)


def run_crossmux_builds(
    root: Path, skip_builds: bool, extra_envs: Sequence[str]
) -> None:
    if skip_builds:
        print("Skipping PlatformIO builds because --skip-builds was passed.")
        return
    run(["pio", "run"], cwd=root, capture=False)
    run(["pio", "run", "-e", "gh_release"], cwd=root, capture=False)
    for env in extra_envs:
        run(["pio", "run", "-e", env], cwd=root, capture=False)


def validate_sdk_candidate(
    candidate: Path, crossmux_root: Path, skip_builds: bool, extra_envs: Sequence[str]
) -> None:
    for script in SDK_HOST_TESTS:
        run(["bash", script], cwd=candidate, capture=False)
    if skip_builds:
        print(
            "Skipping CrossMux SDK candidate builds because --skip-builds was passed."
        )
        return
    with tempfile.TemporaryDirectory(prefix="crossmux-sdk-check-") as temp_dir:
        checkout = Path(temp_dir) / "crossmux"
        run(["git", "clone", str(crossmux_root), str(checkout)], cwd=crossmux_root)
        if (checkout / "freeink-sdk").exists():
            shutil.rmtree(checkout / "freeink-sdk")
        os.symlink(candidate, checkout / "freeink-sdk", target_is_directory=True)
        run_crossmux_builds(checkout, False, extra_envs)


def replace_simulator_with_local_candidate(platformio: str, candidate: Path) -> str:
    pattern = (
        r"simulator=https://github\.com/0x1abin/crosspoint-simulator\.git#[0-9a-f]{40}"
    )
    replaced, count = re.subn(
        pattern, f"simulator=symlink://{candidate}", platformio, count=1
    )
    if count != 1:
        raise RuntimeError("Could not point PlatformIO at the simulator candidate.")
    return replaced


def replace_cmake_simulator_with_local_candidate(cmake: str, candidate: Path) -> str:
    pattern = (
        r"FetchContent_Declare\(\s*crosspoint_simulator\s*"
        r"GIT_REPOSITORY\s+https://github\.com/0x1abin/crosspoint-simulator\.git\s*"
        r"GIT_TAG\s+[0-9a-f]{40}\s*\)"
    )
    replacement = (
        f"FetchContent_Declare(\n  crosspoint_simulator\n  SOURCE_DIR {candidate}\n)"
    )
    replaced, count = re.subn(pattern, replacement, cmake, count=1)
    if count != 1:
        raise RuntimeError("Could not point CMake at the simulator candidate.")
    return replaced


def validate_simulator_candidate(
    candidate: Path, crossmux_root: Path, skip_builds: bool
) -> None:
    run(["bash", "tests/run_host_compat_self_test.sh"], cwd=candidate, capture=False)
    if skip_builds:
        print(
            "Skipping CrossMux simulator candidate builds because --skip-builds was passed."
        )
        return
    with tempfile.TemporaryDirectory(prefix="crossmux-simulator-check-") as temp_dir:
        checkout = Path(temp_dir) / "crossmux"
        run(
            ["git", "clone", "--recurse-submodules", str(crossmux_root), str(checkout)],
            cwd=crossmux_root,
        )
        platform_path = checkout / "platformio.ini"
        cmake_path = checkout / "test/CMakeLists.txt"
        platform_path.write_text(
            replace_simulator_with_local_candidate(
                platform_path.read_text(encoding="utf-8"), candidate
            ),
            encoding="utf-8",
        )
        cmake_path.write_text(
            replace_cmake_simulator_with_local_candidate(
                cmake_path.read_text(encoding="utf-8"), candidate
            ),
            encoding="utf-8",
        )
        command = ["pio", "run"]
        for env in SIMULATOR_ENVS:
            command.extend(("-e", env))
        run(command, cwd=checkout, capture=False)
        run(["cmake", "-S", "test", "-B", "build/test"], cwd=checkout, capture=False)
        run(["cmake", "--build", "build/test"], cwd=checkout, capture=False)
        run(
            ["ctest", "--test-dir", "build/test", "--output-on-failure"],
            cwd=checkout,
            capture=False,
        )


def commit_candidate(candidate: Path, state: dict[str, object]) -> None:
    if not staged_paths(candidate) and not has_merge_head(candidate):
        if full_sha(candidate, "HEAD") == state["base_sha"]:
            raise RuntimeError("Candidate has no staged merge to commit.")
        print("Candidate commit already exists; continuing publish retry.")
        return
    component = str(state["component"])
    upstream_sha = str(state["upstream_sha"])
    if component == "crossmux":
        title = f"chore: sync upstream into {state['base_branch']} ({upstream_sha[:8]})"
    else:
        title = f"chore: sync {component} upstream main ({upstream_sha[:8]})"
    git(candidate, "commit", "-m", title, "-m", f"Upstream: {upstream_sha}")


def pr_body(
    state: dict[str, object], review_notes: Sequence[str], skipped_builds: bool
) -> str:
    lines = [
        "## Summary",
        "",
        f"- Component: `{state['component']}`.",
        f"- Upstream revision: `{state['upstream_sha']}`.",
        f"- Candidate review ID: `{state['candidate_id']}`.",
        "",
        "## Manual conflict and overlap review",
        "",
    ]
    review_items = cast(list[str], state["review_items"])
    if review_items:
        lines.extend(
            f"- `{item}`: {note}" for item, note in zip(review_items, review_notes)
        )
    else:
        lines.append("- No Git conflicts or behavioral overlap candidates were found.")
    lines.extend(
        [
            "",
            "## Validation",
            "",
            "- `git diff --check`",
            "- `git diff --cached --check`",
        ]
    )
    lines.append(
        "- Builds skipped with `--skip-builds`."
        if skipped_builds
        else "- Component validation completed locally."
    )
    return "\n".join(lines) + "\n"


def push_and_create_pr(
    candidate: Path,
    state: dict[str, object],
    review_notes: Sequence[str],
    skipped_builds: bool,
) -> None:
    component = str(state["component"])
    repo = (
        "0x1abin/crossmux"
        if component == "crossmux"
        else DEPENDENCIES[component].fork_repo
    )
    branch = str(state["branch"])
    git(candidate, "push", "-u", "origin", branch)
    existing = run(
        ["gh", "pr", "view", branch, "--repo", repo, "--json", "url", "-q", ".url"],
        cwd=candidate,
        check=False,
    )
    if existing.returncode == 0 and existing.stdout.strip():
        print(f"PR already exists: {existing.stdout.strip()}")
        return
    title = (
        f"chore: sync upstream into {state['base_branch']} ({str(state['upstream_sha'])[:8]})"
        if component == "crossmux"
        else f"chore: sync {component} upstream main ({str(state['upstream_sha'])[:8]})"
    )
    body = pr_body(state, review_notes, skipped_builds)
    with tempfile.NamedTemporaryFile("w", delete=False, encoding="utf-8") as handle:
        handle.write(body)
        body_path = handle.name
    try:
        run(
            [
                "gh",
                "pr",
                "create",
                "--repo",
                repo,
                "--base",
                str(state["base_branch"]),
                "--head",
                branch,
                "--title",
                title,
                "--body-file",
                body_path,
                "--draft",
            ],
            cwd=candidate,
        )
    finally:
        os.unlink(body_path)


def parse_upstream_pin(value: str) -> tuple[str, str]:
    match = re.fullmatch(r"(sdk|simulator|crossmux)=([0-9a-f]{40})", value)
    if not match:
        raise argparse.ArgumentTypeError(
            "upstream pin must be COMPONENT=40_HEX_SHA"
        )
    return match.group(1), match.group(2)


def upstream_pins(args: argparse.Namespace) -> dict[str, str]:
    pins: dict[str, str] = {}
    for component, sha in getattr(args, "upstream_pin", []):
        if component in pins:
            raise RuntimeError(f"Duplicate upstream pin for {component}.")
        pins[component] = sha
    return pins


def cmd_inspect(args: argparse.Namespace) -> int:
    ctx = build_context(args)
    pins = upstream_pins(args)
    statuses = inspect_dependencies(ctx.root, upstream_pins=pins)
    crossmux = inspect_crossmux(ctx, statuses, pins.get("crossmux"))
    summary = {
        "crossmux": crossmux,
        "dependencies": {
            name: {
                "action": status.action,
                "fork_sha": status.fork_sha,
                "latest_parent_sha": status.latest_parent_sha,
                "open_pr": status.open_pr,
                "overlap_paths": status.overlap_paths,
                "parent_sha": status.parent_sha,
            }
            for name, status in statuses.items()
        },
        "next_component": next_component(statuses),
    }
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


def cmd_start(args: argparse.Namespace) -> int:
    ctx = build_context(args)
    pins = upstream_pins(args)
    statuses = inspect_dependencies(ctx.root, upstream_pins=pins)
    expected = next_component(statuses)
    if args.component != expected:
        raise RuntimeError(
            f"Next component is {expected!r}; refusing to start {args.component!r}."
        )
    if args.component == "crossmux":
        crossmux_status = inspect_crossmux(ctx, statuses, pins.get("crossmux"))
        if crossmux_status["action"] == "up-to-date":
            raise RuntimeError("crossmux is already up to date.")
        upstream_sha = str(crossmux_status["upstream_sha"])
    else:
        status = statuses[args.component]
        if status.action != "sync-required":
            raise RuntimeError(
                f"{args.component} action is {status.action}; no candidate can be started."
            )
        upstream_sha = status.parent_sha
    candidate = candidate_path(args.component, upstream_sha, args.candidate_root)
    if candidate.exists():
        state = read_state(candidate)
        if cast(dict[str, str], state.get("upstream_pins", {})) != pins:
            raise RuntimeError("Candidate upstream pins do not match this command.")
        state = refresh_candidate(candidate, state)
        recorded_overlaps = cast(list[str], state["overlap_paths"])
        state["overlap_paths"] = sorted(
            set(recorded_overlaps) | set(args.behavior_overlap)
        )
        recorded_items = cast(list[str], state["review_items"])
        state["review_items"] = sorted(set(recorded_items) | set(args.behavior_overlap))
        if args.component == "crossmux" and not state["conflict_paths"]:
            update_crossmux_dependency_pins(
                candidate, statuses["sdk"].fork_sha, statuses["simulator"].fork_sha
            )
            state["sdk_sha"] = statuses["sdk"].fork_sha
            state["simulator_sha"] = statuses["simulator"].fork_sha
        write_state(candidate, state)
        summarize_state(candidate, state)
        return 0
    if args.component == "crossmux":
        state = clone_crossmux_candidate(
            ctx, statuses, candidate, args.behavior_overlap, upstream_sha
        )
    else:
        state = clone_dependency_candidate(
            ctx.root,
            DEPENDENCIES[args.component],
            statuses[args.component],
            candidate,
            args.behavior_overlap,
        )
    state["upstream_pins"] = pins
    write_state(candidate, state)
    summarize_state(candidate, state)
    return 0


def cmd_publish(args: argparse.Namespace) -> int:
    candidate = Path(args.candidate).resolve()
    state = read_state(candidate)
    pins = upstream_pins(args)
    if cast(dict[str, str], state.get("upstream_pins", {})) != pins:
        raise RuntimeError("Candidate upstream pins do not match this command.")
    state = refresh_candidate(candidate, state)
    if state["component"] != args.component:
        raise RuntimeError(
            f"Candidate is for {state['component']!r}, not {args.component!r}."
        )
    conflicts = cast(list[str], state["conflict_paths"])
    review_items = cast(list[str], state["review_items"])
    if conflicts:
        raise RuntimeError("Unresolved conflicts remain:\n" + "\n".join(conflicts))
    if len(args.review_note) != len(review_items):
        raise RuntimeError(
            f"This candidate has {len(review_items)} manual review items but received "
            f"{len(args.review_note)} approved --review-note values."
        )
    validate_index(candidate)
    root = repo_root()
    if args.component == "sdk":
        current = inspect_dependency(
            root, DEPENDENCIES["sdk"], upstream_pin=pins.get("sdk")
        )
        if (
            current.parent_sha != state["upstream_sha"]
            or current.fork_sha != state["base_sha"]
        ):
            raise RuntimeError(
                "SDK parent or fork moved after start; inspect and start a fresh candidate."
            )
        validate_sdk_candidate(candidate, root, args.skip_builds, args.extra_build_env)
    elif args.component == "simulator":
        current = inspect_dependency(
            root, DEPENDENCIES["simulator"], upstream_pin=pins.get("simulator")
        )
        if (
            current.parent_sha != state["upstream_sha"]
            or current.fork_sha != state["base_sha"]
        ):
            raise RuntimeError(
                "Simulator parent or fork moved after start; inspect and start a fresh candidate."
            )
        validate_simulator_candidate(candidate, root, args.skip_builds)
    else:
        ctx = build_context(args)
        statuses = inspect_dependencies(root, upstream_pins=pins)
        current_crossmux = inspect_crossmux(ctx, statuses, pins.get("crossmux"))
        if (
            current_crossmux["base_sha"] != state["base_sha"]
            or current_crossmux["upstream_sha"] != state["upstream_sha"]
        ):
            raise RuntimeError(
                "CrossMux base or upstream moved after start; inspect and start a fresh candidate."
            )
        if any(status.action != "up-to-date" for status in statuses.values()):
            raise RuntimeError(
                "A dependency fork is no longer up to date; publish it before CrossMux."
            )
        if (
            statuses["sdk"].fork_sha != state["sdk_sha"]
            or statuses["simulator"].fork_sha != state["simulator_sha"]
        ):
            raise RuntimeError(
                "A dependency fork moved after start; rerun CrossMux start to refresh both pins."
            )
        expected_sdk = str(state["sdk_sha"])
        expected_simulator = str(state["simulator_sha"])
        if indexed_gitlink_sha(candidate) != expected_sdk:
            raise RuntimeError(
                "CrossMux candidate does not pin the reviewed SDK revision; rerun start."
            )
        pins = simulator_pins(
            (candidate / "platformio.ini").read_text(encoding="utf-8")
            + "\n"
            + (candidate / "test/CMakeLists.txt").read_text(encoding="utf-8")
        )
        if len(pins) != 2 or any(pin != expected_simulator for pin in pins):
            raise RuntimeError(
                "CrossMux candidate does not pin one reviewed simulator revision; rerun start."
            )
        run_crossmux_builds(candidate, args.skip_builds, args.extra_build_env)
    commit_candidate(candidate, state)
    push_and_create_pr(candidate, state, args.review_note, args.skip_builds)
    return 0


def add_common_options(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--origin-remote", default="origin")
    parser.add_argument("--upstream-remote", default="upstream")
    parser.add_argument("--base-branch", default="auto")
    parser.add_argument("--upstream-branch", default=DEFAULT_UPSTREAM_BRANCH)
    parser.add_argument(
        "--upstream-pin",
        action="append",
        default=[],
        type=parse_upstream_pin,
        metavar="COMPONENT=SHA",
        help="freeze sdk, simulator, or crossmux at an upstream commit; repeatable",
    )


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    inspect_parser = subparsers.add_parser(
        "inspect", help="inspect all three components"
    )
    add_common_options(inspect_parser)
    inspect_parser.set_defaults(func=cmd_inspect)

    start_parser = subparsers.add_parser(
        "start", help="prepare one isolated candidate without publishing"
    )
    add_common_options(start_parser)
    start_parser.add_argument(
        "--component", choices=("sdk", "simulator", "crossmux"), required=True
    )
    start_parser.add_argument(
        "--candidate-root", help="override the temporary candidate parent directory"
    )
    start_parser.add_argument(
        "--behavior-overlap",
        action="append",
        default=[],
        help="record a manually discovered cross-file or cross-symbol overlap; repeatable",
    )
    start_parser.set_defaults(func=cmd_start)

    publish_parser = subparsers.add_parser(
        "publish", help="validate and publish one approved candidate"
    )
    add_common_options(publish_parser)
    publish_parser.add_argument(
        "--component", choices=("sdk", "simulator", "crossmux"), required=True
    )
    publish_parser.add_argument(
        "--candidate", required=True, help="candidate path printed by start"
    )
    publish_parser.add_argument(
        "--draft", action="store_true", default=True, help=argparse.SUPPRESS
    )
    publish_parser.add_argument("--skip-builds", action="store_true")
    publish_parser.add_argument(
        "--extra-build-env", action="append", default=[], metavar="ENV"
    )
    publish_parser.add_argument(
        "--review-note",
        action="append",
        default=[],
        help="record one user-approved conflict or overlap resolution; repeatable",
    )
    publish_parser.set_defaults(func=cmd_publish)
    return parser.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)
    try:
        return args.func(args)
    except (CommandError, RuntimeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
