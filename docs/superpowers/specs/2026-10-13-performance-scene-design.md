# `PerformanceScene` — design

**Date:** 2026-10-13
**Status:** approved section by section in brainstorming; awaiting Kate's review of this written spec
**Subject:** porting legacy `PerformanceView` (`gui/views/performance_view.{h,cpp}`, 2,122 + 207
lines, 67 functions) to a root scene in `src/scenes/performance/`, verified in the simulator against
legacy as the oracle. The first of E5's two specs; `KeyboardScene` is the second and is not in scope
here.

---

## 1. Why this exists

Spec E section 7 row 4 maps `gui/views/*_view.*` onto "scenes + views + gesture files". E4 built the
first root scene (`AudioClipScene`) and the three model-aware services, and left row 4's remainder to
E5: `KeyboardScene` and `PerformanceScene`. This spec is the second of those.

Two services already carry arms that have no scene to ask:

* `services::ModKnobs::ViewState::root_is_performance` (`src/scenes/services/mod_knobs.h:306`) feeds
  the mod-button arm at `mod_knobs.cpp:482`, and the popup-suppression arm at legacy
  `gui/views/view.cpp:888-891` is documented as **not ported and counted as uncovered**
  (`mod_knobs.h:506-510`) for exactly one reason: `PerformanceScene` does not exist.
* `services::GlobalControls::ViewState::root_is_performance` (`global_controls.h:283`) gates the
  KEYBOARD guard at `global_controls.cpp:154`, the port of legacy `view.cpp:237` and `:282`.

Under the guest both fields are filled from `getRootUI() == &performanceView`
(`gui/ui/shell_guest_services.cpp:135`, `:155`). This spec makes them answerable from a scene.

## 2. The decisions this spec records

Four choices were made in brainstorming and are binding here:

1. **Two specs, Performance first.** `KeyboardScene` is separate and later.
2. **Definition of done: port plus oracle, like `AudioClipScene`.** The scene is built in
   `deluge_scenes`, driven through a real `Shell` in `sim/scenes_spec`, compared against legacy
   `PerformanceView` running beside it, and covered by level-2 scripts. It is **not reachable on
   device**; that is spec F's firmware wiring.
3. **Pure parse plus async port for the layout file.** The XML parse and serialise are ported as
   pure functions over an abstract `Serializer`/`Deserializer`; I/O goes behind a tick-polled
   request/collect port. **No legacy storage function is edited**, so the async-SD rung ladder in the
   other clone (`feat/rung1-async-load` → `rung2` → `rung3`, seam `deluge::storage::bytes`) stays
   friction-free.
4. **The scene owns its state.** The `AudioClipScene` pattern: its own state, files split by concern,
   couplings turned into port-side hooks. Legacy `PerformanceView` is untouched except for the two
   named edits in §10, and runs beside the scene as the oracle.

## 3. Scope at a glance

**In scope:** the 16 main FX columns (render, press, hold, latch, restore), the OLED view and FX
displays, the button arms that stay inside the view, the select and mod encoders, the two editing
modes reached by shift+KEYBOARD, the layout file (parse, serialise, read, write, change status), undo
of a column press, and the two services' `root_is_performance` arms.

**Out of scope, declined and recorded:** every exit to a non-scene root; opening the menu from the
scene; the menu's own `MenuAction::OpenPerformanceView` route; MIDI-driven display refresh; the dead
layout bank and variant fields.

## 4. Architecture and components

### 4.1 Where it lives

`src/scenes/performance/`, registered as `FirstApp + 10` — the next free application kind
(`shell/scene.h:50` sets `FirstApp = 16`; the table runs to `FirstApp + 9`, `AudioClipScene`, at
`scenes/registry.cpp:88`). `kPerformanceSceneKind` is spelled by the scene's own header, as every
kind since `FirstApp + 1` is.

`Scene::is_root()` returns true (`shell/scene.h:527`). That is the exact reachability rule for
legacy's `View::buttonAction` arms, and it is what makes `services::GlobalControls`' second group
live for this scene.

New sources are listed in `src/scenes/CMakeLists.txt` beside `audio_clip/audio_clip_scene.cpp`
(`:126`); that list is explicit, not globbed. Sim specs **are** globbed
(`sim/scenes_spec/CMakeLists.txt:68`), so the new spec file needs no build edit.

