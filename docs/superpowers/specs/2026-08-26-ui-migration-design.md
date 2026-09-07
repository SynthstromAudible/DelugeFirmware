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
5. **Three boot calls.** `menu::set_shortcuts_version()` (flash byte 60 — the one menu setting whose
   value does not live in a global the settings loader already populates),
   `scenes::assigned_node_ids()`, and — added by E4 task 7 — a value for
   `ShellConfig::screensaver_idle_ms`, which is
   `screensaverMode == ScreensaverMode::OFF ? 0 : screensaverTimeoutMinutes * 60000`
   (`hid/display/screensaver.cpp:79-81`, `:92-94`) and must be re-supplied on every settings change,
   since `Screensaver::settingsChanged()` has no port. Until it is, `ShellConfig` defaults it to 0
   and **the screensaver can never activate**, however the two settings are set.
6. **`perform_editor`'s four sync calls** into `menu::performance_editing_mode()`, because a
   `PerformanceView` class member offers no bare-`extern` escape.
7. **No application installs a service** — added by E4 task 13, the first thing in the tree that
   ever tried to hold all three services and a `Shell` at once. `services::ModKnobs`,
   `services::GlobalControls` and `services::MidiLearn` each take the `Shell`'s own `SceneStack&`
   (two of them its `InputLowering&`) and each *uses* it, so a real service cannot be constructed
   before the shell it serves; and `Services` used to be taken **by value into the `Shell`'s
   constructor**, with `service_list_` built once there, so it could not be installed after it
   either. An application that built a `Shell` could only pass `Services{}`, and **no gold-knob
   turn, transport button or MIDI-learn press reached anything outside the scene stack.** Unlike 5,
   this is not a setting that degrades — it is a whole routing tier that is unreachable.

   **The mechanism half is closed.** F1 task 2 added `Shell::set_services` (`src/shell/shell.h`),
   which sets `services_`, re-seats `Router`'s own by-value copy and rebuilds the deduplicated
   `service_list_` — well defined on a re-install, and covered by `tests/spec/shell/services_spec.cpp`
   with all four one-line mutations killed. `sim/scenes_spec/scene_scripts_spec.cpp`'s forwarding
   `LateService` shim is gone: it constructs the three in `after_construct` and calls the setter,
   with its 34 goldens unmoved. Shell spec §15.9 has the account. **What remains open here is boot
   calling it** (task 8), which is why this item is not yet struck. Recorded in code at
   `src/scenes/registry.h`'s boot-wiring list and `src/scenes/README.md`.
8. **No producer for three of `MidiLearn`'s four entry points.** `note_on_received`, `cc_received`
   and `thing_pressed` have zero callers in `src/` — there is no MIDI `UiEvent` kind for the router
   to carry, and the scenes that would call `thing_pressed` from `learnable_at(pad)`
   (`SessionScene`, `NoteClipScene`, `ArrangerScene`) are E5's and E6's. The four button arms
   *are* live through `Router`.
9. **Installing the services re-records part of the level-2 baseline** — added by E4's final review,
   and it belongs with 7 rather than after it: doing the wiring in 7 is what makes this fall due.
   All 24 `expect_script_goldens` calls in `sim/scenes_spec/menu_scripts_spec.cpp` pass
   `Services{}`, and twelve of those scripts drive **19 gold-knob turns** between them. What decides
   whether a given step's golden moves is the focused **page**, not the router's chain, and the
   split is measured:
   * A turn the page's own widget claims never reaches `Router::run_chain` at all —
     `Router::route_post_render` drops it on `ctx().claims` first — so installing the services
     changes nothing. `menu_int`'s `after_gold_turn` is that case, and
     `sim/scenes_spec/scene_scripts_spec.cpp`'s `menu_gold_turn` script records the same node with
     the real three services installed to prove it.
   * A turn the page does not claim is dropped today and will, with a real `services::ModKnobs`,
     write the mod param and push a `ParamPopupScene` over the menu — a second `scene` line in
     `.state` and a value strip in the `.pbm`. `menu_decimal`'s first `GoldUpper` turn is the worked
     example.

   Each of the 19 has to be classified and looked at as it is re-recorded; that is a step in the
   wiring work, not a separate plan. E4 deliberately did not do it, because the same commit range
   also reordered the router's chain (§15.4/§15.5 of the shell spec) and a golden diff with two
   causes cannot be reviewed for either. Recorded in code at `src/scenes/README.md`'s carry table,
   `tests/ui-scripts/shell/menu_int.txt` and `menu_scripts_spec.cpp`'s own header.

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

