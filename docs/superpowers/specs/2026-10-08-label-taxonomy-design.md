# Label Taxonomy — Design

**Date:** 2026-10-08
**Status:** Draft for review
**Scope:** Sub-project 1 of the GitHub triage/automation effort. Later sub-projects
(issue-form auto-labeling, PR template + enforcement, LLM triage, changelog gate,
lifecycle bots, release-blocker tracking, CODEOWNERS routing) all build on the
taxonomy defined here.

## Goal

Replace the current ad-hoc set of 47 labels with a small, grouped, version-controlled
taxonomy that humans and bots can apply consistently, and use GitHub's native issue
types and issue fields where they fit.

Success means:

- Every label belongs to exactly one group with a clear rule for when to apply it.
- The label set is defined in the repo and changed only through PRs.
- No existing issue or PR loses the meaning of a label it carries today.
- Issues use native Issue Type / Priority / Effort instead of labels for those concepts.

## Current state (measured 2026-10-08)

- 1,143 issues (551 unlabeled), 3,439 PRs (2,032 unlabeled).
- Duplicates / overlap: `bug`/`bugfix`, `dependencies`/`pr-dependencies`,
  `sound`/`sys-audio`, `enhancement`/`new-feature`.
- Typo: `ui-undesireable`.
- `cherry-pick` (1,060 PRs) / `cherry-picked` (548 PRs) are historical; the workflow
  using them was removed in #4603.
- The only in-repo reference to label names is `.github/dependabot.yml`
  (`pr-dependencies`, `github workflow`).
- Org issue types exist and are in use: `Bug`, `Feature`, `Task`.
- Org issue fields exist: **Priority** (Urgent/High/Medium/Low) and **Effort**
  (High/Medium/Low), both with visibility `ORG_ONLY`.

## Decisions

1. **Prefixed label groups**: `type:`, `area:`, `status:`, `impact:`, one color per group.
2. **~14 coarse `area:` labels** aligned with `src/deluge` subsystems.
3. **Issues use native fields; PRs use labels.**
   - Issues: Issue Type (Bug/Feature/Task), Priority field, Effort field.
   - PRs: `type:` labels (PRs have no issue types or issue fields).
   - `area:`, `status:`, `impact:`, and blocker labels apply to both.
4. **Release gates stay labels** (`release-blocker`, `beta-blocker`): they are not
   priorities and must be markable on PRs.
5. **GitHub-recognised names stay unprefixed** (`good first issue`, `help wanted`)
   because GitHub uses those exact names to surface contributor-friendly issues.

## Taxonomy

### `type:` — PRs only — color `#5319E7`

| Label | From | Meaning |
|---|---|---|
| `type: fix` | `bugfix` | Fixes a bug |
| `type: feature` | `enhancement` (+ merge `new-feature`, `proof of concept`) | New or changed user-facing behavior |
| `type: refactor` | `refactor` | No intended behavior change |
| `type: deps` | `pr-dependencies` | Dependency updates (dependabot) |
| `type: chore` | new | Build, CI, tooling, docs-only, housekeeping |

### `area:` — issues and PRs — color `#0075CA`

| Label | From | Primary paths |
|---|---|---|
| `area: audio` | `sys-audio` (+ merge `sound`) | `dsp/`, `processing/`, `model/voice/`, `model/sample/` |
| `area: sequencer` | `sys-sequencer` | `model/clip/`, `model/note/`, `model/iterance/`, `model/song/`, `playback/` |
| `area: automation` | `sys-automation` | `modulation/` (automation) |
| `area: midi` | `sys-midi` | `io/midi/`, `model/midi/` |
| `area: cv-gate` | new | CV/gate drivers |
| `area: browser` | `sys-browser` | File browser UI (`gui/ui/browser/`) |
| `area: files` | new | Song/preset load & save (`storage/`) |
| `area: menus` | new | `gui/menu_item/`, `gui/context_menu/` |
| `area: ui` | `ui-rendering` | `gui/ui/`, `gui/views/`, `hid/` — pads, OLED, 7SEG |
| `area: system` | new | `memory/`, `OSLikeStuff/`, `drivers/`, `RZA1/` — boot, crash handler |
| `area: toolchain` | `toolchain` (+ merge `vscode`, `python`) | `dbt*`, `toolchain/`, `scripts/`, `IDE_Configs/` |
| `area: ci` | `github workflow` | `.github/` |
| `area: website` | `website` | `website/` |
| `area: docs` | `documentation` | `docs/`, `*.md` |

The path column is guidance for humans now and the starting point for path-based PR
labeling in sub-project 3; it is not enforced by this sub-project.

### `status:` — issues and PRs — color `#FBCA04`

| Label | From | Meaning |
|---|---|---|
| `status: needs-info` | new | Waiting on reporter for details |
| `status: no-repro` | `no-repro` | Cannot reproduce with the information given |
| `status: needs-testing` | `needs testing` | PR needs hardware testing |
| `status: needs-review` | `Code Review wanted` | PR is ready and wants a reviewer |
| `status: quick-review` | `small-pr` | PR is small; quick review welcome |
| `status: deferred` | `deferred` | Intentionally parked |
| `status: stale` | `stale` | No activity; will be closed |
| `status: merge-conflict` | `merge conflict` | PR has conflicts |
| `status: manual-merge` | `pr-needs manual merge` | Cannot go through the merge queue |
| `status: wontfix` | `wontfix` | Will not be worked on |
| `status: duplicate` | `duplicate` | Duplicate of another item |