Naming: snake_case for files, functions and variables under `src/scenes` and `src/ui`, per the
house rule. `check_layer_rules.py` forbids the symbol `performanceView` anywhere under
`src/scenes` (it is one of `scenes-no-root-ui`'s named alternatives, `check_layer_rules.py:40`), and
`openUI(`/`changeRootUI(` likewise. The scene therefore never names the legacy global; §6's bridge is
how legacy is reached, in the shape `menu/action.h`'s editing-mode mirror already uses.

### 4.2 Files, one concern each

| file | holds |
|---|---|
| `performance_state.h` | the plain state struct: `ParamsForPerformance layout[16]`, `int32_t default_fx_values[16][8]`, `FXColumnPress fx_press[16]`, `PadPress first_pad_press`/`last_pad_press`, the two editing flags, `any_changes_to_save`, and the three in-memory XML backups. No gui/ includes. |
| `performance_scene.{h,cpp}` | the scene: lifecycle, `tick`, `render_grid`, `render_oled`, `on_model_changed`, `is_root`, the kind constant and the singleton accessor. |
| `performance_gestures.cpp` | buttons, pads, encoders: the port of `buttonAction` (`:729-1023`), `padAction` (`:1024-1102`), `normalPadAction` (`:1103`), `padPressAction` (`:1182`), `padReleaseAction` (`:1207`), `paramEditorPadAction` (`:1235`), `isPadShortcut` (`:1298`), `selectEncoderAction` (`:1548`), `modEncoderAction` (`:1625`). |
| `performance_params.cpp` | the model side: `setParameterValue` (`:1434`), `getParameterValue` (`:1491`), `getKnobPosForSinglePadPress` (`:1526`), `calculateKnobPosForSelectEncoderTurn` (`:1574`), and the stutter release (`:1421`). |
| `layout_format.{h,cpp}` | pure `serialise(const PerformanceState&, Serializer&)` and `parse(Deserializer&, PerformanceState&)` — §5. |
| `layout_store.{h,cpp}` | the async I/O port — §5.3. |
| `performance_undo.{h,cpp}` | the scene's own consequence — §7. |
| `performance_bridge.h` | the function-pointer seam for the parts session and arranger own — §6. |

### 4.3 Timeline state

Legacy's class chain is `PerformanceView : ClipNavigationTimelineView`, so it inherits scroll and
zoom, but its own `horizontalEncoderAction` (`:1600`) and `verticalEncoderAction` (`:1604`) do not
scroll or zoom, and `getMaxZoom`/`getMaxLength` (`:1614`, `:1619`) delegate. The scene therefore does
**not** adopt `timeline::TimelineState` unless the plan's encoder audit finds a live scroll or zoom
arm; if it does, it reuses `scenes/timeline/timeline.h` rather than adding its own.

## 5. The layout file

### 5.1 What legacy actually does

`SETTINGS/PerformanceView.XML` (`performance_view.cpp:103`).

* **Read, once.** `focusRegained` (`:328`) calls `readDefaultsFromFile` (`:1885`) only when
  `successfullyReadDefaultsFromFile` is false, and that function returns immediately if it is already
  true (`:1887-1889`). A failed or absent read lands on `loadDefaultLayout` (`:1943`).
* **A c1.3 migration on the read path.** `deluge::storage::bootstrap_action` on `fileExists`
  (`:1892`), then `deluge::io::mkdir(SETTINGS_FOLDER)` (`:1900`) and
  `deluge::io::rename("PerformanceView.XML", …)` (`:1902`) for a root-directory file.
* **Write on save only.** `savePerformanceViewLayout` (`:1733`) calls `writeDefaultsToFile` (`:1740`)
  then `updateLayoutChangeStatus` (`:1686`).
* **Revert reads nothing.** `loadPerformanceViewLayout` (`:1851`) restores from the **in-memory**
  backups via `readDefaultsFromBackedUpFile` (`:1869`), "in order to reduce SD Card IO", and only
  falls back to a file read when the first read never succeeded.
* **The change status is a memory comparison.** `updateLayoutChangeStatus` (`:1686-1732`) compares
  live state against `backupXMLDefaultLayoutForPerformance`, `backupXMLDefaultFXPress` and
  `backupXMLDefaultFXValues`, then blinks or clears the SAVE LED and clears the LOAD LED.

So the port needs **one read and one write**, not a read per status check.

### 5.2 The pure functions

`layout_format.{h,cpp}` holds two functions over the abstract base classes
(`storage/storage_manager.h:117` `Serializer`, `:183` `Deserializer`):

* `void serialise(const PerformanceState&, Serializer&)` — the port of `writeDefaultFXValuesToFile`
  (`:1769`) and the three per-column writers (`:1785`, `:1804`, `:1821`).
* `bool parse(Deserializer&, PerformanceState&)` — the port of `readDefaultFXValuesFromFile`
  (`:1960`) and its four readers (`:1983`, `:2006`, `:2024`, `:2057`), plus `initializeHeldFX`
  (`:2101`).

**No storage edits are needed on this stack.** `FileWriter::setMemoryBased()` already exists
(`storage_manager.h:99`, with `getBufferPtr()` at `:97` and `bytesWritten()` at `:98`, over a 32 KB
buffer allocated in `storage_manager.cpp:874-878`), and the memory-backed reader constructor
`FileDeserializer(uint8_t*, size_t)` exists at `storage_manager.h:207` over
`FileReader(char*, uint32_t)` (`storage_manager.cpp:744-748`, whose `readFileClusterIfNecessary`
short-circuits for a memory-backed reader at `:771-775`).

`serialise`'s signature matches rung3's `PerformanceView::serialiseDefaults(Serializer&)`, which is
what makes the two converge rather than collide.

### 5.3 The async port

`layout_store.{h,cpp}`, modelled on `scenes/browser/listing.h`'s three-state seam and on
`storage::bytes`' request/collect pairs:

* `RequestOutcome request_read()` / `request_write(const char* bytes, uint32_t len)`;
* `Status poll()` returning `Idle`, `Pending`, `Ready`, `Absent` or `Failed`;
* `bool collect_read(PerformanceState&)` and `bool collect_write()`, exactly-once delivery;
* a settable backing so a spec can drive it; sim-backed on this stack.

Contract, matching legacy's observable behaviour:

* The scene requests the read once, on its first focus, and polls from `tick`.
* Until a result lands, and on `Absent` or `Failed`, the default layout shows — legacy's
  `loadDefaultLayout` arm.
* On `Ready`, the parsed state becomes live, the three in-memory backups are taken
  (`backupPerformanceLayout`, `:1307`), the change status is recomputed, and the action log is
  cleared, as `focusRegained` does (`:337`).
* Save requests a write of the serialised bytes and clears `any_changes_to_save` **on the request**,
  not on the collect — rung3 made the same call at its `submitWrite` site, for the same reason: the
  flag means "the layout in memory matches what we last asked to store".
* The c1.3 migration is **not** ported. It is a one-time card-layout fixup on the owner arm, it is
  already moved onto the byte channel in rung3, and nothing in the simulator exercises it. Recorded
  as a non-port.

When rung3 lands, `layout_store.cpp`'s backing becomes a few lines over
`deluge::storage::bytes::requestRead`/`collectRead` and `submitWrite`. Nothing above it changes.

## 6. The parts session and arranger own: `PerformanceBridge`

Legacy hands four things to `sessionView`/`arrangerView`, neither of which has a scene:

| legacy | site |
|---|---|
| sidebar rendering | `renderSidebar` (`:490-512`) calls `sessionView.renderSidebar` or `arrangerView.renderSidebar` |
| sidebar pads | `padAction`'s sidebar arm (`:1057-1101`): status pads, audition pads, row-layout `padAction`, `gridHandlePads` |
| the graphics routine | `graphicsRoutine` (`:357-381`): `potentiallyUpdateCompressorLEDs`, the VU meter, `displayLoopsRemainingPopup`, `potentiallyRenderClipLaunchPlayhead` |
| the timer | `timerCallback` (`:383-391`) delegates to `sessionView` or `arrangerView` |

and the SESSION_VIEW arm (`:762-828`) additionally needs `arrangerView.xScrollWhenPlaybackStarted`
and `sessionView.gridFirstPadActive()` for arrangement record start and stop.

`performance_bridge.h` declares a function-pointer table, installed from `src/deluge/gui/` at boot,
exactly as `scenes/menu/note_bridge.h` is: `render_sidebar`, `sidebar_pad`, `graphics_routine`,
`timer`, `set_arranger_scroll_when_playback_started`, `grid_first_pad_active`. Unset pointers are
no-ops, so a harness without the bridge still runs.

Consequences, stated plainly:

* The scene's sidebar is **legacy's** sidebar on this stack, which is why the oracle can compare the
  whole grid. The sidebar half of that comparison proves the bridge is wired, not that a sidebar was
  ported. It is counted that way.
* When `SessionScene` and `ArrangerScene` exist, the pointers move to them. Nothing else changes.
* The one sidebar arm that is an exit (`:1091-1095`, GREEN/BLUE pads calling `changeRootUI`) is
  declined with the other exits — see §9.

## 7. Undo

Legacy's chain: `logPerformanceViewPress` (`:1328`) → `anyChangesToLog` (`:1338`) →
`ActionLogger::recordPerformanceViewPress` (`action_logger.cpp:301-313`), which takes a
`PARAM_UNAUTOMATED_VALUE_CHANGE` action with `ActionAddition::ALLOWED` and adds a
`ConsequencePerformanceViewPress`, whose `revert` memcpys straight into the legacy global
(`consequence_performance_view_press.cpp:32-34`).

The port:

1. **Named legacy edit (§10.1).** `PadPress` (`performance_view.h:33`), `FXColumnPress` (`:41`) and
   `ParamsForPerformance` (`:49`) move verbatim into a new neutral header,
   `src/deluge/model/performance/performance_types.h`; `performance_view.h` includes it. Both sides
   then share one definition, and `src/scenes` can name the types without including `gui/`.
2. **The scene's own consequence.** `performance_undo.{h,cpp}` defines
   `ConsequencePerformanceScenePress` with the same before/after snapshot of one column. Its `revert`
   calls back into the scene (`performance_scene().restore_press(x, snapshot)`) rather than writing a
   global. Recording goes through `actionLogger.getNewAction(...)` plus `Action::addConsequence`
   directly — the path `AudioClipScene` already uses (`audio_clip_scene.cpp:819`, `:872`, `:923`), so
   `ActionLogger` gains nothing and loses nothing.
3. The parameter values themselves revert through whatever the shared model write logged, identically
   on both sides, because both sides perform the same write.

## 8. Outside refresh and the services

`possiblyRefreshPerformanceViewDisplay` (`:669-686`) has three callers.

**Ported: the mod-encoder turn.** `view.cpp:888-891` asks the view whether it is already showing the
turned parameter; the answer suppresses the popup at `:905`. The scene exposes the same question as
`showing_param(kind, id, knob_pos) -> bool`, a direct port of the function's two arms: redraw the FX
display and claim the popup when the held pad's parameter matches; otherwise, if nothing is held and
the FX display is up, fall back to the view display. `services::ModKnobs::popup_suppressed` gains the
arm, asking the scene when the stack root is `performance_scene()` — the same identity test it already
makes against `menu_scene()` (`mod_knobs.cpp:351`). The "uncovered" note at `mod_knobs.h:506-510`
goes away.

Both services' `root_is_performance` are driven from the stack root's identity when the root is a
scene; the guest keeps filling them from `getRootUI()` (`shell_guest_services.cpp:135`, `:155`).

**Not ported, recorded as uncovered:** `midi_follow.cpp:1124` and
`mod_controllable_audio.cpp:1234`. Both are legacy MIDI paths that reach the legacy global by
identity and raise no notify fact, and the shell renders only on input or a dirty request
(`shell/shell.cpp:374-394`, `apply_dirty` at `:543-567`). So under the scene, an incoming CC on the
held parameter would leave the FX display stale and would not suppress the MIDI-follow popup. These
go live when MIDI follow is ported. The alternative — raising the existing `Dirty::ParamAutomation`
at those two legacy sites — was considered and declined: it would add an OLED repaint per incoming CC
to the legacy leg, a real behaviour change in shipping code for a scene that is not reachable on
device.

**Also recorded, same reason:** `view.cpp:2691`, `:2774` and `:2792` treat `performanceView` like
`sessionView` for clip-status MIDI-learn repaints and the play-flash routine. They are legacy-side
identity tests with no scene arm, and the flash routine's work is already inside §6's bridge
territory.

## 9. Exits, and the editing modes

### 9.1 Declined, recorded `NOT PORTED`

Each falls through, as `AudioClipScene`'s do (`audio_clip_scene.cpp:435-446`), so `Router::run_chain`
carries the event and nothing in `src/scenes` names a legacy root:

| gesture | legacy | what it does |
|---|---|---|
| CLIP_VIEW | `:748-751` | `releaseViewOnExit`, then `sessionView.transitionToViewForClip()` |
| KEYBOARD, no shift | `:959-969` | `changeRootUI` to automation, arranger or session |
| KEYBOARD, hold then release | `:977-987` | the same three |
| sidebar GREEN/BLUE pad | `:1092-1093` | `changeRootUI(&sessionView)` and a grid pad handoff |
| SELECT_ENC, no shift | `:892-899` | `openShellMenu(..., performanceView=true)`, else `openUI(&soundEditor)` |
| shift + main pad | `:1038` | `soundEditor.potentialShortcutPadAction` |

The last two are "push a menu from a scene", which `AudioClipScene` records as the wiring plan's job,
not a scene library job. The reverse route is `MenuAction::OpenPerformanceView`
(`scenes/menu/action.h:119`, filed at `scenes/menu/nodes/actions.cpp:163`, drained under the guest at
`gui/ui/shell_action_dispatch.cpp:317`, a no-op in `menu_scene.cpp:1808`); it stays guest-only.

Because the exits are declined, `releaseViewOnExit` (`:1402-1412`) never runs in the port. Its three
effects — release stutter, reset both pad-press records, clear both editing flags — are ported as a
scene method and called from the editing-mode exit only. Recorded divergence: a declined exit leaves
the scene's state where legacy would have cleared it.

### 9.2 The editing modes, which are ported

Shift+KEYBOARD (`:921-958`) walks DISABLED → VALUE → PARAM and back out:

* The first press sets `defaultEditingMode`; a second, with it already set, adds `editingParam`;
  with both set it clears them and `close()`s.
* Legacy calls `openUI(&performanceView)` on **itself** when entering the mode from outside it
  (`:949`), and `close()`s that second stack entry when leaving.
* The mode mirror `menu::set_performance_editing_mode` is synced at every transition, including
  inside `releaseViewOnExit`.

In the port the scene is already the input top, so the two flags are plain scene state and there is
**no second stack entry**: the effects of `opened()`/`focusRegained()` that legacy gets from the
self-`openUI` — the change-status update and the LED states — are performed in place. Recorded
divergence: the stack is one entry shallower than legacy's, which is not observable on either
surface.

The mirror (`menu/nodes/actions.cpp:45-52`) stays the single source of truth for what the PERFORM FX
menu shows, and the scene writes it on the same transitions legacy does.

## 10. The two named legacy edits, and nothing else

1. **Move three plain structs.** `PadPress`, `FXColumnPress` and `ParamsForPerformance` move verbatim
   from `gui/views/performance_view.h:33-56` into
   `src/deluge/model/performance/performance_types.h`, which `performance_view.h` then includes. No
   member changes, no behaviour change. Needed because `src/scenes` may not include `gui/`.
2. **One friend declaration for the oracle.** `performance_view.h` gains
   `friend struct PerformanceViewOracle;` so the simulator spec can drive legacy's private layout
   serialise and parse (`:160` `writeDefaultFXValuesToFile`, `:167` `readDefaultFXValuesFromFile`,
   both under the `private:` at `:128`) without a mounted card. Legacy behaviour is unchanged; a
   friend of a type that only the spec defines adds no code to the firmware.

Everything else in `gui/` is untouched, including every storage function, which is what keeps the
rung ladder free of conflicts.

## 11. Testing, with coverage stated in advance

### 11.1 The oracle

`sim/scenes_spec/performance_scene_spec.cpp`, in `audio_clip_scene_spec.cpp`'s shape:

* **The port leg:** a real `Shell` over `uitest::FakeSurfaceSet`, the scene entered through its own
  `open(...)`, driven by `Shell::tick`.
* **The legacy leg:** legacy's own public entry points over the same `Song` —
  `renderMainPads` (`:393`), `renderSidebar` (`:490`), `renderOLED` (`:687`), `padAction` (`:1024`),
  `buttonAction` (`:729`), `possiblyRefreshPerformanceViewDisplay` (`:669`),
  `savePerformanceViewLayout`/`loadPerformanceViewLayout` (`:1733`, `:1851`), and through
  `PerformanceViewOracle` the two private layout functions.
* Nothing the port computed is handed to legacy. Each side reads the shared model for itself.
* The pad comparison composes legacy the way the device does, through `PadLEDs::prepareColour`, as
  `audio_clip_scene_spec.cpp` establishes.

### 11.2 What is compared

| area | comparison |
|---|---|
| main 16 columns | whole-grid pixel equality, every row colour, tail colour and held-pad state |
| sidebar | whole-grid equality — counted as bridge wiring, not as a ported sidebar (§6) |
| OLED | the view display (`:513`) and the FX display (`:581`), frame by frame |
| press / hold / latch / restore | `normalPadAction` and its four helpers, each side from its own sentinel |
| parameters | model values after each gesture, including the stutter arms |
| undo | revert on both sides, then the hold snapshots and the grid |
| layout serialise | the port's bytes are **byte-identical** to legacy's writer in memory mode |
| layout parse | legacy's bytes parse into state equal to legacy's own after its read |
| the store | pending shows the default layout; `Absent` and `Failed` keep it; save and load are request → collect |
| services | `ModKnobs`' suppression arm and `GlobalControls`' KEYBOARD guard, driven from the stack root in their existing specs |

### 11.3 Level 2

`tests/ui-scripts/shell/performance.txt` and `performance_64.txt` (the 128×64 panel variant, with
`board_64()`/`profile_64()` from `sim/scenes_spec/scene_scripts_spec.cpp:218`, `:226`), registered
beside the audio-clip rows (`:542`, `:569`). `SceneReset` (`:321-345`) gains a `performance_scene()`
reset so the singleton does not leak between examples.

Level 3 (`ui_app_scripts_menu_ab`) is **not** extended: the A/B leg runs the guest, and a root scene
is not reachable there until spec F.

### 11.4 The gate

Unchanged, and all of it:

```
cmake --build build-sim --target deluge_host deluge_scenes_spec -j8 && ctest --test-dir build-sim
cmake --build build/tests -j8 && ctest --test-dir build/tests -C Debug
scripts/check_layer_rules.py (+ --audit-symbols), check_platform_boundary.py, check_gfx_layer.py,
scripts/check_citations.py --strict, scripts/check_citation_shift.py <base>..HEAD (advisory; the
criterion is zero drift), scripts/menu_dump_check.sh
```

Never a bare `cmake --build build-sim`. `./dbt build Debug` is not evidence for this work.

## 12. Recorded, not ported

* **Dead state.** `layoutBank` (`performance_view.h:189`) is never read or written anywhere in the
  tree. `layoutVariant` (`:190`) is written once (`performance_view.cpp:241`) and never read. Neither
  is ported; neither is deleted from legacy.
* **The c1.3 root-file migration** on the read path (§5.3).
* **The MIDI-driven refresh arms** and the three `view.cpp` session-identity arms (§8).
* **Every exit** in §9.1, and `releaseViewOnExit`'s effects on those paths.
* **The stack depth difference** for the editing modes (§9.2).

## 13. Open questions this spec does not answer

1. **Does any encoder arm still scroll or zoom?** The plan's audit decides whether
   `timeline::TimelineState` is adopted (§4.3). Both answers are cheap; neither changes anything else.
2. **How many legacy entry points does this reach, and across how many comparison sites?** E3 and E4
   wrote that number into the migration spec (§10.6: 26/27; §10.7: 64/121). This spec commits to
   producing the same count for Performance; the number itself is the plan's to measure.
3. **Where the bridge's four pointers live after `SessionScene` exists** is that scene's spec, not
   this one.