Recorded so a reader of §7 does not mistake E4's scope for a precondition.

> **Correction, 2026-09-07, closing E4.** This section was written expecting E4 to be the load/save
> browser family. **It was not.** E4 (`2026-09-06-ui-scenes-services-and-roots-4.md`) built the
> three model-aware shell services, the three missing overlay scenes, the shared `TimelineState` and
> the first two application scenes (`SlicerScene`, `AudioClipScene`). The reordering was structural
> rather than opportunistic: `GlobalControls`' and `MidiLearn`'s `View`-arm groups are gated on
> `stack_.input_top().is_root()`, so until a root scene existed those arms were unreachable and
> untestable, and `AudioClipScene` is what made them live. Everything the paragraph below says about
> the load/save family still holds — it is **E5's or E6's** rather than E4's. E4's own coverage
> number is §10.7.

The family in question is `gui/ui/load/load_song_ui`, `load_instrument_preset_ui`, `load_pattern_ui` and all
of `gui/ui/save/` (songs, presets, patterns, kit rows). E3 ported the two browsers the *menu tree*
reaches and stopped there deliberately: the other seven are opened **from root UIs rather than from
menu items**, so the menu tree's legacy-vs-port oracle cannot exercise them, and they carry
`SlotBrowser`'s brand-new-name axis (`mayDefaultToBrandNewNameOnEntry`, `getUnusedSlot`) that
`BrowserScene` has none of. Those files are on §7's **phase 6** row (per-scene), not phase 4.

Also carried, all of them recorded with pointers in `src/scenes/README.md`'s roadmap section (which
the same correction retitles): the `FileCatalogue` claim covering a listing rather than a browsing
session; mod-matrix pad blinking; `model/clip/audio_clip.cpp`'s surviving
`gui/waveform/waveform_renderer.h` include (a `model/` class that renders — `SessionScene`'s to
close); `MenuScene::ascend()`'s silent no-op at depth 0 where legacy's `goUpOneLevel` calls
`exitCompletely()`; a `CustomItem::title` hook for the three pages that want a runtime title; and
three `IntegerWithOff` nodes that draw "0" where legacy draws "OFF".

**E4 adds four rows to that list**, none of them a phase-4 blocker either:

* **`slicing::SliceOps` has no implementation.** Six virtuals with no-op defaults, wired by nobody —
  the whole audio/model/card half of the slicer. `Slicer` reaches that work through four `gui/`
  globals the ratchet forbids (`sampleBrowser`, `soundEditor`, `instrumentClipView`, `PadLEDs`), so
  the port names the calls at a boundary and keeps the state, the guards, the coalescer, the
  dispatch and the gate. Until a wiring layer implements it the slicer selects, deletes, renders and
  snapshots correctly and **makes no sound and no kit**. `SliceOps::play_position` is the third
  consumer of the voice walk no spec can drive, so the fixture that unblocks it — a `Song` with a
  booted `AudioEngine` and one sounding voice — unblocks all three.
* **`ClipView`'s `Y_ENC` shift arm is a silent no-op for a real gesture, and must not survive E5.**
  `AudioClipScene` *claims* the `Y_ENC` (and `X_ENC`-held + CLIP) chord exactly as legacy does
  (`gui/views/clip_view.cpp:249`) and then does nothing: two of the three arguments
  `Clip::shiftHorizontally` takes are decided by `getCurrentUI() == &automationView` (`:203`,
  `:206`) — a question with no answer while no `AutomationScene` exists — and the undo consequence
  `alloc_sdram`s (`:240`). On the device this gesture shifts the clip's contents. **Whoever writes
  `AutomationScene` ports this arm in the same change.** Recorded at the arm itself, in
  `src/scenes/README.md`'s carry table, and here.
* **`AudioClipScene::clip_resynced` is written from a citation and driven by nothing.** A `@warning`
  on its declaration says so.