### `impact:` — mostly issues — color `#D93F0B`

| Label | From | Meaning |
|---|---|---|
| `impact: crash` | `crash` | Hang, freeze, or crash |
| `impact: memory-corruption` | `memory-corruption` | Memory corruption |
| `impact: performance` | `performance` | Performance below expectations |
| `impact: official-firmware` | `official` | Also present on official firmware |
| `impact: ux-inconsistent` | `ui-inconsistent` | Behavior is unexpected / inconsistent |
| `impact: ux-undesirable` | `ui-undesireable` | Not a bug, but should change |
| `impact: accessibility` | `ui-accessibility` | Makes the Deluge hard to use |

### Unprefixed (kept as-is, existing colors)

`release-blocker`, `beta-blocker`, `good first issue`, `help wanted`,
`cherry-pick`, `cherry-picked`.

### Deleted

| Label | Uses | Handling |
|---|---|---|
| `sound` | 13 | merged into `area: audio` first |
| `new-feature` | 0 | merged into `type: feature` |
| `proof of concept` | 1 | merged into `type: feature` |
| `vscode` | 6 | merged into `area: toolchain` first |
| `python` | 5 | merged into `area: toolchain` first |
| `bug` | 2 | deleted (issues carry Issue Type) |
| `dependencies` | 1 | deleted |
| `javascript` | 1 | deleted |
| `spam` | 1 | deleted |
| `question` | 3 | deleted (questions go to Discussions) |

### Native issue fields (issues only)

| Field | Values | Notes |
|---|---|---|
| Issue Type | Bug, Feature, Task | Already configured at org level |
| Priority | Urgent, High, Medium, Low | Urgent = crash or data loss |
| Effort | High, Medium, Low | Set by maintainers when triaging |

**Access caveat:** both fields are `ORG_ONLY`. Repo collaborators who are not org
members cannot see or set them. An org owner must either add maintainers to the org
or change field visibility to `ALL` (which makes values publicly visible; editing
still requires triage access). Whether the Actions `GITHUB_TOKEN` can write
`ORG_ONLY` fields is unverified and will be tested in the triage-bot sub-project.
This sub-project does not depend on the fields being writable.

## Components

### 1. `.github/labels.yml`

Source of truth, in the format consumed by `crazy-max/ghaction-github-labeler`:

```yaml
- name: "area: audio"
  color: "0075ca"
  description: "Audio engine, DSP, voices, samples"
  from_name: "sys-audio"
```

`from_name` entries remain after the first sync; they are no-ops once the old label
is gone.

### 2. `.github/workflows/sync-labels.yml`

- Action: `crazy-max/ghaction-github-labeler@v6`.
- Triggers:
  - `push` to `main` with `paths: [.github/labels.yml]` → apply.
  - `pull_request` touching `.github/labels.yml` → `dry-run: true`, so reviewers see
    planned changes in the job log.
  - `workflow_dispatch` → apply.
- `skip-delete: true` — labels are only ever deleted explicitly by the migration
  script, so a label created in the UI is never silently stripped from items.
- Permissions: `issues: write` (apply), `contents: read`.

### 3. `.github/scripts/migrate-labels.py`

One-shot migration, run locally by a maintainer with an authenticated `gh`.
Python standard library only; shells out to `gh api` / `gh api graphql`.
Dry-run by default; `--apply` performs writes.

- `pre` phase (before merging the PR):
  1. Snapshot every issue and PR's labels and Issue Type to
     `label-snapshot-<timestamp>.json`.
  2. Merge secondary labels into their primary source label so the rename carries them:
     `sound`→`sys-audio`, `new-feature`/`proof of concept`→`enhancement`,
     `vscode`/`python`→`toolchain`.
- `post` phase (after the sync workflow has applied renames):
  1. For each **issue** carrying a type-like label, set the Issue Type if it has none,
     then remove the label: `type: feature`→Feature, `type: fix`→Bug,
     `type: refactor`→Task, `bug`→Bug.
  2. Delete the merged-away and dead labels listed above.
- `verify` phase: re-fetch all items and compare against the snapshot using the
  rename/merge map. Print every item whose label meaning was not preserved. Exit
  non-zero if any.

The script is idempotent: re-running any phase makes no further changes.

### 4. `.github/dependabot.yml`

Replace `pr-dependencies` → `type: deps` and `github workflow` → `area: ci`, in the
same PR as `labels.yml`.

### 5. `docs/CONTRIBUTING.md`

Add a short "Labels" section: the four groups and when to use each, that issues use
Issue Type / Priority / Effort instead of `type:` labels, and that label changes go
through `.github/labels.yml`.

## Rollout

1. Open the PR. Review the sync job's dry-run output.
2. Run `migrate-labels.py pre --apply`.
3. Merge the PR → sync workflow renames/creates labels.
4. Run `migrate-labels.py post --apply`.
5. Run `migrate-labels.py verify`; resolve any reported mismatches.

Between steps 3 and 4 some issues temporarily carry `type:` labels; this is harmless.

## Rollback

- Renames: swap `name` and `from_name` in `labels.yml` and push.
- Merged/removed labels and Issue Types: restore from the `pre` snapshot JSON.

## Testing

- PR dry-run output of the sync action is the review artifact for renames/creates.
- The migration script's dry-run output lists every planned write with counts.
- `verify` phase is the acceptance check: zero mismatches against the snapshot.

## Out of scope

- Applying labels automatically (sub-projects 2–4).
- Setting Priority/Effort on existing issues.
- Org membership or field visibility changes (an org-owner action, tracked separately).
