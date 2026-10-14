# UI stack — handoff for the next session

**Date:** 2026-10-13
**Repo:** `/home/kate/GitHub/DelugeFirmware-clean`
**Written for:** a session picking up the UI stack redesign after `PerformanceScene`'s spec was
written. Read §1 to §4 before touching anything; §5 onwards is the work queue.

---

## 1. Where the stack is

```
next 5fc6c84d9
 └─ … ui/wiring-3 23aae2ef2
     └─ ui/pads-1 20aca8e4f      (18 code commits to e213d3c6a, + 2 spec-only commits)
         └─ ui/notes-1 e62819022 (52 commits; note family DONE, suites green)
             └─ ui/perf-1       (new; carries this handoff and the PerformanceScene spec)
```

Checked on 2026-10-13: local `next` is an ancestor of the whole stack (0 behind / 572 ahead at
`ui/notes-1`). `origin/next` is `ed0cf1d10` and has not moved; local `next` is 925 commits ahead of
it. `origin/main` has moved to `fc9184885`, 131 commits `next` has not merged — a `next`-side
decision, irrelevant to this stack.

**Branch in place. No worktrees** — Kate's standing instruction.

Each completed plan's record: spec in `docs/superpowers/specs/`, plan in `docs/superpowers/plans/`,
execution ledger in the git-ignored `.superpowers/sdd/<plan-basename>/progress.md`. The note family's
workspace is kept at Kate's request, including `rulings-for-kate.md`.

## 2. Standing constraints — these are Kate's, not suggestions

* **No new scenes, UI or UX that did not exist before.** A comment saying "legacy has none" or "spec
  design, not a port" is a defect, not a justification.
* **Legacy bug fixes are Kate's call.** Files under `gui/menu_item/`, `gui/ui/sound_editor.cpp` and
  `gui/views/` change only at edits named in an approved spec.
* **Never edit `src/bsp/rza1`** — retired dead source.
* **Never commit `src/bsp/rust/Cargo.lock`.**
* **`git add` named paths only.** Never `git add -A` or `git add .`. The untracked
  `release-blockers.md` must never be committed.
* **Naming:** snake_case under `src/shell`, `src/scenes`, `src/ui`, `src/notify`; camelCase under
  `src/deluge`.
* **Integration decisions — merge, PR or keep — are Kate's**, for every branch in the stack.
* **No controller edits or commits while an implementer subagent is running**, and do not commit docs
  while an implementer holds the citation index.

## 3. The gate, and what is not evidence

```
cmake --build build-sim --target deluge_host deluge_scenes_spec -j8 && ctest --test-dir build-sim
cmake --build build/tests -j8 && ctest --test-dir build/tests -C Debug
scripts/check_layer_rules.py (+ --audit-symbols), scripts/check_platform_boundary.py,
scripts/check_gfx_layer.py, scripts/check_citations.py --strict,
scripts/check_citation_shift.py <base>..HEAD   (advisory; the criterion is "zero drift")
scripts/menu_dump_check.sh
```

* **Never** run a bare `cmake --build build-sim` — it pulls in targets that cannot link here.
* **`./dbt build Debug` is never evidence** for this work. Device links are broken on `next` for
  reasons unrelated to the stack (LLVM skew, ld overlap, Debug too big for RAM).
* "Builds" means: C++ compiles to the known ThinLTO link failure, `deluge_host` links, host suite
  green.
* `scripts/check_layer_rules.py` is a **ratchet**: its baselines may only shrink.

**Environment traps learned the hard way:**

* The shell is **zsh** — an unquoted `$VAR` is *not* word-split. A parking attempt that relied on
  word-splitting produced an empty patch.
* zsh reads `$B:src/...` as a history modifier. Write `"${B}:src/..."`.
* `grep` is **ugrep**: empty alternations like `(a|)` are rejected. Write `(a )?`.

## 4. Method, in one paragraph

Kate runs this with superpowers: **brainstorming** (sections approved one at a time, then a written
spec she reviews) → **writing-plans** (bite-sized TDD tasks, the plan argues from the spec) →
**subagent-driven-development** (fresh implementer per task, task review after each, one final
whole-branch review on the most capable model, one fix wave). The controller rules on conflicts
rather than stalling, and records each as "what — why — cost if wrong" in the ledger. Four things
stop execution: irreversible operations, security-sensitive actions, side effects outside the
worktree (merge, push, publish), and a plan broken beyond guessing.

Two lessons from earlier plans worth keeping:

* **A mutation that passes is the finding.** If deleting the port's code leaves the suite green, the
  oracle is a mirror, not a driver.
