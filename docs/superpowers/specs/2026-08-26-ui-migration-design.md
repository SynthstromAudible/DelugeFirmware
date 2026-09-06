# UI migration and coexistence

**Date:** 2026-08-26
**Branch target:** `next`
**Status:** design, pending review. Spec F of the UI stack redesign
(`2026-08-26-ui-stack-redesign-design.md` §12). Depends on specs B–E and G for the shapes
it adapts between.

Naming: `snake_case` functions/variables; `PascalCase` types.

## 1. Why

97k lines of UI cannot be replaced in one step, and the project is a living fork with
community PRs landing continuously. The migration must keep the firmware shippable after
every PR, keep in-flight contributor branches mergeable, and give a mechanical, visible
measure of progress. This spec fixes the two coexistence topologies, the shared-buffer
rules, the façade/codemod/ratchet mechanics, the branch strategy, and the deletion
schedule.

Divergence facts (2026-08-26): the `main`↔`next` merge-base is 2026-08-10; since then
`main` changed 21 UI files in 16 commits (mostly l10n strings, `instrument_clip_view.cpp`,
`browser.cpp`). The often-quoted "367 commits behind" is almost entirely non-UI; UI
reconciliation is cheap.

## 2. Two topologies, in sequence

### 2.1 Phase 4 — shell as guest

`ShellHostUI : UI` is one legacy `UI` registered like any other and pushed with
`openUI(&shell_host_ui)`. Inside it a full `Shell` runs its own scene stack.

| Legacy hook | Guest behaviour |
|---|---|
| `padAction`, `buttonAction`, `*EncoderAction`, `modEncoderAction` | forwarded into the shell's `InputLowering`; legacy `Buttons::` state is *read* into `UiContext` (not duplicated) |
| `renderOLED(Canvas&)` | the shell's `Main` surface aliases that canvas's storage |
| `renderMainPads(rows, image, mask)` / `renderSidebar(...)` | the shell's grid surface aliases `image` row by row; returns the top scene's opacity |
| `timerCallback()` | the shell's `tick`; `UI_SPECIFIC` re-armed to `next_wake_` |
| `opened()` / `focusRegained()` | `Shell::push(initial)` / `on_resume` on the shell's top |
| `canSeeViewUnderneath()` | top scene's `oled_transparent(Main)` |
| `exitUI()` | `Shell::pop()`; returns `NOT_DEALT_WITH` when the shell's stack is empty so `closeUI` pops the guest |

LEDs: the guest's `derive_leds` writes through an `indicator_leds` `LedSink`; the guest
owns an LED only while one of its scenes declares it, so legacy and guest never fight.
Popups raised by the model while the guest is up still go through `display->` until
phase 5; the guest's `Notices` service is idle in phase 4.

First tenant: `MenuScene`, entered from every place `soundEditor` is entered today
(`openUI(&soundEditor)` → `openUI(&shell_host_ui)` + `push(Menu{node})`).

### 2.2 Phase 5 — shell as host

The scheduler task is `Shell::tick`. `deluge.cpp`'s `dispatchInputEvent`,
`doAnyPendingUIRendering`, `uiTimerManager.routine` and the UI half of
`readButtonsAndPads` are removed. Every remaining legacy `UI` runs through
**`LegacySceneAdapter : Scene`** — one class, one instance per legacy UI
(`SceneId::Legacy{UIType}`):

