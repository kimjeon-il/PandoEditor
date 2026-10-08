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


## Historical GIS reconstruction policy (mandatory)

For **all historical borders, coastlines, reclamations, river boundaries,
territorial polygons and administrative boundaries**, read and follow
[docs/historical-border-reconstruction-policy.md](docs/historical-border-reconstruction-policy.md)
**before researching, digitizing, simplifying or validating geometry**.

- Prioritize reference-date correctness, source revision history, ownership and
  boundary topology over subpixel or cadastral-precision tracing.
- Use about 1:100,000 for broad screening and about **1:25,000 as the normal
  detailed historical source**. Consult 1:10,000 or finer maps only for a
  specific discrepancy or historically meaningful exception.
- The current **flat-map visual review baseline** is maximum `flatZoom=64`,
  **2560 CSS px map-content width**, with a **0.5 CSS px maximum screen-space
  discrepancy** where independent historical geometry is available. App zoom
  limits must be translated to equivalent actual display resolution.
- A screen tolerance **never overrides** country/settlement attribution,
  border topology, river-bank/thalweg rules, date changes or land reclamation.
- If no independent period boundary has been digitized, do not report the
  0.5px requirement as tested or passed; label visual-only checks explicitly.
- Keep original evidence and master geometry; simplify presentation geometry
  only as a separate derivative, preserving shared-boundary connectivity.
- Record sources, dates, checked scale/resolution, evidence status and
  outstanding exceptions; never claim that an unrun validation passed.

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