* **Six doc-comment tables mangled by clang-format's comment reflow** — `menu/nodes/midi_device.h`,
  `menu/nodes/patch_cables.h`, `recorder/recorder_scene.h`, `shell/scene.h`, `shell/scene_stack.h`,
  `shell/shell.h`. Rewriting one as a bulleted list fixes it, and the citations in it have to be
  re-verified line by line while doing so (E4 tasks 9 and 14 each fixed one this way).

### 10.5 Ten defects in shipping firmware, found by the port and not fixed by it

They belong in a release note, not only in a spec, and are written up in full in
`src/scenes/README.md`'s "Defects this port found in shipping firmware" with a `@warning` at each
site: a **use-after-free** in `AudioRecorder::finishRecording` (freed pointer still advertised to a
reader on the preemptive audio interrupt); `Browser`'s and `LoadInstrumentPresetUI`'s
**default-directory fallbacks are dead** (they test `FOLDER_DOESNT_EXIST` where the read path
returns `FILE_NOT_FOUND`); and **the first gold-knob turn after boot is always swallowed**, which
also means every committed UI-script golden depicts an arm the device never presents first.

E4 task 3 added two more, both in `View`'s mod-knob half and both found by porting it:
**`View::modButtonAction` dereferences `ModControllable::getModKnobMode()` unguarded** at
`gui/views/view.cpp:1517` (a read) and `:1532` (a write, reached on a press in a clip-minder view
where `:1517` is not), both inside the `:1510`/`:1511`/`:1512` guards, where `View::getModKnobMode`
(`:1680-1689`) null-checks the same pointer and the base implementation genuinely returns null
(`model/mod_controllable/mod_controllable.cpp:47-49`); and **`View::setKnobIndicatorLevel` reads an
uninitialised `int32_t knobPos`** (declared `:1410`, consumed `:1468`) on the one of its four paths
that assigns nothing. Neither is annotated in `view.cpp` itself — the same reason the browser row
gives, one order of magnitude worse: this stack cites `view.cpp` by line more than a hundred times.

E4 task 8 added a sixth, found by porting the clip views' scroll and zoom:
**`ClipView::getTickSquare` compares a scroll against the zoom.** The arm that stops following an
auto-extending linear recording reads `if (newXScroll != currentSong->xZoom[NAVIGATION_CLIP])`
(`gui/views/clip_view.cpp:382`), where `newXScroll` came from
`xScrollBeforeFollowingAutoExtendingLinearRecording` (`:379`), which the follow arm filled from
`currentSong->xScroll[NAVIGATION_CLIP]` (`:366`). So the restore is skipped whenever the remembered
scroll happens to equal the current zoom — leaving the view where the follow pushed it rather than
where the user was — and performed in the one case the guard exists to skip. Reached by recording a
clip longer than one screen. Established by reading and by driving legacy's own `getTickSquare`
from both sides (`sim/scenes_spec/timeline_spec.cpp`); the port reproduces it rather than fixing
it, for the reason every row here gives, and it is annotated at
`src/scenes/timeline/timeline.h`'s `tick_square` rather than in `clip_view.cpp`.

E4 tasks 9, 10 and 12 added the last four, three of them in the slicer and one in `ClipView`:

* **`Slicer::renderMainPads` never checks that it was given an image**, although `UI`'s own header
  requires every override to (*"These \*must\* check whether image has been supplied"*,
  `gui/ui/ui.h:141-142`) and `SampleMarkerEditor::renderMainPads` does
  (`gui/ui/sample_marker_editor.cpp:1362-1366`). `closeUI` quizzes every UI down to the one being
  closed with the **no-argument** form (`gui/ui/ui.cpp:184-192`), so `image` is null, and closing
  the slicer takes that path on every exit. REGION mode reaches `WaveformRenderer::renderFullScreen`'s
  memset; MANUAL mode writes `image[yy + 4][xx]` directly. **Memory corruption**, established by
  reading the enclosing braces rather than reproduced — whether address 0 faults on an RZ/A1 depends
  on the boot-time memory map, which is why it has survived.
