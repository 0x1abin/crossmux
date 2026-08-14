# Git Workflow & Repository Awareness

> Deep reference for [CLAUDE.md](../../CLAUDE.md). Verify repository context before
> any git operation, and never commit without explicit request. For the
> contributor-facing fork/branch/PR flow see
> [../contributing/development-workflow.md](../contributing/development-workflow.md).

## Repository Detection Protocol

**CRITICAL**: ALWAYS verify repository context before git operations. This could be:
- A **fork** with `origin` pointing to personal repo, `upstream` to main repo
- A **direct clone** with `origin` pointing to main repo
- Multiple collaborator remotes

**Verification Commands** (run at session start):
```bash
# Check current branch
git branch --show-current

# Check all remotes
git remote -v

# Identify main branch name (could be 'main' or 'master')
git symbolic-ref refs/remotes/origin/HEAD 2>/dev/null | sed 's@^refs/remotes/origin/@@'

# Check working tree status
git status --short
```

**Example Output** (forked repository):
```text
origin      https://github.com/<your-username>/crosspoint-reader.git (fetch/push)
upstream    https://github.com/crosspoint-reader/crosspoint-reader.git (fetch/push)
```

## Git Operation Rules

1. **Never assume branch names**:
   ```bash
   # Bad: git push origin main
   # Good: git push origin $(git branch --show-current)
   ```

2. **Use this project's default PR target**:
   - For an unqualified PR request, push the feature branch to `origin` and open the PR against
     `0x1abin/crossmux:main` without asking for the target again.
   - An explicit user-supplied repository or base branch overrides this default.
   - Still verify that `origin` resolves to `0x1abin/crossmux` and that the current credentials can push; ask only
     when the configured repository differs or required access is unavailable.

3. **Check for upstream changes before starting work**:
   ```bash
   # Sync fork with upstream (if applicable)
   git fetch upstream
   git merge upstream/main  # or upstream/master
   ```

4. **Use explicit remote and branch names**:
   ```bash
   # Check remotes first
   git remote -v

   # Use explicit syntax
   git push <remote> <branch>
   ```

## Branch Naming Convention

**For feature/fix branches**:
```text
feature/<short-description>       # New features
fix/<issue-number>-<description>  # Bug fixes
refactor/<component-name>         # Code refactoring
docs/<topic>                      # Documentation updates
```

**Examples**:
- `feature/sd-download-progress`
- `fix/123-orientation-crash`
- `refactor/hal-storage`

## Commit Message Format

**Pattern**:
```text
<type>: <short summary (50 chars max)>

<optional detailed description>

```

**Types**: `feat`, `fix`, `refactor`, `docs`, `test`, `chore`, `perf`

**Example**:
```text
feat: add real-time SD download progress bar

Implements progress tracking for book downloads using
UITheme progress bar component with heap-safe updates.

Tested in all 4 orientations with 5MB+ files.
```

## When to Commit

**DO commit when**:
- User explicitly requests: "commit these changes"
- Feature is complete and tested on device
- Bug fix is verified working
- Refactoring preserves all functionality
- All tests pass (`pio run` succeeds)

**DO NOT commit when**:
- Changes are untested on actual hardware
- Build fails or has warnings
- Experimenting or debugging in progress
- User hasn't explicitly requested commit
- Files excluded by `.gitignore` would be included — always run `git status` and cross-check against `.gitignore` before staging (e.g., `*.generated.h`, `.pio/`, `compile_commands.json`, `platformio.local.ini`)

**Rule**: **If uncertain, ASK before committing.**