| Scene hook | Adapter behaviour |
|---|---|
| `handle(UiEvent)` | `padAction` / `buttonAction(b, on, /*inCardRoutine=*/false)` / `*EncoderAction`; a `REMIND_ME_OUTSIDE_CARD_ROUTINE` return re-queues the event (the shell already deferred while `sd_busy()`, so this is rare) |
| `render(Frames&)` | `renderOLED(canvas)`; `renderMainPads/renderSidebar(rows, image, mask)` into the shell's surfaces (rows from the dirty mask) |
| `leds(LedFrame&)` | read-only bridge of `indicator_leds`' shadow state |
| `on_enter` / `on_resume` / `on_exit` | `opened()` / `focusRegained()` / nothing |
| `is_grid_overlay()` | `!renderMainPads()` (today's opacity query) |
| `oled_transparent(Main)` | `canSeeViewUnderneath()` |
| `greyout_mask()` | `getGreyoutColsAndRows()` |
| `tick(dt)` | drives the adapter's `Timer` for `UI_SPECIFIC` → `timerCallback()` |

`openUI/closeUI/changeRootUI/changeUISideways/getCurrentUI/getRootUI` become shims over
the stack ops (`getRootUI()` returns the root adapter's `UI*` or `nullptr` for a new
scene), so unmigrated UIs that navigate keep working. `uiTimerManager` survives as a shim
inside the adapter until the last legacy UI goes. `View view`'s services are replaced by
the shell's in this phase; legacy UIs call the shell's `ModKnobs`/`GlobalControls` through
`view.` façade methods.

## 3. Shared framebuffers

Formats are byte-identical, so aliasing is free and copy-less:

- Phase 4: the guest's `Main` surface *is* `OLED::main`'s storage (`bytes()`); the grid
  surface's rows alias `PadLEDs::image` via `PadGrid::row(y)`; the guest calls
  `OLED::markChanged()` and `sendOutMainPadColours()` instead of its own flush.
- Phase 5: `OLED::main` and `PadLEDs::image` become façade names for the shell's surfaces;
  `sendMainImage`, `sortLedsForCol`, `copyBackgroundAroundForeground` and the three static
  canvases are deleted; the shell flushes.

`PadLEDs::occupancyMask` (used by legacy sidebar rendering) is kept as an adapter-private
buffer until phase 7.

## 4. Façades, codemods, removal dates

- Every façade carries `// FACADE (remove after UI phase N): forwards to …` and a row in
  `docs/dev/ui_migration_facades.md` with its removal phase and the ratchet counter that
  gates it.
- Mechanical rewrites ship as scripts under `scripts/codemods/` (libclang-based where
  semantics matter, regex where safe), each idempotent and documented with before/after:
  `display->displayPopup(...)` → `notify::notice(...)`; `uiNeedsRendering(x, rows)` →
  `notify::raise(...)`; `Buttons::isButtonPressed(B)` → `ctx.held.has(ButtonId::B)`;
  `OLED::main.drawX(...)` → gfx names; `indicator_leds::setLedState(L, on)` → `leds.set(...)`
  in `leds()`. Contributors rerun the codemod on their branch instead of hand-resolving.

## 5. Ratchet

`scripts/check_platform_boundary.py` generalises to `scripts/check_layer_rules.py` with the
same shrink-only discipline, the same pre-commit hook and CI step:

- Rules are rows `(from_glob, forbidden_include_prefix | forbidden_symbol_regex, spec)`.
- The committed baseline `scripts/layer_rules_baseline.txt` holds **counts per (file, rule)**;
  `--update` rewrites it; a PR that increases any count fails with the rule's spec link.
- Rows from day one: `src/gfx/** → anything outside gfx` (already); `src/ui/** →
  model|libdeluge|gui|hid|l10n`; `src/notify → *`; `src/scenes → hid|gui/ui/ui.h`;
  `model|playback|processing|storage|io|modulation → gui|hid` (the 652 of spec C);
  `src/scenes → currentSong` (counted for the later model work); `LegacySceneAdapter`
  instances; `FACADE` includes; `uiTimerManager.` uses.

## 6. Branch and PR strategy

- Everything lands on `next` as small PRs — one extraction, one scene, one codemod, one
  table each. No long-lived `ui-rewrite` branch.
- `next` merges `main` at each `main` release tag; UI conflicts are resolved by rerunning
  the codemods.
- `docs/dev/ui_contributing.md` (one page) tells contributors where UI code goes now: a new
  feature goes in the scene if one exists; a legacy UI is frozen except bug fixes once its
  scene has shipped; menus are `Node` literals. The PR template links it and the ratchet's
  failure message links the relevant spec.

## 7. Deletion schedule

| After phase | Delete |
|---|---|
| 2 (gfx) | nothing — façades only |
| 3 (toolkit) | nothing |
| 4 (guest + `MenuScene`) | `gui/menu_item/**`, `gui/ui/menus.*`, `gui/ui/sound_editor.*`, `gui/menu_item/generate/**` — **but not on this row's own authority; see §10** |
| 5 (host) | `gui/ui/ui.cpp` internals, `hid/buttons.cpp` routing, `hid/matrix/`, `hid/encoder_input.cpp`, `View view`, `OLED::` compositing and static canvases, `indicator_leds` command API, `hid/led/pad_leds.cpp` flush/animations, `currentUIMode` |
| 6 (per scene) | that scene's legacy UI, its adapter instance, its façade rows |
| 7 | `LegacySceneAdapter`, `ShellHostUI`, `uiTimerManager` shim, all façades, `hid/display/`, `hid/led/`, `gui/` |

## 8. Risks and budgets

- **Behaviour drift**: the parity scripts (spec G §6) gate every scene; an intentional
  behaviour change is a separate PR that re-records goldens with the diff reviewed.
- **Flash/RAM**: link-map diff in CI (`./dbt sizediff`) with budgets — shell + toolkit must
  not exceed the code they replace at phase 5; ~40 static scene instances with POD state
  must fit the SDRAM `.bss` vacated by `menus.cpp`'s 411 objects. Measured at the end of
  phases 3, 4 and 5.
- **On-device validation**: a per-phase checklist (boot, menu, popup, song load, record,
  each root scene, screensaver, MIDI learn) on real hardware before the phase's last PR —
  goldens cover pixels, not SPI/PIC timing.
- **Two flush paths in phase 4**: the guest flushes through legacy calls; a scene that
  requests a repaint while a legacy popup is up must not clobber it — the guest checks
  `OLED::isPopupPresent()` and defers, mirroring what legacy UIs do.
- **Community**: frozen-legacy rule + codemods + ratchet messages keep in-flight PRs
  mergeable; `ui_contributing.md` is the contract.

## 9. Open questions

1. Whether `ShellHostUI` (phase 4) is worth its adapter code given phase 5 follows
   directly — it is, if `MenuScene` ships before the host inversion is ready; if the
   schedule collapses them, skip 2.1 and go straight to 2.2 with `MenuScene` as the first
   native scene.
2. `getRootUI()` returning `nullptr` for a new root scene: the 69 external callers are
   being removed by spec C's burn-down; any that remain at phase 5 need the null check they
   should already have.

## 10. Amendment: what the phase-4 deletion actually waits on (2026-09-02, closing spec E3)

§7's phase-4 row has always been the schedule's most-quoted line and its least precise. It names
what goes; it never named the precondition. Two corrections, in order.

### 10.1 The precondition E2 stated, and the fact that it is now met

The E2 plan corrected the precondition to: **`validate_tree` reports zero
`Issue::Kind::PlaceholderCustom` over `kAllRoots` with the allowance off.** (That correction was
written into the E2 plan's step list but never landed in this spec — it is being written down here,
one plan late, which is itself the argument for putting preconditions in the spec rather than in a
plan step.)

**As of E3 task 7, that precondition is met.** `kPendingCustomNodes` is 0, no node table binds
`&kPending`, the `PlaceholderCustom` arm is unconditional, and
`ValidateOptions::allow_placeholder_custom` — the option that could have hidden a regression here —
is deleted along with its eleven call sites. The ported menu tree is complete: 477 nodes, 55 custom
nodes behind 55 distinct real items, zero placeholders.

### 10.2 What still blocks the deletion — and it is not the menu tree

**Nothing from the menu tree remains.** As far as spec E knows, the only blocker is **spec F's own
wiring**, which is this document. Concretely, and each of these is a thing that does not exist today
rather than a thing that is half-done:

1. **Nothing links `deluge_scenes`.** `src/CMakeLists.txt` builds the library; its only consumer is
   its own spec executable. Until an application target links it, deleting the legacy tree deletes
   the only menu the firmware has.
2. **No entry point.** Nothing routes buttons, pads, encoders or MIDI into the scene stack — §2.1's
   `ShellHostUI` or §2.2's host inversion is what supplies it. `openUI(&soundEditor)` has no
   replacement call site yet.
3. **No `MenuAction` dispatcher.** Five ported items request root-UI behaviour
   (`openUI(&performanceView)`, `setClipLengthEqualToSampleLength()`, the MIDI-learn overlay, the two
   song verbs) through `MenuScene::take_action()`, which nothing consumes. That mailbox is a one-slot
   silent overwrite, so the dispatcher must poll every tick.
4. **No `take_choice()` dispatcher.** `BrowserScene`'s deferring descriptor parks a chosen file and
   closes; performing `SampleBrowser::claimCurrentFile` and legacy's `maybeGetParentMenu` re-rooting
   is the wiring layer's.
5. **Two boot calls.** `menu::set_shortcuts_version()` (flash byte 60 — the one menu setting whose
   value does not live in a global the settings loader already populates) and
   `scenes::assigned_node_ids()`.
6. **`perform_editor`'s four sync calls** into `menu::performance_editing_mode()`, because a
   `PerformanceView` class member offers no bare-`extern` escape.

### 10.3 Two consequences of the deletion that are not preconditions but must be planned for

* **Deleting `gui/ui/sound_editor.*` and `gui/ui/menus.*` deletes the oracle.** Sixteen files under
  `sim/scenes_spec/` drive the *legacy* menu objects side by side with the port — that comparison is
  this stack's central safety claim, and it is the only reason the port is believed faithful. The
  deletion PR must decide what replaces it: goldens alone are weaker, so the honest options are to
  delete the legacy tree *after* a device-validated release rather than at the moment the port
  compiles, or to keep `menus.cpp`'s object definitions alive behind a spec-only target for one more
  phase. This is a schedule question, not a technical blocker, and it is the one §7's row hides.
* **`soundEditor` has ~30 namers outside `gui/menu_item/`.** Views, browsers, the keyboard, MIDI
  follow, `flash_storage`, `action_logger` and `source.cpp` all reference it. Most are
  `openUI(&soundEditor)` or a `getCurrentMenuItem()` read and become shims or scene pushes, but they
  are the deletion's real edit surface and none of them is a menu item. `gui/ui/menus.*` also holds
  the `MenuItem` globals several of those files reach directly.

### 10.4 What spec E leaves that is *not* a phase-4 blocker

Recorded so a reader of §7 does not mistake E4's scope for a precondition. **E4 is the load/save
browser family** — `gui/ui/load/load_song_ui`, `load_instrument_preset_ui`, `load_pattern_ui` and all
of `gui/ui/save/` (songs, presets, patterns, kit rows). E3 ported the two browsers the *menu tree*
reaches and stopped there deliberately: the other seven are opened **from root UIs rather than from
menu items**, so the menu tree's legacy-vs-port oracle cannot exercise them, and they carry
`SlotBrowser`'s brand-new-name axis (`mayDefaultToBrandNewNameOnEntry`, `getUnusedSlot`) that
`BrowserScene` has none of. Those files are on §7's **phase 6** row (per-scene), not phase 4.

Also carried, all of them recorded with pointers in `src/scenes/README.md`'s "E4 roadmap": the
`FileCatalogue` claim covering a listing rather than a browsing session; mod-matrix pad blinking;
`model/clip/audio_clip.cpp`'s surviving `gui/waveform/waveform_renderer.h` include (a `model/` class
that renders — `SessionScene`'s to close); `MenuScene::ascend()`'s silent no-op at depth 0 where
legacy's `goUpOneLevel` calls `exitCompletely()`; a `CustomItem::title` hook for the three pages that
want a runtime title; and three `IntegerWithOff` nodes that draw "0" where legacy draws "OFF".

### 10.5 Five defects in shipping firmware, found by the port and not fixed by it

They belong in a release note, not only in a spec, and are written up in full in
`src/scenes/README.md`'s "Defects this port found in shipping firmware" with a `@warning` at each
site: a **use-after-free** in `AudioRecorder::finishRecording` (freed pointer still advertised to a
reader on the preemptive audio interrupt); `Browser`'s and `LoadInstrumentPresetUI`'s
**default-directory fallbacks are dead** (they test `FOLDER_DOESNT_EXIST` where the read path
returns `FILE_NOT_FOUND`); and **the first gold-knob turn after boot is always swallowed**, which
also means every committed UI-script golden depicts an arm the device never presents first.

E4 task 3 added two more, both in `View`'s mod-knob half and both found by porting it:
**`View::modButtonAction` dereferences `ModControllable::getModKnobMode()` unguarded** at
`gui/views/view.cpp:1517` and `:1532`, where `View::getModKnobMode` (`:1680-1689`) null-checks the
same pointer and the base implementation genuinely returns null
(`model/mod_controllable/mod_controllable.cpp:47-49`); and **`View::setKnobIndicatorLevel` reads an
uninitialised `int32_t knobPos`** (declared `:1410`, consumed `:1468`) on the one of its four paths
that assigns nothing. Neither is annotated in `view.cpp` itself — the same reason the browser row
gives, one order of magnitude worse: this stack cites `view.cpp` by line more than a hundred times.

### 10.6 What the oracle actually covers for the three new scenes (2026-09-04, closing E3's final review)

§10.3's first bullet says deleting the legacy tree deletes the oracle. **Read the deletion schedule
against this number, not against the headline "the port matches legacy, proven by specs that run
both".** For the *menu tree* that headline is close to true — sixteen specs drive the legacy
`MenuItem` objects. For the three genuinely new UIs — `BrowserScene`, `MarkerScene`,
`RecorderScene` — it was locally false until E3's final review, and even now the honest count is
smaller than the file count suggests.

**Covered** means: the spec drives the legacy entry point from *its own* inputs, on a model or
catalogue reset to what the port started from, and compares the two answers. A comparison whose
reference is computed from the port's own state proves nothing and is not counted; neither is a
comparison against a transcription of legacy's body held in the spec file. Both existed on this
branch and both are gone (the review's B1 and B2).

**26 legacy entry points, across 27 comparison sites.**

| scene | entry points driven from their own sentinel | comparison sites | spec |
|---|---|---|---|
| Browser | 14 | 6 | `sim/scenes_spec/browser_scene_spec.cpp` |
| Marker | 11 | 18 | `sim/scenes_spec/marker_scene_spec.cpp` |
| Recorder | 1 | 3 | `sim/scenes_spec/recorder_scene_spec.cpp` |

**Browser** (`gui/ui/browser/browser.cpp` unless stated). `beginListing` (`:97`) and
`runPendingListing` (`:126`); `openListingImpl` (`:349`); `arrivedInNewFolder` (`:367`);
`readFileItemsFromFolderAndMemory` (`:280`); `selectEncoderAction` (`:549`), both its plain arm and
its `newFileIndex < 0` wrap; `finishSelectEncoderAction` (`:702`); `clampFileSelectionAndScroll`
(`:1009`); `setEnteredTextFromCurrentFilename` (`:1316`); `predictExtendedText` (`:782`);
`Browser::padAction` (`:1186`) and `QwertyUI::padAction` (`gui/ui/qwerty_ui.cpp:202`);
`goIntoFolder`/`goIntoFolderImpl` (`:1334`/`:1339`); `goUpOneDirectoryLevel`/`Impl` (`:1359`/`:1379`);
`renderOLED` (`:925`), compared over the **full** 128x48 panel including the title rows. Each site
compares `fileIndexSelected`, `scrollPosVertical`, `enteredText`, `enteredTextEditPos`,
`scrollPosHorizontal` and the panel.

**Marker** (`gui/ui/sample_marker_editor.cpp`). `selectEncoderAction` (`:268`); `padAction` (`:339`)
main-column arms — move, select-and-hold, loop-start add, loop-start remove, loop-end remove,
loop-lock toggle; `writeValue` (`:148`); `getColsOnScreen` (`:236`) with its four column helpers;
`horizontalEncoderAction` (`:687`), zoom and scroll; `renderMainPads` (`:1362`) and, through it, the
private `renderColumn` (`:1268`) and `renderMarkerInCol` (`:1058`) over four marker layouts and every
blink phase; `renderOLED` (`:1098`); `reverseRemap` (`:83`); `loopLock`/`loopUnlock`;
`WaveformNav::opened` as `SampleMarkerEditor::opened` calls it (`:126`).

**Recorder** (`gui/ui/audio_recorder.cpp`). `renderOLED` (`:116`) and nothing else.

**Pinned only to literals, with the reason each one is not drivable.** These are what a deletion
loses outright.

* Browser's cull-and-reload arms — `reloadImpl` (`:735`) and `selectEncoderAction`'s four reload
  dispatches (`:637`, `:648`, `:665`, `:676`). They need `numFileItemsDeletedAtStart`/`AtEnd`
  non-zero, i.e. a folder past `FILE_ITEMS_MAX_NUM_ELEMENTS`; no fixture builds one. **This is the
  largest single gap** — it is the part of the browser most likely to be wrong and least likely to
  be noticed.
* `SampleBrowser::opened`'s configuration (`gui/ui/browser/sample_browser.cpp:96-172`) and
  `SampleBrowser::enterKeyPress`'s folder arm (`:333-360`) are **transcribed** into the spec's
  `SpecBrowser`, seven flags and one branch, each cited. `SampleBrowser` is `final` and `opened()`
  dereferences `soundEditor.getCurrentAudioFileHolder()` and drives `actionLogger`,
  `favouritesManager` and `instrumentClipView`, so it cannot run in a spec process. Everything past
  the configuration — the listing, the search, the clamp, the prediction — is `Browser`'s own code,
  executed.
* The whole of `LoadMidiDeviceDefinitionUI`. `opened()` (`:45-47`) tests
  `getRootUI()->toClipMinder()`, and `getRootUI()` indexes an empty UI stack in a spec process. The
  MIDI browser is pinned by its descriptor table and its `start_at` against cited lines only.
* `AudioRecorder`'s entire behaviour: `opened` (`:63`), `process` (`:194`), `buttonAction` (`:280`),
  `slowRoutine` (`:168`), `finishRecording` (`:260`), `updateStatus`, `padAction`,
  `getGreyoutColsAndRows` (`:58`). `process()` is an unbounded blocking loop that pumps
  `AudioEngine::routineWithClusterLoading`, `deluge_display_service` and `readButtonsAndPads`;
  `opened()` starts a real `SampleRecorder` on the audio engine through `setupRecordingToFile` and
  drives `soundEditor`/`PadLEDs`/`indicator_leds`/`instrumentClipView`; `buttonAction`'s release arm
  calls `getRootUI()->buttonAction` on a null root. **Of the three new scenes the recorder is the
  one the oracle barely touches.**
* `SampleMarkerEditor::timerCallback` (`:771`), which renders straight into `PadLEDs::image`. The
  scene's blink is compared against `renderMainPads` instead, with the one place the two legacy
  render paths disagree (phase 2) asserted in both directions.
* A second consecutive zoom detent. A successful `WaveformBasicNavigator::zoom` leaves
  `UI_MODE_HORIZONTAL_ZOOM` set until the `PadLEDs` transition completes, and nothing completes it
  in a spec process, so legacy refuses the next detent where the port takes it. One detent is
  compared; the mode is asserted.

**No legacy counterpart at all** (not a gap, and not counted either way): the browser's favourites
and banks rows and `setFileByFullPath` (`:230`); `createFolder`, `getUnusedSlot`,
`currentFileDeleted`, `goIntoDeleteFileContextMenu`; `SampleBrowser`'s sample preview
(`currentFileChanged`/`folderContentsReady`) and `claimCurrentFile` (`:398`), which the port defers;
`SampleMarkerEditor::renderSidebar`, `verticalEncoderAction`, and `padAction`'s audition-column and
mute-column arms.

**What this means for §7 and §10.3.** The menu tree can be deleted against its own oracle. The three
new scenes cannot be treated the same way: browser reload-under-cull and the audio recorder are
carried by literals and by citations, not by a comparison, and no amount of golden recording
strengthens that — a golden pins the port to itself. Either device-validate those two areas before
the legacy files go, or keep `browser.cpp`, `sample_marker_editor.cpp` and `audio_recorder.cpp`
alive behind a spec-only target for one more phase, which is the same choice §10.3 offers for
`menus.cpp` and should be made once for both.