* **`Slicer` declares 432 bytes of stack it never touches, twice** (`gui/ui/slicer.cpp:107`, `:376`).
  Cosmetic; recorded because a reader comparing the three render arms will otherwise assume the two
  dead arrays matter.
* **`Slicer::selectEncoderAction`'s slice-count wrap only fires on an exact hit** — equality, not a
  range (`gui/ui/slicer.cpp:249-257`) — while `offset` is whole detents, so a fast turn steps over
  both ends and a single -20 turn from the opening 16 lands on **-4**, which the page then displays.
  Reachable with an ordinary flick of the select encoder. The port reproduces it exactly and asserts
  both halves against legacy's own `selectEncoderAction`.
* **`ClipView::lengthenClip` and `shortenClip` dereference `actionLogger.firstAction[BEFORE]`
  without a null check** — `gui/views/clip_view.cpp:98` and `:129`. The nearest null-guarded read of
  that pointer is `lengthenClip`'s `undoing` test **eighteen lines up, in a different `if` whose
  result is not carried down**, and `shortenClip` has no earlier test at all, which is what makes it
  easy to miss rather than an obvious oversight. It survives on the device because an `Action`
  almost always exists by then; the window is a fresh boot (or an `actionLogger.deleteAllLogs()`,
  which `SoundEditor::setup` does on every menu open) followed immediately by a length edit.
  **Established by driving it**: `sim/scenes_spec/audio_clip_scene_spec.cpp` exited 139 rather than
  failing, which is why its `Rig::reseed` seeds one inert `ActionType::MISC` action. The port guards
  it and is deliberately **not** a faithful reproduction — a port cannot faithfully reproduce a
  segfault.

Four of the ten are in `gui/views/view.cpp` and `gui/views/clip_view.cpp` and **none of the four is
annotated in the legacy file**: this stack cites those two by line more than 130 times, and
inserting a comment would shift every citation. Every port-side `@warning` is complete, and
`src/scenes/README.md`'s "Defects this port found in shipping firmware" is the full table.

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

### 10.7 What the oracle actually covers for E4's nine subjects (2026-09-07, closing E4)

The same question §10.6 asks, for the three services, the three overlay scenes, the shared timeline
state and the two application scenes E4 built. **Read the deletion schedule against this number**,
which is what §10.3's first bullet says it must be read against.

**Covered** means what §10.6 established: the spec drives the legacy entry point from *its own*
inputs, on a model or fixture reset to what the port started from, and compares the two answers. A
comparison whose reference is computed from the port's own state is not counted; neither is a
comparison against a transcription of legacy's body held in the spec file.

**64 legacy entry points, across 121 comparison sites.**

| subject | entry points driven from their own sentinel | comparison sites | spec (`sim/scenes_spec/`) |
|---|---|---|---|
| `ModKnobs` | 11 | 13 | `mod_knobs_spec.cpp` |
| `GlobalControls` | 2 | 7 | `global_controls_spec.cpp` |
| `MidiLearn` | 11 | 15 | `midi_learn_spec.cpp` |
| `ParamPopupScene` | 2 | 3 | `param_popup_spec.cpp` |
| `CountInScene` | 1 | 1 | `count_in_spec.cpp` |
| `ScreensaverScene` | 2 | 2 | `screensaver_scene_spec.cpp` |
| `TimelineState` | 14 | 29 | `timeline_spec.cpp` |
| `SlicerScene` | 7 | 21 | `slicer_scene_spec.cpp` |
| `AudioClipScene` | 14 | 30 | `audio_clip_scene_spec.cpp` |

**How "comparison sites" is counted here, and why it differs from §10.6.** A site is **one example
(`it(...)`) that calls a legacy entry point with spec-chosen inputs and asserts**. Precisely, so it
can be re-derived: walk the spec's lines, tracking the enclosing `it(`/`they(` block; **skip lines
whose first non-space characters are `//`, `///`, `*`, `*/` or `/*`**; and count a block that
contains both a **call** to one of the entry points named below (matched as the identifier followed
by `(`) and an `expect`. A legacy call reached only through a **teardown helper** does not count —
`global_controls_spec.cpp`'s `release_all_legacy_buttons()` calls `Buttons::buttonAction` to release
held buttons so one example's combo cannot leak into the next, which is not the gesture under test;
counting helpers transitively would turn that spec's 7 into 21.