* **Ask of every A/B extension: what gesture would a player make that this script never makes?** That
  question is what found the guest pad-forwarding divergence after five task reviews missed it.

## 5. The work queue

### 5.1 `PerformanceScene` — spec written, plan next

* Spec: `docs/superpowers/specs/2026-10-13-performance-scene-design.md` (this branch).
* Status: approved section by section in brainstorming; awaiting Kate's review of the written spec.
* Next step after her approval: invoke **writing-plans** for it, on `ui/perf-1`.
* The spec commits to two named legacy edits and nothing else: moving three plain structs out of
  `gui/views/performance_view.h` into `model/performance/performance_types.h`, and one
  `friend struct PerformanceViewOracle;` line for the layout-format oracle.
* It deliberately does **not** port: any exit to a non-scene root, the scene→menu push, the MIDI-driven
  display refresh, the c1.3 card migration, `layoutBank`/`layoutVariant` (dead).
* Open questions it leaves to the plan: whether any encoder arm still scrolls or zooms (decides
  whether `timeline::TimelineState` is adopted), and the oracle count in §10.6/§10.7's shape.

### 5.2 `KeyboardScene` — E5's second spec, not yet brainstormed

The hard part is known and unresolved. Measured on 2026-10-13:

* `src/deluge/gui/ui/keyboard/**` is **5,252 lines**; `keyboard_screen.cpp` is 924.
  `KeyboardScreen : RootUI, InstrumentClipMinder` — one of only two `RootUI` classes in the tree.
* `KeyboardLayout`'s virtuals: `evaluatePads`, `handleVerticalEncoder`, `handleHorizontalEncoder`,
  `precalculate`, `renderPads`, `renderSidebarPads`, `name`, the `supports*` predicates,
  `requiredScaleMode`, `getNotesState`.
* The layouts and the column controls reach legacy UI globals: `display->` on 23 lines,
  `keyboardScreen` on 15, `soundEditor` on 7, `getCurrentUI` on 6, `changeRootUI(` on 2, plus
  includes of `gui/ui/sound_editor.h` (6), `gui/ui/audio_recorder.h` (5),
  `gui/ui/browser/sample_browser.h` (4), `hid/buttons.h` (6), `hid/display/display.h` (7).
* **Therefore spec E's "wrap the existing layouts unchanged" conflicts with the layer rules**
  (`scenes-includes-gui`, `scenes-no-root-ui`, and the hid/ ban). Resolving that conflict is the first
  question for its brainstorming: wrap behind a bridge like `PerformanceBridge`/`NoteBridge`, port the
  layouts, or split the layout interface so the pure half moves and the UI half stays.
* Everything `PerformanceScene` establishes — the bridge pattern, the `is_root` arm wiring, the
  oracle shape — applies directly.

### 5.3 After E5

* **`SessionScene` / `ArrangerScene`.** Needed to retire `PerformanceBridge`'s four pointers and to
  give the sidebar a real port. Spec E section 7 row 3's remainder.
* **Spec F: the firmware wiring.** `docs/superpowers/specs/2026-08-26-ui-migration-design.md` §10.2
  lists **nine** blockers. The highest-value one: **no application can install a service at all** —
  services need the `Shell`'s own `SceneStack&`/`InputLowering&`, `Services` is taken by value in the
  constructor, and there is no `set_services`. Nothing links `deluge_scenes` yet
  (`src/CMakeLists.txt`); the firmware still runs `SoundEditor`, and only the legacy edits in these
  branches actually ship.
* **Phase 6: the load/save browser family** — seven UIs `BrowserScene` does not carry; see
  `src/scenes/README.md`'s carry list.
* **The host-side choice-poll spec**, carried from an earlier session and still unwritten.

## 6. Decisions waiting on Kate

Do not guess at these; surface them and continue elsewhere.

1. **Integration for every branch in the stack** — `ui/notes-1` (she chose "keep as-is" for now),
   `ui/pads-1` (two spec-only commits past its approved review point), `ui/wiring-3` and earlier.
2. **The three device-reachable defects from E4** belong in a bugfix PR against `main`, independent of
   this stack, and that PR has not been opened: `ClipView::shortenClip`/`lengthenClip` dereference
   `actionLogger.firstAction[BEFORE]` unguarded (`clip_view.cpp:98`, `:129`);
   `Slicer::renderMainPads` never null-checks `image`; `View::setKnobIndicatorLevel` reads an
   uninitialised `knobPos` on one of four paths.
