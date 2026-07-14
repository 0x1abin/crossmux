#!/usr/bin/env python3
"""Automate the CrossMux upstream sync branch and PR flow."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence


DEFAULT_UPSTREAM_BRANCH = "develop"
DEFAULT_BRANCH_PREFIX = "agent/sync-upstream"


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


def run(
    cmd: Sequence[str],
    *,
    cwd: Path,
    check: bool = True,
    capture: bool = True,
    input_text: str | None = None,
) -> subprocess.CompletedProcess[str]:
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
        completed = subprocess.CompletedProcess(list(cmd), 127, message + "\n")
    if capture and completed.stdout:
        print(completed.stdout, end="" if completed.stdout.endswith("\n") else "\n")
    if check and completed.returncode != 0:
        raise CommandError(cmd, completed.returncode, completed.stdout or "")
    return completed


def git(root: Path, *args: str, check: bool = True, capture: bool = True) -> subprocess.CompletedProcess[str]:
    return run(["git", *args], cwd=root, check=check, capture=capture)


def repo_root() -> Path:
    completed = run(["git", "rev-parse", "--show-toplevel"], cwd=Path.cwd())
    return Path(completed.stdout.strip())


def git_output(root: Path, *args: str, check: bool = True) -> str:
    completed = git(root, *args, check=check)
    return completed.stdout.strip()


def git_success(root: Path, *args: str) -> bool:
    completed = subprocess.run(
        ["git", *args],
        cwd=str(root),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return completed.returncode == 0


def sc_target_branch(root: Path) -> str | None:
    if shutil.which("sc") is None:
        return None
    completed = run(["sc", "worktree", "status", "--json"], cwd=root, check=False)
    if completed.returncode != 0 or not completed.stdout.strip():
        return None
    try:
        payload = json.loads(completed.stdout)
    except json.JSONDecodeError:
        return None
    response = payload.get("response", {})
    target = response.get("target_branch")
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


def script_relative_path(root: Path) -> str:
    return Path(__file__).resolve().relative_to(root).as_posix()


def base_contains_script(root: Path, base_ref: str) -> bool:
    return git_success(root, "cat-file", "-e", f"{base_ref}:{script_relative_path(root)}")


def require_base_contains_script(root: Path, base_ref: str) -> None:
    if base_contains_script(root, base_ref):
        return
    rel_path = script_relative_path(root)
    raise RuntimeError(
        f"{base_ref} does not contain {rel_path}. "
        "The detected base is probably wrong for this repo, or the skill has "
        "not been merged into that base yet. Pass --base-branch main if the "
        "CrossMux integration branch is intended."
    )


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
    raise RuntimeError("Could not determine base branch; pass --base-branch explicitly.")


def choose_upstream_branch(root: Path, upstream_remote: str, requested: str) -> str:
    if requested != "auto":
        return requested
    if remote_branch_exists(root, upstream_remote, DEFAULT_UPSTREAM_BRANCH):
        return DEFAULT_UPSTREAM_BRANCH
    head = remote_head_branch(root, upstream_remote)
    if head:
        return head
    raise RuntimeError("Could not determine upstream branch; pass --upstream-branch explicitly.")


def fetch_branch(root: Path, remote: str, branch: str) -> None:
    git(root, "fetch", remote, f"+refs/heads/{branch}:refs/remotes/{remote}/{branch}")


def require_clean_worktree(root: Path) -> None:
    status = git_output(root, "status", "--porcelain=v1", "-uall")
    if status:
        raise RuntimeError(
            "Working tree is not clean. Commit, stash, or remove unrelated files before starting.\n"
            + status
        )


def current_branch(root: Path) -> str:
    branch = git_output(root, "branch", "--show-current")
    if not branch:
        raise RuntimeError("Detached HEAD is not supported for this sync flow.")
    return branch


def require_not_base_branch(root: Path, base_branch: str) -> None:
    branch = current_branch(root)
    if branch == base_branch:
        raise RuntimeError(f"Refusing to publish from base branch {base_branch!r}.")


def short_sha(root: Path, ref: str) -> str:
    return git_output(root, "rev-parse", "--short=8", ref)


def full_sha(root: Path, ref: str) -> str:
    return git_output(root, "rev-parse", ref)


def sanitize_branch_component(value: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9._-]+", "-", value.strip("/"))
    return cleaned.strip("-") or "upstream"


def branch_exists(root: Path, name: str, origin_remote: str) -> bool:
    local_exists = git_success(root, "show-ref", "--verify", f"refs/heads/{name}")
    remote_exists = git_success(root, "show-ref", "--verify", f"refs/remotes/{origin_remote}/{name}")
    return local_exists or remote_exists


def unique_branch_name(root: Path, prefix: str, upstream_branch: str, sha: str, origin_remote: str) -> str:
    base = f"{prefix}-{sanitize_branch_component(upstream_branch)}-{sha}"
    candidate = base
    counter = 2
    while branch_exists(root, candidate, origin_remote):
        candidate = f"{base}-{counter}"
        counter += 1
    return candidate


def is_ancestor(root: Path, ancestor: str, descendant: str) -> bool:
    return git_success(root, "merge-base", "--is-ancestor", ancestor, descendant)


def has_merge_head(root: Path) -> bool:
    return (root / ".git" / "MERGE_HEAD").exists() or bool(git_output(root, "rev-parse", "-q", "--verify", "MERGE_HEAD", check=False))


def unmerged_paths(root: Path) -> list[str]:
    output = git_output(root, "diff", "--name-only", "--diff-filter=U", check=False)
    return [line for line in output.splitlines() if line]


def staged_paths(root: Path) -> list[str]:
    output = git_output(root, "diff", "--cached", "--name-only")
    return [line for line in output.splitlines() if line]


def unstaged_tracked_paths(root: Path) -> list[str]:
    output = git_output(root, "diff", "--name-only")
    return [line for line in output.splitlines() if line]


def sync_marker_found(root: Path, upstream_base_ref: str) -> bool:
    marker_sha = short_sha(root, upstream_base_ref)
    log = git_output(root, "log", "--format=%s", "--max-count=300", "HEAD")
    lowered = log.lower()
    return marker_sha.lower() in lowered or "sync upstream master" in lowered


def auto_squash_base(root: Path, ctx: Context, requested: str) -> str | None:
    if requested == "none":
        return None
    if requested != "auto":
        return requested
    if ctx.upstream_branch != DEFAULT_UPSTREAM_BRANCH:
        return None
    candidate = f"refs/remotes/{ctx.upstream_remote}/master"
    if not git_success(root, "show-ref", "--verify", candidate):
        return None
    if not is_ancestor(root, candidate, ctx.upstream_ref):
        return None
    if is_ancestor(root, candidate, "HEAD"):
        return None
    return candidate if sync_marker_found(root, candidate) else None


def apply_diff_from_base(root: Path, base_ref: str, upstream_ref: str) -> None:
    diff = git(root, "diff", "--binary", f"{base_ref}..{upstream_ref}").stdout
    completed = run(
        ["git", "apply", "--3way", "--index"],
        cwd=root,
        check=False,
        input_text=diff,
    )
    if completed.returncode != 0:
        conflicts = unmerged_paths(root)
        conflict_text = "\n".join(conflicts) if conflicts else completed.stdout
        raise RuntimeError("Squash fallback stopped with conflicts:\n" + conflict_text)


def create_merge(root: Path, ctx: Context, squash_from: str | None) -> None:
    title = sync_title(ctx, short_sha(root, ctx.upstream_ref))
    if squash_from:
        print(f"Using squash fallback from {squash_from}.")
        completed = git(
            root,
            "merge",
            "--no-ff",
            "--no-commit",
            "-s",
            "ours",
            "-m",
            title,
            ctx.upstream_ref,
            check=False,
        )
        if completed.returncode != 0:
            raise RuntimeError("Could not create ours merge for squash fallback.")
        apply_diff_from_base(root, squash_from, ctx.upstream_ref)
        return

    completed = git(root, "merge", "--no-ff", "--no-commit", "-m", title, ctx.upstream_ref, check=False)
    if completed.returncode != 0:
        conflicts = unmerged_paths(root)
        conflict_text = "\n".join(conflicts) if conflicts else completed.stdout
        raise RuntimeError("Merge stopped with conflicts:\n" + conflict_text)


def repo_slug_from_remote(root: Path, remote: str) -> str | None:
    url = git_output(root, "remote", "get-url", "--push", remote, check=False)
    if not url:
        url = git_output(root, "remote", "get-url", remote, check=False)
    url = url.strip()
    patterns = [
        r"github\.com[:/]([^/]+)/([^/]+?)(?:\.git)?$",
        r"^([^/]+)/([^/]+)$",
    ]
    for pattern in patterns:
        match = re.search(pattern, url)
        if match:
            return f"{match.group(1)}/{match.group(2)}"
    return None


def sync_title(ctx: Context, upstream_short: str) -> str:
    return f"chore: sync upstream {ctx.upstream_branch} into {ctx.base_branch} ({upstream_short})"


def pr_body(ctx: Context, upstream_full: str, skipped_builds: bool) -> str:
    lines = [
        "## Summary",
        "",
        f"- Sync `{ctx.upstream_remote}/{ctx.upstream_branch}` at `{upstream_full}` into `{ctx.base_branch}`.",
        "- Keep the sync isolated on an agent branch for review.",
        "",
        "## Validation",
        "",
        "- `git diff --check`",
        "- `git diff --cached --check`",
    ]
    if skipped_builds:
        lines.append("- Builds skipped with `--skip-builds`")
    else:
        lines.extend(["- `pio run`", "- `pio run -e gh_release_cn`"])
    return "\n".join(lines) + "\n"


def check_conflict_markers(root: Path) -> None:
    left = "<" * 7
    right = ">" * 7
    completed = git(root, "grep", "-n", "-E", f"{left}|{right}", "--", ".", check=False)
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
            "Tracked files have unstaged changes. Stage resolved files before publishing:\n"
            + "\n".join(unstaged)
        )
    git(root, "diff", "--check")
    git(root, "diff", "--cached", "--check")
    check_conflict_markers(root)


def run_builds(root: Path, skip_builds: bool) -> None:
    if skip_builds:
        print("Skipping PlatformIO builds because --skip-builds was passed.")
        return
    run(["pio", "run"], cwd=root, capture=False)
    run(["pio", "run", "-e", "gh_release_cn"], cwd=root, capture=False)


def commit_if_needed(root: Path, ctx: Context) -> None:
    staged = staged_paths(root)
    if not staged and not has_merge_head(root):
        print("No staged changes or merge state to commit.")
        return
    upstream_short = short_sha(root, ctx.upstream_ref)
    upstream_full = full_sha(root, ctx.upstream_ref)
    title = sync_title(ctx, upstream_short)
    body = f"Upstream: {ctx.upstream_remote}/{ctx.upstream_branch} {upstream_full}"
    git(root, "commit", "-m", title, "-m", body)


def push_branch(root: Path, origin_remote: str) -> None:
    branch = current_branch(root)
    git(root, "push", "-u", origin_remote, branch)


def create_or_show_pr(root: Path, ctx: Context, draft: bool, skip_builds: bool) -> None:
    repo = repo_slug_from_remote(root, ctx.origin_remote)
    if not repo:
        raise RuntimeError(f"Could not parse GitHub repo from remote {ctx.origin_remote!r}.")
    branch = current_branch(root)
    existing = run(
        ["gh", "pr", "view", branch, "--repo", repo, "--json", "url", "-q", ".url"],
        cwd=root,
        check=False,
    )
    if existing.returncode == 0 and existing.stdout.strip():
        print(f"PR already exists: {existing.stdout.strip()}")
        return

    upstream_short = short_sha(root, ctx.upstream_ref)
    upstream_full = full_sha(root, ctx.upstream_ref)
    title = sync_title(ctx, upstream_short)
    body = pr_body(ctx, upstream_full, skip_builds)
    with tempfile.NamedTemporaryFile("w", delete=False, encoding="utf-8") as handle:
        handle.write(body)
        body_path = handle.name
    try:
        cmd = [
            "gh",
            "pr",
            "create",
            "--repo",
            repo,
            "--base",
            ctx.base_branch,
            "--head",
            branch,
            "--title",
            title,
            "--body-file",
            body_path,
        ]
        if draft:
            cmd.append("--draft")
        run(cmd, cwd=root)
    finally:
        os.unlink(body_path)


def build_context(args: argparse.Namespace) -> Context:
    root = repo_root()
    base_branch = choose_base_branch(root, args.origin_remote, args.base_branch)
    upstream_branch = choose_upstream_branch(root, args.upstream_remote, args.upstream_branch)
    return Context(
        root=root,
        base_branch=base_branch,
        upstream_remote=args.upstream_remote,
        origin_remote=args.origin_remote,
        upstream_branch=upstream_branch,
    )


def cmd_inspect(args: argparse.Namespace) -> int:
    ctx = build_context(args)
    fetch_branch(ctx.root, ctx.origin_remote, ctx.base_branch)
    fetch_branch(ctx.root, ctx.upstream_remote, ctx.upstream_branch)
    upstream_full = full_sha(ctx.root, ctx.upstream_ref)
    upstream_short = short_sha(ctx.root, ctx.upstream_ref)
    suggested = unique_branch_name(
        ctx.root,
        args.branch_prefix,
        ctx.upstream_branch,
        upstream_short,
        ctx.origin_remote,
    )
    summary = {
        "base_contains_skill": base_contains_script(ctx.root, ctx.origin_base_ref),
        "repo": str(ctx.root),
        "current_branch": current_branch(ctx.root),
        "base_branch": ctx.base_branch,
        "origin_remote": ctx.origin_remote,
        "upstream_remote": ctx.upstream_remote,
        "upstream_branch": ctx.upstream_branch,
        "upstream_sha": upstream_full,
        "suggested_branch": suggested,
    }
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


def cmd_start(args: argparse.Namespace) -> int:
    ctx = build_context(args)
    require_clean_worktree(ctx.root)
    fetch_branch(ctx.root, ctx.origin_remote, ctx.base_branch)
    fetch_branch(ctx.root, ctx.upstream_remote, ctx.upstream_branch)
    require_base_contains_script(ctx.root, ctx.origin_base_ref)
    if args.squash_from == "auto" and ctx.upstream_branch == DEFAULT_UPSTREAM_BRANCH:
        git(
            ctx.root,
            "fetch",
            ctx.upstream_remote,
            f"+refs/heads/master:refs/remotes/{ctx.upstream_remote}/master",
            check=False,
        )

    upstream_short = short_sha(ctx.root, ctx.upstream_ref)
    if is_ancestor(ctx.root, ctx.upstream_ref, ctx.origin_base_ref):
        print(f"{ctx.origin_base_ref} already contains {ctx.upstream_ref}.")
        return 0

    branch = unique_branch_name(ctx.root, args.branch_prefix, ctx.upstream_branch, upstream_short, ctx.origin_remote)
    git(ctx.root, "switch", "-c", branch, ctx.origin_base_ref)
    squash_from = auto_squash_base(ctx.root, ctx, args.squash_from)
    create_merge(ctx.root, ctx, squash_from)
    git(ctx.root, "submodule", "update", "--init", "--recursive")
    print("Merge staged. Run publish after resolving any review concerns.")
    return 0


def cmd_publish(args: argparse.Namespace) -> int:
    ctx = build_context(args)
    require_not_base_branch(ctx.root, ctx.base_branch)
    fetch_branch(ctx.root, ctx.upstream_remote, ctx.upstream_branch)
    validate_index(ctx.root)
    run_builds(ctx.root, args.skip_builds)
    commit_if_needed(ctx.root, ctx)
    push_branch(ctx.root, ctx.origin_remote)
    create_or_show_pr(ctx.root, ctx, args.draft, args.skip_builds)
    return 0


def cmd_run(args: argparse.Namespace) -> int:
    start_status = cmd_start(args)
    if start_status != 0:
        return start_status
    return cmd_publish(args)


def add_common_options(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--origin-remote", default="origin", help="remote to push branches to")
    parser.add_argument("--upstream-remote", default="upstream", help="remote to sync from")
    parser.add_argument("--base-branch", default="auto", help="base branch, or auto from sc/origin HEAD")
    parser.add_argument(
        "--upstream-branch",
        default=DEFAULT_UPSTREAM_BRANCH,
        help=f"upstream branch to sync, default: {DEFAULT_UPSTREAM_BRANCH}",
    )
    parser.add_argument("--branch-prefix", default=DEFAULT_BRANCH_PREFIX, help="prefix for generated sync branches")
    parser.add_argument(
        "--squash-from",
        default="auto",
        help="base ref for squash fallback, 'auto', or 'none'",
    )


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    inspect_parser = subparsers.add_parser("inspect", help="show detected remotes, branches, and suggested branch")
    add_common_options(inspect_parser)
    inspect_parser.set_defaults(func=cmd_inspect)

    start_parser = subparsers.add_parser("start", help="create a sync branch and stage the upstream merge")
    add_common_options(start_parser)
    start_parser.set_defaults(func=cmd_start)

    publish_parser = subparsers.add_parser("publish", help="validate, commit, push, and open a PR")
    add_common_options(publish_parser)
    publish_parser.add_argument("--draft", action="store_true", default=True, help="open the PR as draft")
    publish_parser.add_argument("--ready", action="store_false", dest="draft", help="open a ready-for-review PR")
    publish_parser.add_argument("--skip-builds", action="store_true", help="skip PlatformIO build validation")
    publish_parser.set_defaults(func=cmd_publish)

    run_parser = subparsers.add_parser("run", help="start and publish the sync in one command")
    add_common_options(run_parser)
    run_parser.add_argument("--draft", action="store_true", default=True, help="open the PR as draft")
    run_parser.add_argument("--ready", action="store_false", dest="draft", help="open a ready-for-review PR")
    run_parser.add_argument("--skip-builds", action="store_true", help="skip PlatformIO build validation")
    run_parser.set_defaults(func=cmd_run)
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