Two places the rule earns its precision, both found by a reviewer re-deriving these numbers:

* **Comment lines must be excluded.** Counting `global_controls_spec.cpp`'s blocks for the bare word
  `buttonAction` *without* the comment skip gives **8**; with it, **7**. The extra block is "the
  root gate: `View`'s arms need a root scene, and the `Buttons` tail never does", which names
  `View::buttonAction` and `Buttons::buttonAction` in its opening comment and **calls neither** — it
  drives the port and asserts against a rule established by reading. 7 is the count.
* **§10.6's own site numbers are a hand count and do not re-derive.** Applying this rule to E3's
  specs gives 6 for the browser and 3 for the recorder — matching §10.6 exactly — and **5** for the
  marker against a recorded **18**. Recorded rather than reconciled: §10.6's entry-point column is
  unaffected, and this plan's own lesson (task 9's unreproducible "8 violations") is that a
  measurement a reader cannot repeat is an argument wearing a measurement's clothes.

The entry-point column is the substantive one either way, and every entry point in it is enumerated
below by name.

**`ModKnobs`** (`gui/views/view.cpp`). `modEncoderAction`; `modButtonAction`;
`displayModEncoderValuePopup`; `calculateKnobPosForModEncoderTurn`;
`convertPatchCableKnobPosToIndicatorLevel`; `setKnobIndicatorLevel`; `isClipContext`, over all five
reachable target shapes; `getModKnobMode`; and legacy's own three target setters —
`setActiveModControllableTimelineCounter`, `setActiveModControllableWithoutTimelineCounter`,
`setModRegion` — which are driven as legacy's side of the sentinel and whose resulting
`activeModControllableModelStack` is compared field by field. The central example runs the two sides
on **two different clips**, each with its own `SoundInstrument` and `ParamManager`, each seeded by
its own explicit write, and reads the resulting `ParamManager` back by identity rather than asking
the port what it did.