3. **The banked null-deref fix** (migration spec §10.5) and the **four inventions** awaiting a ruling.
4. **Four legacy entry points still open the legacy sound editor** (rows 9, 10, 11, 14 of
   `gui/ui/shell_menu_entry.cpp`'s fourteen): `enterOrUpdateSoundEditor`/dx param,
   `SampleBrowser::claimCurrentFile`, `createNewInstrument`→dxMenu, `enterDrumCreator`→file0 selector.
5. **The note family's recorded divergences**, listed in full in
   `.superpowers/sdd/2026-09-10-note-family/rulings-for-kate.md` §2 — the one with the widest reach is
   that **`ShellHostUI` never re-assesses pad greyout after its first push** (`gui/ui/ui.cpp:234`
   samples before `opened()` pushes), which affects every ported root.
6. **Legacy's `Submenu` row memory across re-opens** vs `MenuScene::open`'s unconditional reset
   (`submenu.cpp:21-34` vs `menu_scene.cpp:331-335`, which resets `MenuState{}` wholesale) — player-reachable; `tests/ui-scripts/app/menu.txt`
   carries one `encoder Select -5` gesture solely to converge the legs past it.
7. **Deleting the note family's SDD workspace** — she chose to keep it; it is not approved for
   deletion.

## 7. The async-SD rung ladder, and why it shapes port decisions

Kate's storage migration lives in a **different clone**: `~/GitHub/DelugeFirmware`, branches
`feat/rung1-async-load` → `feat/rung2-preview` → `feat/rung3-browser-listing`. rung3 sits on that
clone's `next` (`f50ee94f2`, 54 commits ahead of this clone's `next`) and is 336 commits past it.

* The seam is `deluge::storage::bytes` (`src/deluge/storage/byte_channel.h`): request/collect pairs
  (`requestRead`/`collectRead`, `requestWrite`/`collectWrite`, rename, unlink, exists, mkdirAll,
  setTime, readAt/writeAt, stat, copy), `ByteIo {Pending, Ready, Unknown, Failed}`,
  `usingByteChannel()`, and settings-level `submitRead(...)`/`submitWrite(...)`.
* **Nothing in that layer parses.** XML handling, policy and migration stay with the consumers.
* rung3 has already moved `PerformanceView.XML`'s reads (`e8789736d`: `parseDefaultsBytes`,
  `defaultsReadDone`) and its write (`writeDefaultsToFile` → `submitWrite`) onto the channel; eight
  rung3 commits touch that file's storage functions.
* **Therefore:** a ported scene must never call `StorageManager` synchronously, and **must not edit
  legacy storage functions on the UI stack**. Port parse and serialise as pure functions over
  `Serializer&`/`Deserializer&`, and put I/O behind a tick-polled request/collect port, the shape
  `src/scenes/browser/listing.h` already uses. When rung3 lands, the port's backing becomes a few
  lines over `storage::bytes`.
* Useful fact for this stack: in-memory XML needs **no** storage edits here —
  `FileWriter::setMemoryBased()` (`storage_manager.h:99`) and `FileDeserializer(uint8_t*, size_t)`
  (`:207`) already exist.

## 8. Defects the port has recorded and not fixed

`src/scenes/README.md` plus migration spec §10.5 ("Twelve defects in shipping firmware, found by the
port and not fixed by it") are the authoritative lists; `rulings-for-kate.md` §2 adds the note
family's. Treat all of them as read-only unless Kate names one.

Two upstream legacy bugs found and deliberately left: opening ACTIONS on a MIDI or gate drum row
dereferences a null `Sound` in `osc::AudioRecorder::isRelevant`; and the divisor-8 `iteranceStep`
bitset read is UB, transcribed faithfully.

## 9. Where the records are

| what | where |
|---|---|
| the UI specs (E, F, shell, toolkit, harness, notifications, migration) | `docs/superpowers/specs/2026-08-26-*` |
| the migration spec's amendment, blockers, defects and oracle counts | `2026-08-26-ui-migration-design.md` §10.1–§10.11 |
| per-plan implementation plans | `docs/superpowers/plans/` |
| per-plan execution ledgers (git-ignored, losable) | `.superpowers/sdd/<plan-basename>/progress.md` |
| the note family's curated rulings for Kate | `.superpowers/sdd/2026-09-10-note-family/rulings-for-kate.md` |
| cross-session facts | `~/.claude/projects/-home-kate-GitHub-DelugeFirmware-clean/memory/` — `ui-stack-branch-stack-status.md` is the long-form plan history, `async-sd-rung-ladder.md` the storage seam, `ui-stack-no-new-ux.md` the no-new-UX rule |

Each plan writes its own oracle count into the migration spec in §10.6's shape ("N legacy entry
points driven from their own sentinel across M comparison sites"). That number, not a line count, is
what the legacy deletion gets scheduled against.
