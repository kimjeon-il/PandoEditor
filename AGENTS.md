# AGENTS.md — PandoEditor (Qt application)

These instructions apply throughout the PandoEditor repository unless more
specific directory-level agent instructions apply.

## Branch and permanent worktree policy (mandatory)

Read [docs/branch-policy.md](docs/branch-policy.md) before choosing a branch or
local worktree. These rules apply to this repository and its independently
managed Web/App counterpart:

- Reuse the long-lived branches `work/ui`, `work/objects`, `work/gis`, and
  `work/places` according to the task's primary purpose; `main` is the
  stable integration branch.
- Before local changes, run `git worktree list --porcelain` and inspect the
  assigned worktree's branch, `git status --short --branch`, and uncommitted or
  untracked files. Reuse the existing permanent worktree for that branch.
- Create a permanent worktree only if it does not yet exist and is needed for
  local work. Do not generate new branches/worktrees per task by default.
- After a task, leave the worktree in place for the next task. Do not
  automatically delete/prune worktrees, branches, directories, caches, or
  locally generated files.
- Never discard unrelated or uncommitted work, including via automatic stash,
  `git clean`, `git reset --hard`, force checkout, force push, or force
  worktree removal. Request explicit user approval for destructive operations.
- Synchronize the latest `main` **into** the appropriate `work/*` branch
  only after checking the local and remote state; preserve unfinished work and
  resolve conflicts without silently choosing one side.
- Never merge `work/*` into `main`, commit directly to `main`, deploy, or
  transfer changes between categories without explicit user instructions.
- Web and App are separate repositories: matching branch names do not imply
  matching code or commits. Verify and maintain each repository independently.
- Exception branches or temporary worktrees require a concrete need and user
  approval. Do not delete them automatically after integration.


## App-specific implementation rules

- PandoEditor is an independent C++ / Qt application repository; never assume
  Web-side features, code paths, or CI results are automatically present in App.
- Prefer the existing domain/model/controller, Qt/QML, and persistence owners.
  Avoid duplicate APIs and do not replace canonical logic merely to mimic Web UI.
- Respect existing source and build directory separation for each permanent
  worktree. Do not overwrite another worktree's Qt/CMake build products.
- Preserve the current project format, round-trip contracts, and undo/redo
  behavior unless the user explicitly authorizes a format or contract change.
- Run checks focused on the changed App code, including appropriate CMake/CTest
  or Qt UI tests when applicable; record anything not actually executed.