**`GlobalControls`** (`hid/buttons.cpp`, `gui/views/view.cpp`). `Buttons::buttonAction`, driven at
15 call sites, and `View::buttonAction` reached through it — a stub **root** UI forwards, because
`UI::buttonAction`'s base returns `NOT_DEALT_WITH` and does not. Two entry points is a small number
for a 24-example spec, and it is the honest one: transport is a **process singleton** (one
`PlaybackHandler`, one `Buttons`, one `AudioEngine::audioSampleTimer`), so no fixture can give
legacy and the port one each. The discipline is applied in the shape available and stated at the
file: each side establishes its own starting state and asserts the reset took; legacy's answer is
captured into a local and the model is then **poisoned** with a value neither transition can produce
(`RecordingMode::ARRANGEMENT`, `false` for a metronome legacy just turned on, `0x5EED` for the BACK
arm's cluster counter); both sides are asserted to have moved.

**`MidiLearn`** (`gui/views/view.cpp`). `startMIDILearn`; `endMIDILearn`; `buttonAction`;
`midiLearnFlash`; `noteOnReceivedForMidiLearn`; `ccReceivedForMIDILearn`; and the five learn-pad
entry points — `sectionMidiLearnPadPressed`, `instrumentMidiLearnPadPressed`,
`clipStatusMidiLearnPadPressed`, `noteRowMuteMidiLearnPadPressed`, `drumMidiLearnPadPressed`. The
`LearnedMIDI` objects are per-fixture where legacy allows and process singletons where it does not
(`midiEngine.globalMIDICommands`); the poison step is what makes the singleton case honest.

**`ParamPopupScene`** (`gui/views/view.cpp`). `displayModEncoderValuePopup` and
`flushPendingModEncoderValuePopup`, compared as **the assembled scene's own pixels** through a real
`deluge::hid::display::OLED` and a real `Shell`, not as the widget in isolation.

**`CountInScene`** (`gui/ui/notify_drain.cpp`'s count-in block, `:92-113`). One entry point: legacy's
own `display->displayPopup(buffer, 0, true, 255, 2)` call at `:100`, with `buffer` formatted from
`playbackHandler.currentVisualCountForCountIn` — the same public sentinel the port reads for itself
through `count_in_visual()`. Neither side is computed from the other's.

> **This one sits on the boundary of the rule above, and is marked so it is not read as precedent.**
> `notify_drain.cpp`'s count-in block never runs: `count_in_spec.cpp`'s `legacy_show_count_in()` is a
> **two-line transcription** of `:98-100` — the `intToString` call and the five `displayPopup`
> arguments — and what it reaches from there is real, unmodified legacy
> (`OLED::displayPopup` → `popupText`, and the panel is compared through the real
> `hid::display::OLED` singleton). So the *drawing* is driven, the *dispatch* is transcribed. It is
> counted because the transcribed part is a call site rather than a body — five literal arguments a
> reader can check against `:100` in one glance, with no branch and no arithmetic — and because
> nothing above `:100` is reachable from a spec (`notify_drain` is driven by the tick tree). A
> future subject that transcribes a *body* to reach legacy is **not** the same thing and does not
> get counted on this precedent; §10.6's exclusion means what it says.

**`ScreensaverScene`** (`hid/display/screensaver.cpp`). `Screensaver::timerEvent` and
`Screensaver::noteActivity`, observed through `isActive()` and `getCanvas()`, with the two 128x48
panels compared **pixel for pixel on every frame** in both animated modes.

> **What that pair can and cannot disagree about.** Since the move, legacy's own `Screensaver`
> includes `ui/oled/rainfall.h` and `ui/oled/starfield.h` (`hid/display/screensaver.h:21-22`) — so
> **both sides draw with the same objects.** The comparison is therefore about *sequencing*: which
> of `scatter()` / `advance()` / `render()` each side runs, in what order, on which tick, and into
> which canvas. It cannot disagree about pixel *content* for a given animation state, because there
> is only one implementation of that left in the tree. That is exactly why the transcription oracles
> exist and why the move needed a second, textual argument — and it is why "pixel for pixel" here
> means "the assembled frame agrees", not "the animation arithmetic was re-derived".

What is *not* counted
here is the move's own proof: `Rainfall` and `Starfield` are compared against
`sim/scenes_spec/support/oracle_{rainfall,starfield}` — a transcription of the pre-move source, and
§10.6's rule excludes transcriptions. It is a strong transcription (produced by a recorded
three-substitution `sed`, reproduced byte-identically by the reviewer against
`git show`, and corroborated by an independent reverse-mapped diff showing only comment-column
realignment), but it is a transcription, and this table counts what the rule counts.

**`TimelineState`** (`gui/views/timeline_view.cpp`, `clip_navigation_timeline_view.cpp`,
`clip_view.cpp`, `audio_clip_view.cpp`). Fourteen functions, driven on **two** concrete views
(`audioClipView` and `instrumentClipView`, 18 view×function pairs): `horizontalEncoderAction`,
`buttonAction`, `zoomToMax`, `scrollRightToEndOfLengthIfNecessary`, `scrollLeftIfTooFarRight`,
`isSquareDefined`, `getSquareFromPos`, `getSquareEndFromPos`, `getPosFromSquare`,
`getLengthExtendAmount`, `getLengthChopAmount`, `displayNumberOfBarsAndBeats`,
`tripletsButtonPressed`, and `ClipView::getTickSquare` through a pointer-to-member (it is
`protected`).

**`SlicerScene`** (`gui/ui/slicer.cpp`). `selectEncoderAction`; `horizontalEncoderAction`;
`verticalEncoderAction`; `buttonAction`; `renderMainPads`; `renderOLED`; `focusRegained`. Several
arms need a slice table with more than one entry and neither side can build one from its own
`padAction` (legacy's adder is behind the undriveable voice walk), so the spec **tells both sides the
same table**, each through its own front door — the port through its own ported adder, legacy
through its own public members — and asserts the two agree before the gesture under test runs.

**`AudioClipScene`** (`gui/views/audio_clip_view.cpp` and its bases). `padAction`; `buttonAction`;
`horizontalEncoderAction`; `verticalEncoderAction`; `selectEncoderAction`; `timerCallback`;
`graphicsRoutine`; `renderMainPads`; `renderSidebar`; `renderOLED`; `getMaxLength`; `getMaxZoom`;
`clipNeedsReRendering`; `setClipLengthEqualToSampleLength`.

**Pinned only to literals or to a counter, with the reason each one is not drivable.** These are
what a deletion loses outright.

* **`AudioRecorder`-shaped: everything behind `audioRecorder` in `GlobalControls`** —
  `beginOutputRecording` (`hid/buttons.cpp:161`, `:182`), `isCurrentlyResampling` (`:195`),
  `endRecordingSoon` (`:196`). Not ported at all, so there is nothing on the port side to compare;
  the *decisions* that reach them are covered through
  `GlobalControls::unported_opens(UnportedOpen)`, a tally **per site**, so an assertion about one
  arm cannot be satisfied by another. `openUI(&saveSongUI)` / `openUI(&loadSongUI)` /
  `context_menu::clearSong` (`view.cpp:261`, `:313`, `:290-293`) have the same shape.
* **`slicing::SliceOps` has no implementation.** Six virtuals with no-op defaults — the whole
  audio/model/card half of the slicer. `Slicer::doSlice` (`gui/ui/slicer.cpp:596-751`) itself is not
  driven: it calls `sampleBrowser.claimAudioFileForInstrument`, reads `soundEditor.currentSource` /
  `currentSound`, calls `instrumentClipView.recalculateColours` and ends in
  `sampleBrowser.exitAndNeverDeleteDrum()` — four `gui/` globals this layer may never name, plus a
  card read. The spec drives a fake and compares everything **around** the commit: the snapshot, the
  gate, the dispatch and the release. This is the largest single gap in E4, and it is the direct
  analogue of E3's `AudioRecorder` gap.
* **The voice walk, which three separate things need.** `Slicer::graphicsRoutine`
  (`slicer.cpp:156-203`), `Slicer::padAction`'s do-slice branch (`:508-534`) and
  `SliceOps::play_position` all need `Sound::voices()` to report a live voice, which needs a running
  `AudioEngine`. `support/song_fixture.h` builds a `Song` by direct construction and never calls
  `AudioEngine::init()`, so the only branch a spec can reach is "no voices, no tick square", which
  asserts nothing. One fixture — a `Song` with a booted `AudioEngine` and one sounding voice —
  unblocks all three.
* **`AudioClipScene::clip_resynced` is written from a citation and driven by nothing**, and says so
  on its declaration.
* **`TimelineState::tick_square`'s auto-scroll arm.** `ClipView::getTickSquare`'s follow arm is
  gated on `getCurrentlyRecordingLinearly()` and `getCurrentUI() == this` — a live recorder and a
  real root UI, neither of which the fixture has. The arms reachable from a non-recording clip are
  compared; the recording ones are not.
* **`ModKnobs`' popup suppression arms** for the performance view and the song browser
  (`view.cpp:869-874`, `:885`) — no `PerformanceScene` exists and the song browser is not among the
  legacy UIs `BrowserScene` carries, so no port-side state can make either fire. **`PopupContext::stuttering`**
  — nothing in the port owns the stutter gesture. **The MIDI-clip CC/expression naming arm**
  (`view.cpp:1122-1146`) — reachable since task 2 widened `show()`, but a bare MIDI fixture's gold
  knobs resolve through `MIDIInstrument::getParamFromModEncoder` to no `autoParam`, so no popup is
  shown at all; covering it needs a fixture that assigns a mod-knob CC.
* **`ModKnobs::notify_param_automation_occurred`'s `DISPLAY_AUTOMATION` consumer** — the timer
  callback is `automationView.displayAutomation()` / `view.displayAutomation()`, both view-side and
  unported. Only the *arming* is compared.
* **`MidiLearn`'s `clearMelodicInstrumentMonoExpressionIfPossible`** (`view.cpp:745-765`),
  **`iterateAndCallSpecificDeviceHook(HOOK_ON_MIDI_LEARN)`** (`:620`) and
  **`FlashStorage::writeSettings()`** (`:485`): the first is unported (it needs
  `model/model_stack.h`, which `scenes-model-stack-outside-context` confines to two files) and is
  counted through `MidiLearn::unported_calls`; the second likewise; the third is skipped on **both**
  sides, because `spec::QuietAudio` holds `AudioEngine::audioRoutineLocked` for the whole process,
  so what is asserted is the `shouldSaveSettingsAfterMidiLearn` decision rather than the write.
* **`GlobalControls`' `THRESHOLD_RECORDING_MODE` popup arm** (`hid/buttons.cpp:190-192`), the
  **`SYNC_SCALING` fill-mode arm's note halves** (`view.cpp:339-353`, which need `soundEditor` and
  `instrumentClipView`), the **dedicated undo/redo buttons** (`view.cpp:423-446`, dead code behind
  `#ifdef undoButtonX`, defined nowhere in the tree), and the **UNDO-vs-REDO choice** at `:414-415`
  (`playbackHandler.slowRoutine()` consumes the pending command inside the same call and a spec
  cannot make `deluge::sync::sd_busy()` true, so both spellings read `NONE` by the time control
  returns — the arm's *body running* is covered, which of the two it chose is not).
* **`CountInScene`'s masked `UI_MODE_RECORD_COUNT_IN` clear** (`notify_drain.cpp:104-111`):
  `currentUIMode` lives in `gui/ui/ui.h`, which this layer may never include, so there is no port to
  compare against at all.
* **Four `ScreensaverScene` behaviours, all of them policy rather than drawing and all of them
  `shell::Screensaver`'s half**: `settingsChanged()` (`hid/display/screensaver.cpp:165-177`), the
  permanent-popup / working-animation / stem-export inhibitors (`:106-118`) and with them the
  1000 ms re-check that exists only to run them, the `ScreensaverMode::OFF` early-outs, and the
  `consumeFrameDirty` blit protocol.
* **`AudioClipScene`'s `UI_MODE_HOLDING_SONG_BUTTON` macro arm of `renderSidebar`** (`:247-249`) and
  **the four `PadLEDs` transition modes** — the port carries no transitions at all, so those early
  returns have no counterpart.
* **`View::setModLedStates` is not driven.** The eight-mode × eight-LED table is asserted by
  identity against a transcription of `view.cpp:1662-1676` read row by row, which is the shape §10.6
  excludes. It is a table rather than a body, and the assertion fails on an undeclared LED id, but
  it is not a comparison against running legacy and is not counted above.

**Anything with no legacy counterpart at all** (not a gap, and not counted either way):
`Slicer::redraw` (`slicer.cpp:100-101`, an empty 7SEG-only hook); **`AudioClipView::renderOLED` at
64 rows** and `CountInScene` at 64 rows, because `OLED_MAIN_HEIGHT_PIXELS` is a compile-time 48 and
there is no 64-row legacy singleton to ask — both are goldens at that geometry, exactly as
`marker_scene_spec`'s and `browser_scene_spec`'s are; and the `Y_ENC` shift arm and
`changeRootUI(&automationView)` arm of `ClipView`, which have no port to compare (§10.4).

**Three of `MidiLearn`'s four entry points have no producer in `src/` at all.**
`note_on_received`, `cc_received` and `thing_pressed` have zero callers outside the spec: there is
no MIDI `UiEvent` kind for the router to carry, and the scenes that would call `thing_pressed` from
`learnable_at(pad)` are E5's and E6's. So its coverage reads as "the service's behaviour is pinned;
three of its four entry points have no producer yet", and §10.2 item 8 owes them.

**What this means for §7 and §10.3.** Two of E4's nine subjects are carried by literals in the way
`AudioRecorder` is: the slicer's **commit path** (`SliceOps`, `doSlice`) and everything the **voice
walk** feeds. Both are unblocked by the same fixture, which is the cheapest single thing anyone
could build to raise this number. Everything else is comparison-covered on the arms a spec process
can reach, and the two services whose gaps are largest — `GlobalControls`' recorder arms and
`MidiLearn`'s absent producers — are gaps in the **wiring**, not in the port: they close when spec F
supplies the callers, not when someone writes more spec. As in §10.6, the honest answer is either
device-validate the slicer's commit before `slicer.cpp` goes, or keep it alive behind a spec-only
target for one more phase — the same choice §10.3 offers for `menus.cpp`, `browser.cpp`,
`sample_marker_editor.cpp` and `audio_recorder.cpp`, and it should be made once for all of them.
