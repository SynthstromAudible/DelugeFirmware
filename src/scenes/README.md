# deluge_scenes

The first UI layer allowed to include the model (spec: `docs/superpowers/specs/2026-08-26-ui-scenes-menus-design.md`,
spec E of the UI stack redesign). E1 is complete as of the twelve tasks of
`docs/superpowers/plans/2026-08-27-ui-scenes-menus-1.md`: the library target and the shared CppSpec
CMake module, the l10n bridge and `scenes::deluge_profile()`, the declarative `Node` tree with all
ten root tables, `MenuContext` resolution, `bindings::read`/`write`, the `MenuScene` interpreter,
the shortcut tables, the validator, the node-id registry and `deluge_menu_dump`.

E2 is complete as of the thirteen tasks of `docs/superpowers/plans/2026-08-30-ui-scenes-menus-2.md`:
every remaining legacy menu item that needs no card access now has a real implementation behind it.
That added two scenes (`rename/`, `context_menu/`), three toolkit widgets (`ui::oled::text_edit`,
`option_window`, `routing_chain`/`segmented_switch`), input dispatch and lifecycle hooks on
`CustomItem`, `Node::select_chain`/`chain_targets`, `Kind::Range`, and the **action seam**
(`menu/action.h`). What E2 did *not* do is give the library an entry point — see the box below —
and the eleven `kPendingCustomNodes` of the file/sample family were E3's.

E3 is complete as of the twelve tasks of `docs/superpowers/plans/2026-09-02-ui-scenes-menus-3.md`,
and it is the plan that **closed the menu tree**. It added three scenes — `browser/`, `marker/` and
`recorder/` — the **async listing seam** (`browser/listing.h` over `storage::FileCatalogue`), two
extractions out of `gui/` (`storage/file_catalogue.*` and `model/sample/waveform_*`), three toolkit
pieces (`ui::grid::markers`, `ui::oled::sample_position`, `ui::oled::text`'s additions), the MIDI
per-device page, `CustomItem::on_focus_backward` / `on_focus_is_press()`, and `Shell::tick`'s
repaint fold with `Scene::timer_repaints()`. Its tasks 6 and 7 closed all eleven remaining
placeholders: task 6 the nine of the file/sample family (`menu/nodes/sample.h`: the two file
browsers, the four loop points, the audio clip's waveform view and the two record-to-sample items)
and task 7 the last two, `midi_device_linked` and `specific_output_selector`
(`menu/nodes/output_items.h`). **The menu tree has no `Kind::Custom` placeholders left**;
`kPendingCustomNodes` is 0 and `ValidateOptions::allow_placeholder_custom` is gone with them.

E4 is complete as of the fourteen tasks of
`docs/superpowers/plans/2026-09-06-ui-scenes-services-and-roots-4.md`, and it is the plan that
**broke up `View`**. `gui/views/view.cpp` is 2,908 lines and holds at least three unrelated
concerns; E4 ported three of them as `shell::Service` implementations that live *here* rather than
in `src/shell/` — `services::ModKnobs`, `services::GlobalControls` and `services::MidiLearn`
(see "The three services" below for why the directory is this one). It added the three missing
overlay scenes — `overlay::ParamPopupScene`, `overlay::CountInScene`, `overlay::ScreensaverScene` —
the shared `timeline::TimelineState` every clip view needs, and the first two application scenes
that own the grid: `slicing::SlicerScene` and `audio_clip::AudioClipScene`, the latter being the
**first scene in this library with `is_root() == true`**. It moved `Rainfall` and `Starfield` out of
`hid/display/` into `src/ui/oled/`, the only extraction out of `hid/` in the plan. And it found
**three defects in the shell's own contracts** — an inverted router chain, a declared LED blink that
was never a wake source, and a service timer that could not repaint anything — each invisible for
three plans because the thing that would have exercised it did not exist yet.

What is left is not menu items, and it is no longer the load/save family either: E4's scope moved
(spec E §7's row 4 came first, since a root scene is what makes a service's root-gated arms
reachable at all). What is left is spec F's wiring, the **load/save family** — the song, preset,
pattern and kit-row browsers, which are reached from root UIs rather than from menu items — and the
remaining root scenes, `KeyboardScene` (5,252 lines) and `PerformanceScene` (2,249). See "E4
roadmap" at the end of this file, which is now E5's list.

> **Read this before adding a menu item.** The shipping firmware still runs the *legacy*
> `SoundEditor` and its ~200 hand-written `MenuItem` classes under `src/deluge/gui/menu_item/`.
> **`deluge_scenes` is now linked into the firmware and the host sim** (F1 task 1) and **eight of
> the fourteen `openUI(&soundEditor)` sites can open this tree instead** (F1 task 9) — but only when
> the `ShellMenu` community feature is on, and it is **Off by default**. So during the coexistence:
>
> * **A menu item users must be able to open goes into `gui/menu_item/` + `gui/ui/menus.cpp`**, the
>   legacy tree, exactly as before. That is the system the device runs with the toggle off, which is
>   how every device ships today.
> * **A node added here is a port of a legacy item**, kept in step with it and cross-checked
>   against it by `sim/scenes_spec/tables_*_spec.cpp`. **With the toggle off it changes nothing a
>   user can see; with the toggle on it is what they get.** E2 and E3 finished the *items* — the tree
>   is complete and placeholder-free — and F1 built the wiring that reaches them:
>   `deluge::gui::ui::ShellHostUI` is a legacy `UI` holding a `Shell`, `openShellMenu`
>   (`src/deluge/util/shell_menu_entry.h`) is the one door, and `deluge_boot` numbers the tree and
>   installs the three services.
> * **What is still not wired, and each has an owner** (migration spec §10.2 is the list):
>   **nothing performs a `MenuAction`** — the five items that ask for legacy root-UI behaviour do
>   nothing with the toggle on; **nothing drains `take_choice()`**; **`perform_editor` has no sync
>   calls**; **six of the fourteen entry points still open the legacy `SoundEditor`**, each because
>   its root node has no port; and **two of the four MIDI-learn messages have a transport but no
>   producer**, with all four inert for a third reason recorded at §10.2 item 8.
>   `menu::set_shortcuts_version` and `assigned_node_ids` *are* called now — see "Boot wiring this
>   layer does not do for itself" below, which is down to one open item.
> * **A complete tree is not a reachable one, and a reachable one is not a finished one.** The
>   deletion of `gui/menu_item/**` waits on the wiring, not on the tree —
>   `docs/superpowers/specs/2026-08-26-ui-migration-design.md`'s deletion schedule is the record, and
>   §10.8 is what its acceptance oracle actually covers.
> * **Adding an item to one tree only silently diverges them**, and that now costs a user rather
>   than a test. A new legacy item with no node here fails no test today; a node here with no legacy
>   item fails the lockstep walk immediately. If you add to `gui/menu_item/`, add the node too.

Rules:
1. `src/scenes/**` may include `scenes/**`, `shell/**`, `ui/**`, `gfx/**`, `notify/**`,
   `libdeluge/**`, the standard library, and -- being the first model-aware layer --
   `model/**`, `modulation/**`, `playback/**`, `processing/**`, `storage/**`, `gui/l10n/**`,
   `definitions_cxx.hpp`.
   Never `gui/` otherwise, never `hid/`. `model/model_stack.h` is narrower still: only
   `menu/context.cpp` and `menu/bindings.cpp` may include it, and `menu::model_stack_for()` is
   the single entry point everything else uses to reach a `ModelStack` (task-3 ruling 6).
   Two more narrow rows, both added by E3 task 12 and both with a zero baseline:
   **`scenes-listing-through-the-seam`** confines `FileCatalogue`'s six *dispatching* entry points
   (`readFileItemsForFolder`, `sortFileItems`, `cullSomeFileItems`, `deleteSomeFileItems`,
   `deleteFolderAndDuplicateItems`, `getNewFileItem`) to `browser/listing.{h,cpp}`, because that is
   the only thing in this layer that holds the catalogue's claim while it dispatches — the read-only
   spellings a landed result needs (`fileItems`, `currentDir`, `searchFileItems`, `repinComparator`,
   `emptyFileItems`) are deliberately not in the row; and **`scenes-no-legacy-waveform`** (two rows,
   `src/scenes/**` and `src/ui/**`) forbids naming `waveformRenderer` or `waveformBasicNavigator`,
   which is what keeps E3 task 4's arithmetic-down/drawing-up split split. Both are symbol rows for
   the reason `scenes-no-root-ui` is: the include rows already forbid the header, so a regression
   would be spelled as a bare `extern`. `--audit-symbols` checks every name in all three lists is a
   symbol this tree really declares.

   E4 task 14 added three more, all zero-baseline and each probed on its own.
   **`scenes-no-legacy-hid-drivers`** (two rows, `src/scenes/**` and `src/ui/**`) forbids the five
   legacy output-driver spellings — `uiNeedsRendering(`, `renderUIsForOled(`, `indicator_leds::`,
   `PadLEDs::`, `uiTimerManager.` — which is `model-calls-ui`'s alternative list minus everything
   `scenes-no-display` and `scenes-no-root-ui` already hold this layer to. It matters here because
   the three services are the port of `View`, which is *the* legacy caller of all five; the port
   declares LEDs through `shell::Leds`, files `Action::Kind::Repaint` and returns a deadline from
   `Service::next_timer()`. `PadLEDs::flashCursor` is excluded by name — it is the persisted setting
   byte `flash::Status` edits, not a driver call — and `currentUIMode`/`isUIModeActive(` are
   excluded because trailing citation comments in `services/global_controls.cpp` would otherwise
   give the row a non-zero baseline. **`scenes-no-input-lock`** forbids `Action::Kind::LockInput`
   from this library, which Global Constraint 5 has asserted in prose since E2 and six `@warning`s
   repeat with no mechanism behind them; and **`services-no-oled-timer-claim`** forbids
   `.oled = true` in `src/scenes/services/**`, for the reason in "The three services" above.

   Also E4 task 14: **a symbol row no longer counts `#include` lines.** The row that made the case
   is `scenes-no-root-ui`, which lists `slicer` as a whole word and so flagged the plan's own
   mandated path `src/scenes/slicer/slicer_scene.h` from its two include lines — which is why the
   directory is `slicing/`. Nothing is lost, and that is checked rather than argued: every symbol
   row is scoped to a directory an include row already gates, so a real
   `#include "gui/ui/slicer.h"` still fails, under `scenes-includes-gui`, which names the actual
   violation. Zero of the baseline's 120 counts moved.
2. `snake_case` functions/variables, `PascalCase` types, `k` constants; namespace
   `deluge::scenes`.
3. No heap in library code; no wall clock — `Scene::next_timer()` is how a blink stays alive, and
   since E3 task 10 that is nearly all it takes: `Shell::tick`'s step 8b arms the surfaces the
   scene's `Scene::timer_repaints()` mask names — **of those it is not covered on** — at the
   deadline the hook returns, so a scene no longer has to remember a matching
   `RepaintGate::request_repaint_after` from inside its own `render_*`. Several still make one, more
   tightly, where the scene's single interval is coarser than the phase actually being drawn. A
   covered scene arms nothing and is woken by `compute_next_wake` instead, so its `tick(dt)` still
   lands on its own cadence.

   **The mask is per surface (`{oled, grid}`), not a boolean opt-out**, and the reason is a real
   cost rather than tidiness: the grid's flush diffs against `last_sent` and blits nothing when a
   repaint changed no pixel, while the OLED flush blits unconditionally once it has rendered. So
   arming a surface you do not need is free on the grid and a permanent blit on the OLED —
   `MenuScene` masks the OLED off because its timer is purely the shortcut/source pad blink, and
   arming both turned "sitting on a patched-param leaf" into 50 Hz on both panels forever. See
   "The repaint fold" below, and the shell design spec's §15.1.

## Link direction (ruling Q4)

`deluge_scenes` links `deluge_app` (an `OBJECT` library) PUBLIC, alongside `deluge_ui_shell` --
that's the model-aware layer this library exists to be. **Nothing links `deluge_scenes` yet**;
that starts once a later task needs a real entry point (a registered scene, a menu). Until then
its only consumer is its own spec executable, `deluge_scenes_spec` (`sim/CMakeLists.txt`).

`add_subdirectory(scenes)` in `src/CMakeLists.txt` and `sim/CMakeLists.txt` both come **after**
`add_subdirectory(deluge)`, since `deluge_scenes` needs `deluge_app` to already exist to link
against it.

## The l10n bridge (`l10n_bridge.h`, ruling Q3)

`src/shell` has no l10n of its own -- a board profile supplies `BoardProfile::text`/`error_text`,
or leaves them null (`ui::shell::deluge_profile()` does exactly that,
`src/shell/profiles/deluge.cpp:143-144`, deliberately: the `l10n::String` table these resolve
against belongs to `gui/l10n/`, which `src/shell` may not include). `src/scenes` is the layer
allowed to include `gui/l10n/`, so it supplies the bridge:

| function | purpose |
|---|---|
| `text_lookup(uint16_t)` | `l10n::getView(static_cast<l10n::String>(string_id))`. Matches `ui::shell::TextLookup`. |
| `error_lookup(int16_t)` | A hand-transcribed copy of `getErrorMessage(Error)`'s switch (`src/deluge/hid/display/display.cpp:7`) -- `src/scenes` may not include `hid/`, so this cannot call the original. Matches `ui::shell::ErrorLookup`. |

## The scenes profile (`profile.h`, ruling Q3)

`scenes::deluge_profile()` is `ui::shell::deluge_profile()` field-for-field, with `.text` and
`.error_text` filled in from the bridge above -- a function-local `static const BoardProfile`
copy-initialised from the shell's own profile. `BoardProfile` is spans and function pointers, so
the copy is cheap and owns nothing; `profile_spec.cpp` (in `sim/scenes_spec/`) is the drift guard
that asserts every field *other* than the two hooks stays byte-identical to the shell's copy (spans
compared by `.data()`/`.size()`, not by value) -- this is what the two-profile split costs, and the
one place it would silently break.

`deluge_scenes` and every scenes spec pass `scenes::deluge_profile()` to `Shell`/
`RunOptions::profile`, never `ui::shell::deluge_profile()` directly -- the shell's own copy exists
so `src/shell`'s specs don't need the l10n bridge, not because it's the profile applications use.

## Testing: two spec trees (ruling Q5)

`src/scenes` is model-aware, so its specs need the real `deluge_app` -- not `tests/`'s
mock display/print doubles, which define symbols `deluge_app` also defines. Two spec locations,
different reasons:

- **`tests/spec/scenes/`** -- pure parts that need no model at all: `node_spec.cpp` (the builders),
  `node_id_spec.cpp` (the id registry) and `validate_spec.cpp` (the rule engine, over fake trees).
  Built into `tests/`'s `all_specs`, same as every other layer's specs.
- **`sim/scenes_spec/`** -- model-backed specs, twenty-four files as of E3: the E1 set
  (`l10n_bridge_spec.cpp`, `profile_spec.cpp`, `context_spec.cpp`, `bindings_spec.cpp`,
  `menu_scene_spec.cpp`, `menu_scripts_spec.cpp`, `shortcuts_spec.cpp` and the three table
  cross-checks `tables_1_spec.cpp` / `tables_2_spec.cpp` / `tables_3_spec.cpp`), E2's item-family
  specs, and E3's `file_catalogue_spec.cpp`, `listing_spec.cpp`, `browser_scene_spec.cpp`,
  `marker_scene_spec.cpp`, `recorder_scene_spec.cpp`, `sample_items_spec.cpp`,
  `output_items_spec.cpp` and `midi_device_spec.cpp` --
  built into their own executable, `deluge_scenes_spec`, in the `sim/` project: the one place a
  spec can link `deluge_app`, the host BSP, and `deluge_scenes` together. `cmake/deluge_specs.cmake`
  (factored out of `tests/spec/CMakeLists.txt` this task, so both projects share one copy) supplies
  the CppSpec fetch and `create_specs_driver()`; `deluge_scenes_spec_support` is the mock-free
  support library (`tests/harness/ui_script.cpp`, `snapshot.cpp`) a spec that drives a script needs.
  Run with `ctest --test-dir build-sim -R scenes` after
  `cmake --build build-sim --target deluge_scenes_spec`.

Nothing under `src/scenes` depends on either spec tree existing; both are testing infrastructure
looking in, the same relationship `tests/spec/shell/` has to `src/shell`.

## The menu tree (`menu/`, spec E §4, Tasks 2–11)

`menu/node.h` defines the declarative data model: `Node`, `Binding`, `ParamRef`, `Kind`, `Hint` (an
alias of `ui::oled::Hint`), `IntRange` (an alias of `ui::oled::Range`), and the `constexpr` builders
(`param`, `toggle`, `enumeration`, `submenu`, `custom`, …) that assemble a tree as plain data with no
code generation. Ten root tables live in `menu/nodes/*.cpp` (one file per legacy subtree — sound,
kit, MIDI/CV, song, settings, …), gathered by `MenuScene::roots()`
(`menu/menu_scene.h`/`nodes/nodes.h`) and indexed by `MenuContext::Root`.

### Adding a node

1. Find the legacy `MenuItem` subclass the node replaces (`gui/menu_item/**`, `gui/ui/menus.cpp`)
   and read its `getName`/`isRelevant`/`getValue`/`setValue`/`checkPermissionToBeginSession`.
2. Pick a builder from `node.h` matching its `Kind` (`param` for a `ParamRef`-backed item, `toggle`,
   `enumeration`, `submenu`, or `custom` for the escape hatch — see below). Add the node to the
   relevant `menu/nodes/<subtree>.{h,cpp}` file, following that file's existing table-comment style
   (a `| node | legacy global | legacy class | kind |` markdown table naming exactly where each node
   came from — every table in the tree does this, and a new one should too).
3. If the item's relevance depends on model state, write or reuse a predicate in `menu/relevance.cpp`
   and add its `{predicate, name}` row to the registry `relevance::name_of` searches
   (`relevance.cpp`'s row table, guarded by a `static_assert` on the row count so a missing entry is
   a build failure, not a silent "?" from the dump).
4. If the item reads or writes a `ParamRef` or a `get`/`set` hook pair, wire it through
   `menu/bindings.cpp` (`bindings::read`/`write`) — see "Adding a `Kind`" below if no existing
   binding shape fits.
5. Run `python3 scripts/check_layer_rules.py` and the relevant `sim/scenes_spec/tables_*_spec.cpp`
   spec; a new node usually needs a row added to whichever table-cross-check spec covers its root.

### Adding a `Kind`

The shipped `Kind` enum is `{Submenu, Toggle, Enum, Int, Decimal, SyncLevel, Range, Custom}` —
still narrower than spec E's original draft, which also listed `Bipolar`, `MultiRange`, `Colour`
and `Action`.

**The rule that decides this is spec E line 95: a kind exists only if ≥3 items use it.** It has now
been applied in all three directions, and the history is worth reading before you propose a kind:

* **Dropped at zero (E1 task 8).** `Bipolar`, `Range`, `MultiRange`, `Colour` and `Action` had no
  node at all across the ten roots. Speculative kinds, deleted.
* **Created and deleted inside one task (E2 task 2).** `Kind::Info` was going to carry the two
  read-only text pages, `firmware_version` and `battery_level`. Two is under the bar, so it was
  under the bar the day it was written into the plan — and a second reason confirmed the first:
  `InfoFn` was a stateless function pointer, and legacy's battery item derives "is charging" from a
  voltage delta held *on the item instance* across timer ticks (`battery/level.h:57-61`), so the
  kind's shape was dictating a behaviour loss. Both items are `CustomItem`s, which may hold
  `mutable` state exactly as legacy does. The custom-input plumbing that task built is what every
  later item group stands on; the kind is what was wrong.
* **Returned at exactly three (E2 task 11b).** `Kind::Range` came back for `default_tempo`,
  `default_swing_amount` and `default_key` — legacy's `menu_item::Range` and its two subclasses
  (`gui/menu_item/range.h:30`, `integer_range.h:25`, `key_range.h:24`). Three is the bar, and the
  node set is pinned by a spec so a fourth consumer or a lost third is a deliberate change. The
  same rule that deleted it as speculative is the rule that brought it back.
* **Not needed even where it looks obvious (E2 task 11).** The six pad colours were expected to
  want a `Kind::Colour`. They do not: legacy's `menu_item::Colour` is a plain `Selection` over nine
  options plus a rendering side effect, so the port's six are ordinary `Kind::Enum` nodes with
  get/set hooks. "There is a legacy class named after it" is not the test; three consumers with a
  shape no existing kind expresses is.

Before adding a new `Kind`, count actual users the same way — a kind that will not clear three real
nodes belongs as a `Custom` item instead. See the design spec's amendments section for the cited
account of each of the four moves above.

### Adding a `Custom`

`Kind::Custom` is the escape hatch for a bespoke item that fits no `Kind`, capped by
`validate_tree`'s `TooManyCustom` rule. **The cap is not spec E §4's "at most 10", and the rule
counts nodes, not implementations** (fix round 2, I4; recorded as an amendment in the design spec):

* The rule used to count *distinct `CustomItem` instances*. E1 ships the shape of its bespoke items
  and points every one of them at the single shared `nodes::kPending` placeholder, so that count was
  **1** for the whole tree and `TooManyCustom` could never fire — the rule was switched off for
  exactly the phase it was meant to police.
* It now counts **nodes that point at a custom item**. The shipped tree has **55** of them
  (`validate.h`'s `kShippedCustomNodes`, asserted against the live tree by
  `sim/scenes_spec/shortcuts_spec.cpp`), behind **55** distinct items -- **every one of them real**,
  so node count and item count are now equal and no two nodes share an item
  (`validate.h`'s `kPendingCustomNodes` is 0, pinned three ways by
  `sim/scenes_spec/output_items_spec.cpp`). The cap `kMaxCustomNodes` is **56**, one above the
  count. (It was 64
  until E2 task 11 turned nine of the placeholders -- the six pad colours, the play cursor, the
  dev-sysex setting and the shortcuts version -- into ordinary `Kind::Enum` items with get/set
  hooks; none of the nine needed a bespoke item at all once its state was reachable. E2 task 11b
  took three more out of the hatch entirely by reviving `Kind::Range` for `default_tempo`,
  `default_swing_amount` and `default_key`, and E2 task 12 ratcheted the cap down to follow.) The real, distinct exceptions
  are `battery_level` and `firmware_version` (E2 task 2 — `BatteryLevelItem`, `FirmwareVersionItem`),
  the three name editors (E2 task 5 — one `EditNameItem` per `rename::TargetId`), the context-menu
  openers (E2 task 7 and 11b — one `ContextMenuItem` per `context_menu::DescriptorId`), the twelve MIDI commands
  (E2 task 8 — one `MidiCommandItem` per `GlobalMIDICommand`) and E2 task 9's eight patch-cable /
  range-picker items, E2 task 10's three dynamic settings items, and E2 task 11b's five action
  items (four `ActionItem`s plus `PerformEditorItem`, `menu/nodes/actions.h`) and its two
  `ActiveScalesItem`s (`menu/nodes/scales.h`) and the sixth `ContextMenuItem`
  (`menu/nodes/context_menu_items.h` — the class the five reset items became once
  `audio_source_selector` needed the same one), and E3 task 6's nine sample items (two
  `FileBrowserItem`s, five `MarkerItem`s, two `AudioRecorderItem`s — `menu/nodes/sample.h`; nine
  instances rather than three because `on_focus` is handed a `MenuContext` and never its own `Node`,
  so one shared instance could not tell which source it was focused as), and E3 task 7's two output
  items (`MidiDeviceLinkedItem`, `SpecificOutputItem` — `menu/nodes/output_items.h`), whose arrival
  took the shared `kPending` *out* of the set: the distinct-instance count that spelling used to be
  inert on is now 52, not 1. E3 task 8's three MPE items (`MpeDirectionItem`, `MpeZoneItem`,
  `MpeZoneChannelsItem` — `menu/nodes/midi_device.h`) take it to **55**, and they are the first
  additions that are *new nodes* rather than retired placeholders: all three are bespoke because
  their value is `MenuState` navigation state (`mpe_direction`/`mpe_zone`) that a `Binding`'s
  `get`/`set` — handed only a `const MenuContext&` — cannot see, and the direction selector
  additionally computes its title from that value.
* So the cap is `kMaxCustomNodes == 56`, one above reality — a ratchet against the escape hatch
  growing unnoticed, not a claim that ten was ever achievable. Adding a custom node means bumping
  both `kShippedCustomNodes` and, if you have run out of room, `kMaxCustomNodes` — deliberately two
  places. Adding one that is still a *placeholder* means a third: `kPendingCustomNodes` (now **0**)
  and the by-identity list in `sim/scenes_spec/shortcuts_spec.cpp` (now empty), so "what is still
  owed" cannot drift while its count stands still. There is no longer any option that suppresses
  the report — see rule 3 below.

To add one:

1. Implement `CustomItem` (`node.h:272`). Two pure virtuals and nine defaulted ones, all added by
   E2 as the item families needed them — implement only what your item actually has:

   | virtual | added by | legacy counterpart |
   |---|---|---|
   | `render(OledFrame&, const MenuContext&, MenuState&) const` | E1 | `drawPixelsForOled` |
   | `handle(UiEvent, UiContext, const MenuContext&, MenuState&) const` | E2 task 2 | the item's own `buttonAction`/`padAction`/`selectEncoderAction` |
   | `on_focus(const MenuContext&, MenuState&) const` | E2 task 2 | `beginSession` |
   | `commit(const MenuContext&, MenuState&) const` | E1 | `writeCurrentValue` |
   | `is_placeholder() const` | E1 | — (the `kPending` marker) |
   | `learn_note_on` / `learn_cc` / `learn_program_change` / `unlearn` / `wants_learn_blink` / `learn_descriptor` | E2 task 8 | `MenuItem::learnNoteOn`/`learnCC`/`learnProgramChange`/`unlearnAction`/`shouldBlinkLearnLed`/`getLearningThing` |

   **`handle` runs first.** `MenuScene` gives a focused custom item every event before it looks at
   the shortcut-pad jump or the root `Back` (`menu_scene.cpp`), which is the precedence legacy gives
   a `MenuItem`'s own `buttonAction` over the rest of `SoundEditor::buttonAction`
   (`gui/ui/sound_editor.cpp:607 buttonAction`). Return an unconsumed `Result` and the scene
   proceeds as usual.

   **`on_focus` is the forward arm only.** It runs when the item becomes focused going *down*, and
   `MenuScene::ascend()` deliberately does **not** call it when a leaf is reached going *up* —
   including it broke the CV selector (E2 task 12). **E3 task 10** gave `CustomItem` the backward arm
   this note used to say was missing — `on_focus_backward` (`node.h`), which `ascend()` calls instead
   of its old blanket `Kind::Custom` exclusion. Do-nothing default; `GateSelectionItem`
   (`nodes/dynamic.cpp`) is the one item that overrides it, because "leave `MenuState` alone" (what
   the old exclusion gave every `Kind::Custom` item for free) is wrong for it specifically — see the
   item's own doc for why.

   **Two of the learn virtuals are routed now; one is not, and all of them are inert.** F1 task 10
   gave `MenuScene::learn_note_on` and `learn_program_change` producers —
   `ShellHostUI::noteOnReceivedForMidiLearn` / `pcReceivedForMidiLearn`, the two `UI` virtuals
   legacy's dispatchers ask (`gui/ui/ui.h:130`, `:132`), reached through
   `gui/ui/shell_guest_services.h`. **`learn_cc` still has none**, because
   `PlaybackHandler::midiCCReceived` reaches learn through a hardcoded
   `getCurrentUI() == &soundEditor` (`playback/playback_handler.cpp:3164`) rather than a virtual, and
   adding one is a legacy-behaviour change F1 does not make. And **all of them are unreachable in the
   shipped firmware today**, because every learn dispatcher is gated on
   `currentUIMode == UI_MODE_MIDI_LEARN` and nothing under the guest sets it. **Two assignments,
   three entries:** `View::startMIDILearn` (`gui/views/view.cpp:493`) and
   `SoundEditor::buttonAction`'s `LEARN` arm (`gui/ui/sound_editor.cpp:520`) are the only
   assignments, and the first has two callers — `View::buttonAction`'s `LEARN` arm (`view.cpp:192`)
   and `SessionView::enterMidiLearnMode` (`gui/views/session_view.cpp:3072-3076`), reached from the
   song context menu (`gui/context_menu/midi_learn_mode.cpp:51`). None of the three runs while the
   guest is the current UI, and the third one's *port* is
   `MenuScene::take_action`'s `song_midi_learn`, which nothing polls (migration §10.2 item 3). See
   `gui/ui/shell_host_ui.h`'s `@par MIDI learn`.
   `bindings::learn_target` is a *different* path — PARAM learn, not global-command learn.
2. Point the node at a `static`/`inline const` instance via the `custom(name, &item, relevant)`
   builder. A null `custom` pointer is itself a validator error (`Issue::Kind::NullCustom`) — there
   is no "no-op custom" shorthand.
3. **If the real implementation is a later task's job** (E1 shipped the *shape* of several bespoke
   items — the file selector, the audio recorder, loop points, patch-cable pages, the
   multi-range picker — without their real behaviour; E2 built all but the file/sample family,
   which needs the card, and E3 tasks 6 and 7 built the whole of that family), point the node at the
   shared `nodes::kPending` placeholder (`menu/nodes/sound_osc.{h,cpp}`) instead of a real
   implementation. `kPending` draws the item's title and consumes no input;
   `CustomItem::is_placeholder()` reports `true` for it, so `validate_tree` flags every node
   pointing there as `Issue::Kind::PlaceholderCustom`.

   **That report can no longer be turned off.** `ValidateOptions::allow_placeholder_custom` existed
   so an E1/E2 spec could say out loud, in one place, that the tree was not yet complete; with
   `kPendingCustomNodes` at 0 it could never suppress anything again, and a permanently-satisfiable
   opt-out is the shape that hides the *next* regression rather than documenting this one, so E3
   task 7 deleted it. The placeholder mechanism itself stays — a table that declares a shape before
   its item exists still gets `kPending`; it just cannot ask for silence, and the spec run says so
   until the item lands.

### `MenuState`'s one-shot mailboxes, and the order `after_render` drains them

A hook that has no `Result` of its own — a `CustomItem`'s `on_focus`, `handle` or `render`, a
widget's response — asks for things by writing a field on `MenuState`, and `MenuScene::after_render`
performs the request and clears the slot. There are six such mailboxes (`range_choice`,
`source_choice`, `midi_cable_choice`, the `value_changed`/`value_node` pair, `pending_action`,
`pending_scene`), plus the `range_picker_pending`/`pending_after_range` handshake, which is not one.

**The order they drain in is load-bearing, and the contract is written on `MenuState` itself**
(`menu/menu_scene.h`, "The one-shot mailboxes, and the order `after_render` must drain them in"),
with a per-band table. The one-line version: `ctx_` mutations first, then navigation, then the
repaint decision, then scene pushes — because a descent decided by `select_chain` reads the
`MenuContext` the mailbox above it just wrote. `sim/scenes_spec/tables_3_spec.cpp`'s *drains the
selector's output index BEFORE the navigation arm, not after* is the spec that fails if
`source_choice` moves, which is the case where it actually bites; the others are ordered by the
argument in the header rather than by a test, which is why the argument is written down.

### The relevance registry (`menu/relevance.h`, `relevance.cpp`)

Every `Node::relevant` predicate is a plain `bool(const MenuContext&)` function pointer — no
virtual dispatch, no per-node closure. `relevance::name_of(RelevantFn)` is the reverse lookup Task
10's `deluge_menu_dump` needs to print *which* predicate a node uses (not just that it has one): a
table of `{predicate, name}` rows, guarded by a `static_assert` on the row count so adding a
predicate without adding its row is a build failure rather than a dump that silently prints `"?"`.
`name_of(nullptr)` answers `"always"` — a node with no `relevant` hook is unconditionally relevant.

Sixty-two rows as of E3 task 9. Sixty-one of them ask about the **model** — the sound's synth mode,
the clip's shape, a mod-FX type. The sixty-second, `is_session_view`, is the one that asks about the
**UI**, and it is the exception that proves task-3 ruling 2 rather than a hole in it: it reads
`MenuContext::is_session_view`, a field the caller fills from `context_for`'s `session_view_root`
argument, exactly as the other three UI facts are reduced to arguments. `getRootUI()` itself stays
outside this library. See the session-vs-arranger note below.

### The validator (`menu/validate.h`, `validate.cpp`)

`validate_tree` walks every node reachable from a set of roots and checks: every `Submenu` is
non-empty under at least one context; every `ParamRef` resolves for its context; every `Enum` has
`names` or `dynamic_names`; every shortcut cell points at a node; `Custom` count ≤ `kMaxCustomNodes` (`validate.h`) — counting the *nodes* that point at a
custom item, not the distinct `CustomItem` instances — a distinction that mattered while eleven
nodes shared the single `nodes::kPending` placeholder and made the instance count 1 for the whole
tree; since E3 task 7 there are no placeholders and the two counts are equal, but the rule still
counts nodes because that is the thing the cap is about (see "Adding a `Custom`" above and the
design spec's amendment); no
`NullCustom`; no `DuplicateName` among siblings that could actually both be on screen at once (a
co-visibility qualifier — see the design spec's amendment for the nine blessed `FormattedTitle`
collisions this exists to allow); `Node::range_dependent` and `Binding::scaling ==
Scaling::HalfPrecision` are each set on exactly the caller-supplied expected set, no more and no
less (`ValidateOptions::expected_range_dependent`, `expected_half_precision`); and
`Binding::updates_reverb_params` never coexists with a `set` hook on the same node
(`Issue::Kind::FlagMismatch`, detail `Flag::ReverbUpdateWithHook` — a `set` hook runs and returns
before the reverb-update branch is ever reached, so the flag would silently never fire). Every
`Issue` names its node and a `Kind`. `ValidateOptions` has one opt-out left, `skip_shortcut_sweep`,
for a caller validating a *subtree*; `allow_placeholder_custom` was deleted with the last
placeholder (E3 task 7), so `Issue::Kind::PlaceholderCustom` is unconditional.

### The node-id registry (`menu/node_id.h`, `node_id.cpp`)

`assign_node_ids(roots)` walks the tree depth-first (pre-order) and gives every *statically*
reachable node an id, 1..n (0 means "no node"); `node_id(n)` and `node_by_id(id)` are the two
directions of the lookup. **It is called for the application by `scenes::assigned_node_ids()`, from
`menu_scene()` and `scene_registry()`** (`registry.cpp`) — before this it had no non-spec caller at
all, which left `MenuScene::save_state`, `restore_state` and the shortcut-pad jump each silently
no-opping on an unnumbered tree (fix round 2, M1). A spec that re-numbers a subset afterwards
overwrites that, which is intended.

**Its `.bss` cost, on the record** (rule 3 is "no heap", which makes static footprint the thing to
watch): two arrays sized by `kMaxNodes == 1024` — a `kMaxNodes * 2`-slot open-addressed table of
`uint16_t` ids plus the `kMaxNodes`-entry id→node array. On the 32-bit target that is
`2048 * 2 + 1024 * 4` = **8 KB** to index the 477 nodes the tree actually has
(`assigned_node_ids()`'s return value, over `MenuScene::roots()`). It was 20 KB until
fix round 2's M5: each slot used to store `{const Node*, uint16_t}`, 8 bytes after padding, when the
node is already recoverable as `g_by_id[id - 1]`. Shrinking `kMaxNodes` itself would halve it again
and is the next lever if the device budget needs it.

The registry exists at all because a menu position has to cross a scene boundary as a `SceneId`,
which is only 16 bits (spec D §3) and cannot hold a pointer: a shortcut-pad jump
(`MenuScene::handle`) pushes `Action::Kind::Replace` with `SceneId{SceneKind::FirstApp, node_id}`,
and `Shell::state_lines()` spells the currently-open node as `app16/<id>` in a `.state` golden line
— so a level-2 script's snapshot names *which* node is open by its stable id, not by a path a table
edit could silently repoint. Only statically-declared children (`Node::children`) are walked; a
`dynamic_children` node's children are never reachable here and are never addressable by id (see
the dump's own fail-closed behaviour below for the same limitation from another angle).

### The dump and its CI diff (`menu/menu_dump_main.cpp`, `deluge_menu_dump`)

`deluge_menu_dump` is a headless host tool (`sim/CMakeLists.txt`, no hardware) that walks the tree
under every `MenuContext::Root` and prints it as JSON, close to but not identical to the retired
Python DSL's (`gui/menu_item/generate`, `dmui/visitor/doc_emitter.py`) schema: every entry's
`"description"` key is **structurally absent**, not present-but-empty (`Node` carries no such
data), and a node with `Node::dynamic_children` set makes the dump **fail closed** — it prints the
offending node to stderr and exits non-zero rather than invent a `"multicontext"` shape, because the
recovered `doc_emitter.py`'s own `"multicontext"` entry has no `"children"` key at all, so any
invented shape would not even match the schema it claims to preserve. E2 task 10 went looking for
the first real `dynamic_children` node and found none — the MIDI device list and the CV/gate output
selectors, the three the plan expected, are all self-drawing `CustomItem`s
(`menu/nodes/dynamic.h`), because a submenu row is labelled from one static l10n string id and a
device row's label is a runtime string. The count is pinned at zero by a spec, so the arm still
guards a case nothing produces. `scripts/menu_dump_check.sh`
is the CI regression net: it rebuilds `deluge_menu_dump`, regenerates
`website/src/data/generated/menu-hierarchies.json` from its output, and fails loudly if that changes
anything already committed — i.e. the declarative tree drifted from the JSON a `.state` golden's
`app16/<id>` entries are meant to resolve against. Run it locally after configuring `build-sim`
with `-DDELUGE_SIM_X64=ON`.

### The two-profile arrangement, restated

As above (`profile.h`): `scenes::deluge_profile()` is `ui::shell::deluge_profile()` copied
field-for-field with `.text`/`.error_text` filled in from the l10n bridge, because `src/shell` may
not include `gui/l10n/` and `src/scenes` may. `profile_spec.cpp` (`sim/scenes_spec/`) is the drift
guard: it asserts every field *other* than the two l10n hooks stays byte-identical to the shell's
own profile (spans compared by `.data()`/`.size()`), so the two copies cannot silently diverge on
anything but the one thing they are meant to differ on. `deluge_scenes` and every scenes spec pass
`scenes::deluge_profile()`, never `ui::shell::deluge_profile()` directly.

## The rename editor (`rename/`, E2 task 5)

`rename::RenameScene` is one scene in place of legacy's five-class `gui/ui/rename/` directory
(`RenameUI` plus `RenameClipUI`/`RenameOutputUI`/`RenameDrumUI`/`RenameMidiCCUI` over `QwertyUI`).
The four subclasses differed only in three virtuals — `getCurrentName()`, `trySetName()` and
`allowEmpty()`/`canRename()` — so those become `rename::Target`, a plain struct in a table indexed
by `SceneId::arg`. It is registered at `SceneKind::FirstApp + 1` (`rename::kRenameSceneKind`,
`registry.cpp`).

Three targets, not four: `Clip`, `Output` and `Drum` are what the ported tree reaches
(`nodes::edit_clip_name`, `edit_track_name`, `drum_name_edit`). `renameMidiCCUI` is opened only by
`gui/views/automation_view.cpp:1663` and no `Node` reaches it, so it is deliberately absent rather
than shipped untested.

The pixels are the toolkit's: `ui::grid::text_keyboard` is `QwertyUI::drawKeys` and
`ui::oled::text_edit` is `QwertyUI::drawTextForOLEDEditing`, both already ported; the box is
`ui::oled::popup_frame` in its square-cornered `Permanent` style. The scene owns only the *input*
half of `QwertyUI` (the typing rules, `processBackspace`, `horizontalEncoderAction`) and
`RenameUI`'s buttons, including legacy's backspace auto-repeat: a press deletes one character and
arms `kBackspaceRepeatDelayMs` (500), then `kBackspaceRepeatMs` (80) per repeat, which are legacy's
own two `TimerName::UI_SPECIFIC` values (`qwerty_ui.cpp:220,452`). The hold is read from
`Response::held_index` — the *level* under `index`'s edge, added to the shared `Response` and filled
by `text_keyboard` under the same "first pad that types a character" rule, so E3's browser gets the
same hold without a second scene-local mechanism.

### How a menu item opens a scene

`nodes::EditNameItem` is the port of `menu_item::EditName`, whose whole body is a `beginSession`
override that calls `openUI(renameUI)`. `CustomItem::on_focus` *is* `beginSession`, but it returns
`void` — and must, because it is also reached from `retarget()` and `restore_state()`, neither of
which has a `Result` to put an `Action` in. So the item writes the request into
`MenuState::pending_scene` and `MenuScene::after_render` — the first hook after `enter_leaf` that
does have a `Result` — drains it into an `Action::Kind::Push`. It sets `MenuState::go_up_on_resume`
at the same moment, which is legacy's `soundEditor.shouldGoUpOneLevelOnBegin`
(`edit_name.cpp:50`, read by `SoundEditor::focusRegained`, `sound_editor.cpp:318-321`): without it,
closing the pushed scene would land back on the item and `on_focus` would open it again.

This is the pattern for every other bespoke item that opens a UI (the file selector, the sample
marker editor, the loop points, the DX browser, the audio recorder) — all five set the same legacy
flag beside the same `openUI` call.

#### …and how an item says its `on_focus` is really a *press*

`on_focus` is `beginSession`, and `MenuScene` runs it on **every** focus transition: a descent from
the parent list, a `SceneId` deep link, the shortcut-pad jump, and `restore_state` rebuilding a saved
path. Legacy runs `beginSession` on all of those too (`SoundEditor::focusRegained` → `beginScreen`,
`sound_editor.cpp:330`, `:1015`), so for the items above the port matches and there is nothing to do.

It does **not** match for an item whose legacy body is `selectButtonPress` rather than
`beginSession` — legacy never navigates into such an item at all (`shouldEnterSubmenu() == false`),
so the port descends for one tick and acts from `on_focus` instead (`menu/nodes/actions.h`). That is
right for a press and wrong for every other way in. E3 task 7 found it live: restoring a menu parked
on `midi_device_linked` replayed the toggle and **cleared the user's linked definition file**, and a
deep link onto `start_stem_export` re-filed the export. Legacy guards exactly this —
`Toggle::selectButtonPress` returns without toggling when `soundEditor.getCurrentMenuItem() == this`
(`gui/menu_item/toggle.h:35-37`), i.e. when it was reached without a press on its row.

The port of that guard is two pieces: `MenuScene::enter_leaf` takes a `FocusCause`
(`RowPress` from `descend_to`, `NoRowPress` from `retarget`, `restore_state` and `ascend` — the
values are named for the question they answer, "a select press on **this node's own row**?", because
a shortcut-pad jump is a real press and the old `Restored` said otherwise), and
`CustomItem::on_focus_is_press()` says which items it applies to. Both are needed — a blanket skip on
restore would regress the shipped ruling above (`EditNameItem` *must* reopen its editor), and a flag
with no cause cannot tell a press from a rebuild. **The line is "does it act without a press", not
"does it do anything":** opening a scene the user can back out of keeps the flag false; writing the
model or firing a one-shot verb sets it. Two items set it today, `MidiDeviceLinkedItem` and
`ActionItem`.

**Three names, two questions** — `on_focus`, `on_focus_backward` and `FocusCause` are easy to
mistake for three spellings of one mechanism. `CustomItem`'s doc in `menu/node.h` has the table a
contributor needs: the two virtuals answer *which direction focus arrived from* (and legacy's own
`beginSession` arms share no code, which is why they are two hooks rather than one hook and an
argument), while `FocusCause` answers *was there a press on this row* — a question whose answer is
never "run a different body", only "run the body you have, or don't".

## The context menu (`context_menu/`, E2 tasks 6, 7 and 11b)

`context_menu::ContextMenuScene` is the port of legacy's `gui/context_menu/` family — a modal
option window over a greyed-out grid. It is registered at `SceneKind::FirstApp + 2`
(`context_menu::kContextMenuSceneKind`, `registry.cpp`).

Legacy's `ContextMenu` is a base class with a subclass per menu; the port is **one scene plus a
descriptor table**. A `Descriptor` (`context_menu_scene.h`) carries the title, the option strings,
the accept hook and — since task 11b — three more hooks the audio-source selector needed
(`context_menu_scene.h`). `DescriptorId` indexes it, and `SceneId::arg` carries the id, so opening
a context menu is `Action::Kind::Push` with an argument and nothing more.

Six descriptors ship: the five `reset_settings::Reset` actions (`gui/menu_item/reset_settings/reset.h:25`)
and `audio_clip::AudioSourceSelector`. `AudioSourceSelector` was deliberately left out of task 6 and
added in 11b once `Descriptor` grew the hooks it needs — the header records why rather than
pretending the shape always fitted.

Two things about the pixels are worth knowing before you add a descriptor:

* The window is `ui::oled::option_window` (`ui/oled/popup.h`), **not** `ui::oled::list`. It is a
  separate widget on measured grounds, not convenience: legacy insets the label by 1 where `list`
  uses `style.inset_x` (6), derives the highlight extent as 106 where `list` derives it from the
  label's own rect (101), and pins the cursor to the **bottom** row where `list_window_start`
  centres it. Parameterising `list` would mean disabling most of what `list` is.
* The box is a fixed 100×40 square in `PopupStyle::Permanent`, drawn by `popup_frame`, and the
  title is a plain left `drawString` at x 22 — *not* `ui::oled::title`. Legacy's own
  `renderOLED` is the reference; check it before assuming a toolkit widget fits.

## The file browser (`browser/`, E3 tasks 1-3)

`browser::BrowserScene` is **one** file browser in place of legacy's `Browser` base class plus a
subclass per browser, registered at `SceneKind::FirstApp + 3` (`browser::kBrowserSceneKind`). It is
the same table-not-hierarchy shape as `rename::Target` and `context_menu::Descriptor`: everything
two browsers differ in — title, extensions, where the listing starts, which icon a row draws, what
accepting a file does — is a `Descriptor`, and `SceneId::arg` carries a `BrowserId`.

**Two descriptors ship, and they are the two the *menu tree* reaches**: `BrowserId::Sample`
(`sampleBrowser`, opened by the sample-file items) and `BrowserId::MidiDeviceDefinition`
(`loadMidiDeviceDefinitionUI`, opened by `midi_device_linked`). The load/save family — songs,
presets, patterns, kit rows — is **not** here and is not an oversight; see "E4 roadmap" below.

Two things a new descriptor author should know before writing one:

* **`accept` has three outcomes, not two.** `Close` and `Stay` are legacy's own; `Defer` exists
  because exactly one thing a browser does cannot be done from this layer —
  `SampleBrowser::claimCurrentFile` reaches into the sound and the audio-file manager through paths
  `src/scenes` may not name. A deferring descriptor parks the choice in `take_choice()` and closes,
  and the dispatcher that drains it owes two more things legacy does at the same moment: the
  `claimCurrentFile` work itself, and the `maybeGetParentMenu` re-rooting that lands the user on the
  source page rather than back on the item that opened the browser. Both are stated on
  `Descriptor::accept` and both are carried below.
* **`SampleBrowser::lastFilePathLoaded` is dropped, not mirrored.** It is written *inside*
  `claimCurrentFile()`, which this layer cannot perform, so a mirror would be stale from the first
  load. Named at the site.

### The async listing seam (`browser/listing.h`, E3 task 2), and `FileCatalogue::claim`

The SD half of legacy `Browser` is not in the scene. Legacy reads a directory **synchronously**, from
inside the UI's own call stack; a scene may not block a tick, so the read became a seam:

* `storage::FileCatalogue` (`storage/file_catalogue.{h,cpp}`, E3 task 1) is legacy's listing code
  **moved verbatim** out of `gui/ui/browser/browser.cpp`. It had to move: `src/scenes` may not
  include `gui/`, so the code was simply unreachable where it was. It went to `storage/` rather than
  anywhere else because that is what it is — the file catalogue — and because `check_layer_rules`'s
  own `NON_UI` glob already counts `src/deluge/storage/` as non-UI, so the extraction inherited the
  no-`gui/`, no-`hid/` rules for free. Legacy `Browser` now delegates into it; the equivalence is
  pinned by `sim/scenes_spec/file_catalogue_spec.cpp`, a characterisation spec written against
  **legacy** and re-run unchanged after the move.
* `browser::Listing` is the request/poll seam over it: `request()`, then `poll()` on later ticks,
  then `take_result()`. A scene owns one as a member and drives it from `after_render`.

**The refusal contract is the part to read before adding a second one.** Delivery is exactly-once
over a three-state machine (`Idle` / `InFlight` / `Ready`), and a request is **refused** rather than
overwritten — `Busy` if anyone holds the catalogue claim, `ResultPending` if a landed result has not
been collected. That is deliberately *not* the one-slot silent-overwrite mailbox `take_action()` is:
there is one `FileCatalogue::fileItems` vector, so an overwrite would interleave two directory reads
over one buffer. The trade is stated as **"a jam, not a loss"**, and it is mitigated by polling every
tick. There is **no cancel**, and `~Listing` asserts it is not in flight — so own one as a scene
member and nothing else. (Being *popped* does not destroy a scene: scenes are function-local statics
and the stack stores `SceneId` values, so `on_exit` is the only thing that runs.)

**The single-flight guard lives on the resource, not on the seam.** `FileCatalogue::claim(owner)` /
`release(owner)` is a CAS from `nullptr` with an owner-checked release, and **both** dispatch paths
claim through it — `browser::Listing::request` *and* legacy `Browser::beginListing`, whose own
`static listingInProgress_` was deleted in favour of it. A member guard would not have worked: the
resource is process-global, and during the coexistence a legacy browser listing and a scenes listing
are otherwise mutually invisible over one shared vector. The contract, stated once on `claim()`: hold
from dispatch until **the consumer has finished reading `fileItems`**, not until the read ends —
which is why the two sides release at different moments. Legacy consumes *inside* the op, so it
releases on completion; the seam counts entries rather than copying them, so it releases on
`take_result()`, one or more ticks later. `scripts/check_layer_rules.py`'s
`scenes-listing-through-the-seam` row is what keeps a scene from dispatching around the claim.

Two honest limits: `emptyFileItems` and friends do **not** check the claim — several legacy paths
call them directly and enforcing would break every one of them, so the claim binds the two *dispatch*
paths and nothing else. And the claim covers a **listing**, not a browsing *session*: the leaf calls
(`predictExtendedText`, `getUnusedSlot`, the save UIs' default-name search) do not claim at all.
Closing that means claim-on-open/release-on-close and a change to `Browser`'s lifecycle — E4's, with
the load/save family.

> `Owner::run` is inline on the host (`storage/owner.h`), so **no spec in this layer exercises real
> fiber concurrency across this seam**: a request lands before `request()` returns, and the specs
> drive the state machine rather than a race. `src/bsp/rust/tests/owner_host.rs` and the TSan entry
> point are where the real `deluge_worker_run` seam is driven.

## The sample marker editor (`marker/`, E3 task 4)

`marker::MarkerScene` is the port of `gui/ui/sample_marker_editor.{h,cpp}` — the start/end/loop
marker editor the four loop-point items and the audio clip's waveform item open. Registered at
`SceneKind::FirstApp + 4`; `SceneId::arg` is the `MarkerType` to start on.

**There is no OLED waveform.** The plan asserted one and was wrong: legacy's `renderOLED` draws a
title, a lock badge, a duration and a sample count — the waveform is grid-only. So the scene's OLED
half is `ui::oled::sample_position` (text and badges) and its grid half is `ui::grid::markers` over
the extracted scan.

That extraction is the second one this plan needed, and for the same reason as the first: the peak
scan and the scroll/zoom navigator lived in `gui/waveform/`, which neither `src/scenes` nor `src/ui`
may include. They moved verbatim to `model/sample/waveform_scan.h` and
`model/sample/waveform_navigator.h`, with the split line **arithmetic down, `PadLEDs` up**: legacy's
`WaveformBasicNavigator` now derives from `deluge::sample::WaveformNav` and keeps only the `PadLEDs`
zoom transition. The *drawing* stayed in `gui/waveform/waveform_renderer.h`, which is why
`scenes-no-legacy-waveform` exists. `model/clip/audio_clip.cpp` still includes the legacy renderer and
legitimately so — `AudioClip::renderAsSingleRow` writes into an `RGB*`, i.e. a `model/` class that
renders — and closing that means moving clip-row rendering into `SessionScene`, recorded at the
include site and carried below.

Deliberately not ported, stated at the sites rather than silently dropped: the collapse animation
(its only caller is `PadLEDs::renderExplodeAnimation`, driven by session/arranger, so it is
unreachable from here), the zoom transition, and the playhead. One legacy render bug is **fixed in
the replacement and recorded, not patched in legacy**: `renderOLED` draws the h/m unit letters at a
fixed `x = 6`, over their own digits, past a minute. `ui/oled/sample_position.cpp` pins both
behaviours with a `fix_unit_letter_x` flag.

## The audio recorder (`recorder/`, E3 task 5)

`recorder::RecorderScene` is the port of `gui/ui/audio_recorder.{h,cpp}`'s **UI** half — the
record-to-sample flow the two `record_to_sample` items open. Registered at `SceneKind::FirstApp + 5`.
The output-recording half (resampling, stem export) is not a UI and stays on the legacy object.

The interesting part is what replaced `AudioRecorder::process()`: **an infinite loop in the menu
item's own stack frame**. That became the shell's tick plus a poll in `after_render`, woken by
`next_timer()`. The cadence drops from as-fast-as-the-CPU-allows to 10 ms, which is accepted; the SD
side keeps turning because `doRecorderCardRoutines` is reached from `AudioEngine::routine`/
`slowRoutine`, not from `process()`.

**`AudioEngine::recordingSource` is published, not re-derived, and that distinction is the whole
lesson of the task.** `AudioRecorder::opened()` sets `recordingSource` and **nine** subsystems read
it as "the mic/line recorder UI is live" — most consequentially `setMonitoringMode`, which enables
`doMonitoring` only for the channels `opened()` sets. The port's first attempt respelled the
*condition* at one reader and left the other eight broken: input monitoring was dead for the whole
ported recording, so the user heard nothing while recording. The fix writes **the same storage all
nine observe**, through `AudioEngine::setForegroundRecording`/`foregroundRecordingSource`, in
legacy's own assignment order — no layer edge and no baseline row added, because `audio_engine.cpp`
already names `audioRecorder`. Writing it is also what turned up the use-after-free recorded below.

## The three services (`services/`, E4 tasks 1–5) — and why they are not in `src/shell/`

`services::ModKnobs`, `services::GlobalControls` and `services::MidiLearn` implement
`ui::shell::Service` (`src/shell/service.h`) and are the port of three of `View`'s concerns
(`gui/views/view.cpp`, 2,908 lines). A **service** is the shell's word for something that handles
routed events, declares LEDs and holds a timer but is never pushed and draws nothing — legacy's
`View` is exactly that shape, since `Buttons::buttonAction` runs a pre-UI section, then
`getCurrentUI()->buttonAction`, then a tail (`hid/buttons.cpp:75-235`).

| service | what it ports | LEDs it declares |
|---|---|---|
| `services::ModKnobs` | the `ModTarget`, gold-knob turns and their acceleration, mod-button mode, knob indicators (`View::modEncoderAction`, `modButtonAction`, `modEncoderButtonAction`, `setModLedStates`, `setKnobIndicatorLevel`) | the eight mod LEDs, both knob indicators |
| `services::GlobalControls` | transport buttons, the record combo, tempo encoder and button, tap tempo, sync scaling, the BACK long-press, undo/redo (`Buttons::buttonAction`'s tail plus `View::buttonAction`'s global arms) | Play, Record, TapTempo, SyncScaling, Save, Load, Clip |
| `services::MidiLearn` | learn mode, the pressed thing, the flash phase (`View::startMIDILearn`/`endMIDILearn`, the six `*MidiLearnPadPressed`, `noteOnReceivedForMidiLearn`, `ccReceivedForMIDILearn`, `midiLearnFlash`) | Learn, and it **overwrites** three LEDs `GlobalControls` declares |

**Why they are here and not in `src/shell/`, which is the architectural fact a contributor will
otherwise get wrong.** All three touch the model — a `Song`, a `ParamManager`, a `Clip`, a
`LearnedMIDI` — and `check_layer_rules.py`'s **`shell-includes-outside`** row forbids `src/shell/**`
from including anything but `shell/`, `ui/`, `gfx/`, `notify/` and `libdeluge/`. That row is not a
convention: it is what keeps the shell driveable by a fake surface set in a host process with no
model linked at all (`tests/spec/shell/`, 98 examples, none of which links `deluge_app`). So the
`Service` *interface* lives in the shell and every model-aware *implementation* lives here, exactly
as `Scene` does. There is no version of this where a service that reads a `Song` compiles under
`src/shell/`.

Three consequences worth knowing before adding a fourth service:

* **The router's order is top scene → `mod_knobs` → `global_controls` → `midi_learn`**
  (`shell/router.h`), and it was **reversed** until E4 task 3. Plan D put `mod_knobs` first; legacy
  is uniformly *current UI first, `View` as the base-class fallback* — `UI::modEncoderAction`,
  `UI::modButtonAction` and `UI::modEncoderButtonAction` are bare forwards to `view`
  (`gui/ui/ui.cpp:35-45`) that six, two and five UIs respectively override. With the service first,
  `SoundEditor::modEncoderAction`'s MIDI-learn interception could never have fired, which is a hole
  no spec in this library would have caught, because no spec drives a service and a scene competing
  for one event. `MidiLearn` is last on LED-composition grounds: `midiLearnFlash` overwrites three
  LEDs `GlobalControls` declares, so it must compose after them.
* **A service has no stack position**, so `Shell::tick`'s step 8c cannot narrow a service's repaint
  claim the way step 8b narrows a scene's (`Shell::oled_paintable`). Claiming `oled` from a service
  therefore arms *every* OLED panel every period, and an OLED surface blits unconditionally once it
  has rendered. `check_layer_rules.py`'s **`services-no-oled-timer-claim`** row is the tripwire; see
  "The `Service` half of the same fold" below.
* **No application can install one.** All three take the `Shell`'s own `SceneStack&` (and two of
  them its `InputLowering&`), `Services` is taken by value into the constructor and `service_list_`
  is built once — so a service cannot be built before the shell it serves and cannot be installed
  after it. This is the third spec-F wiring blocker and it is written up under "Boot wiring this
  layer does not do for itself".

## The four scene kinds E4 added

`FirstApp + 6` `overlay::ParamPopupScene`, `+ 7` `overlay::CountInScene`, `+ 8`
`slicing::SlicerScene`, `+ 9` `audio_clip::AudioClipScene`; plus `overlay::ScreensaverScene`, which
registers at the shell's own `SceneKind::Screensaver` rather than at an application kind (below).
The full numbered list, which is the authority, is `registry.h`'s file docstring.

The two overlays are the shell-owned `PopupScene`'s shape — `is_grid_overlay()` true and
`dims_grid()` false — and neither is a root. `SlicerScene` is the interesting predicate set and it
took two corrections to get right: `is_root()` **false** (`class Slicer final : public UI`, opened
*over* the sample browser), `is_grid_overlay()` true, `occludes_grid()` **false** (it overrides
`renderMainPads` only, so the sidebar beneath keeps painting), `oled_transparent(Main)` true, and
`dims_grid()` overridden to false because its default is `is_grid_overlay()` and that would have
halved a sidebar legacy never greys. `AudioClipScene` is the first `is_root() == true` scene in the
library, which is what finally makes `GlobalControls`' root-gated arms reachable.

## The screensaver (`overlay/screensaver_scene.h`, E4 task 7)

`overlay::ScreensaverScene` is the port of the **drawing** half of `hid::display::Screensaver`; the
policy half (idle deadline, wake on input) shipped earlier as `shell::Screensaver`
(`src/shell/screensaver.h`). Two things make it unlike the other two overlay scenes:

* **It registers at `SceneKind::Screensaver`, one of the shell's own kinds, not at
  `FirstApp + N`** — because the service pushes it by name (`shell/screensaver.cpp:60`). The shell
  overwrites the registry's `Popup`/`Console`/`Working`/`Loading` slots with its own instances and
  deliberately leaves this one alone (`shell/shell.cpp:176-183`). Registering it is what stops
  `shell::Screensaver`'s documented "a refused push latches the service off" ever firing on the
  shipped application; that behaviour is kept, because it is still right for a profile or spec that
  registers nothing there.
* **It is the only task in E4 that moved code out of `hid/`.** `Rainfall` and `Starfield` are now
  `ui/oled/rainfall.h` and `ui/oled/starfield.h`, because `src/scenes/**` may include neither `gui/`
  nor `hid/`. The move is E3 §15.2's pattern: legacy's `Screensaver` still draws the same two
  objects through the moved symbols, and the safety argument is a frame-by-frame comparison against
  a mechanical transcription of the pre-move source
  (`sim/scenes_spec/support/oracle_rainfall.{h,cpp}`, `oracle_starfield.{h,cpp}`), plus a pixel-for-
  pixel comparison of the assembled scene against the real legacy `Screensaver`'s own canvas.

Four legacy behaviours have no port and are recorded as gaps at the scene's own doc, all four being
policy rather than drawing: `settingsChanged()`, the permanent-popup / working-animation /
stem-export inhibitors (and with them the 1000 ms blank re-check that exists only to run them),
`ScreensaverMode::OFF` (ported as the service's `idle_ms == 0`), and the `consumeFrameDirty` blit
protocol (whose port is the shell's `RepaintGate`).

## The shared timeline state (`timeline/timeline.h`, E4 task 8)

`timeline::TimelineState` is the port of the **state and the pure arithmetic** in legacy's
three-class clip-view chain — `TimelineView` -> `ClipNavigationTimelineView` -> `ClipView`. It is
not a scene: it is a plain aggregate that a root scene owns, and `timeline.h` carries the
symbol-by-symbol table of what moved into it, what stayed with the scene, and what is dropped.

Four things about it are worth finding from here rather than from the header.

* **Only `AudioClipScene` consumes it.** The plan's prose calls it "the shared state every clip view
  needs", and it is — for the *clip views*. Legacy's `Slicer` is a plain `UI`
  (`gui/ui/slicer.h:33 class Slicer final : public UI`), not a `TimelineView`, so `SlicerScene`
  (E4 tasks 9-10) has no scroll or zoom to share. The header's table is the authority on who uses
  what.
* **`getNavSysId()` has no port.** Legacy keeps two navigation slots on `Song` (`xScroll[2]`,
  `xZoom[2]`) and every helper indexes them by a virtual. One `TimelineState` *is* one slot, so the
  virtual disappears; `adopt`/`publish` are the only two functions in this layer that spell
  `NAVIGATION_CLIP`, and they exist because `scenes-currentSong` forbids reaching for the song
  directly.
* **The `PadLEDs` scroll and zoom transitions are not carried**, the same call E3 made for the
  marker editor. `initiateXScroll`/`initiateXZoom` each set the scroll or zoom *and* start an
  animation; only the first half is here. That imposes a one-detent ceiling on the legacy oracle,
  which `sim/scenes_spec/timeline_spec.cpp` measures instead of working around — and **measures a
  difference from E3's outcome**: clearing `UI_MODE_HORIZONTAL_ZOOM` by hand buys the next detent
  here, proven by the scroll example landing on page two. No claim is made about *why* E3 saw
  otherwise; `WaveformBasicNavigator::zoom`'s tail
  (`gui/waveform/waveform_basic_navigator.cpp:64-69`) is the same three statements `initiateXZoom`
  ends in and contains no wait of its own.
* **Every horizontal turn clears the linear-recording restore point**, and that is easy to miss:
  `ClipNavigationTimelineView::horizontalEncoderAction` is two statements
  (`clip_navigation_timeline_view.cpp:30-34`), and the first one is the clear. `zoom()` and
  `scroll()` both call `stop_following()` because of it. Task 8's first round omitted it and the
  review caught it; without it a manual scroll during an auto-extending linear recording keeps a
  stale restore point and yanks the scroll back when recording stops.

## The slicer (`slicing/`, E4 tasks 9-10)

`slicing::SlicerScene` is the port of `Slicer` (`gui/ui/slicer.{h,cpp}`) -- the page the sample
browser opens once a sample has been chosen, where a kit is cut into equal regions or into slices
placed by hand. Task 9 built its state, its two renders and its three encoder arms; task 10 added
`padAction`, the rest of `buttonAction`, the audition preview and the slice commit.

Seven things are worth finding from here rather than from the header.

* **It is not a root scene, and it does not use `TimelineState`.** The plan's prose calls
  `SlicerScene` and `AudioClipScene` "the first two root scenes"; only the second is one.
  `class Slicer final : public UI` (`gui/ui/slicer.h:33`) -- a plain `UI`, opened over the sample
  browser, and `grep "public RootUI"` finds exactly two classes in the tree (`shell/scene.h:475-478`).
  So `is_root()` is false, `services::GlobalControls` correctly withholds the `View::buttonAction`
  group of global button arms from it (they are unreachable in legacy too, because
  `UI::buttonAction`'s base returns `NOT_DEALT_WITH` without forwarding to `view`,
  `gui/ui/ui.h:98-100`), and the scroll/zoom it navigates is a *sample's*, through its own
  `deluge::sample::WaveformNav`, not a timeline's.
* **`occludes_grid()` is false, not true.** The scene paints every MAIN pad -- so it is an overlay
  and not the grid base -- but `Slicer` overrides `renderMainPads` only. It has no `renderSidebar`,
  and `UI::renderSidebar`'s base returns false (`gui/ui/ui.h:148-151`), so the sidebar beneath keeps
  painting, exactly as it does under `MarkerScene`. `occludes_grid()`'s one consumer is
  `Shell::tick_stack`, which stops advancing clocks beneath a scene that claims it
  (`shell/shell.cpp:571-585`); claiming it here would stop the scene that owns the sidebar.
* **The directory and namespace are `slicing`, not `slicer`.** `scenes-no-root-ui` lists the legacy
  global `slicer` as a whole word, and `#include "scenes/slicer/slicer_scene.h"` contains that whole
  word. Measured with the path put back and the namespace left alone: **two violations, in two
  files, one line each** — this scene's own include and `registry.cpp`'s. (`count_file` skips
  comment-only lines, `check_layer_rules.py:849-853`, so the prose mentions never counted.) Two real
  include lines is not "the path is impossible"; it is "the row as written flags this path, and
  narrowing it to skip `#include` would preserve its stated purpose exactly". That narrowing is a
  change to a shared checker, so it is an open question for E4 task 14 and the local fix stands,
  ratified: Global Constraint 10 (the baseline may only shrink) against a plan file path, with the
  file names still the plan's. `SlicerScene` / `kSlicerSceneKind` / `slicer_scene()` are unaffected,
  because `\bslicer\b` does not match a name that continues into another word character.
* **The playhead is not ported, and the scene therefore has no timer.**
  `Slicer::graphicsRoutine` (`gui/ui/slicer.cpp:156-203`) is a per-tick redraw driven by audio, not
  a deadline: it walks the current kit's first drum for a sounding `VoiceSample` and hands rows 4-7
  to `PadLEDs::setTickSquares`. The *paint* half now exists in the toolkit
  (`ui::grid::tick_squares`), which closes the gap `MarkerScene` recorded for the same routine; the
  *walk* needs a live voice, which needs a running `AudioEngine`, and `sim/scenes_spec`'s fixtures
  build a `Song` by direct construction with none. Shipping the column arithmetic would mean
  shipping every branch of it unmeasured, so `next_timer()` stays `std::nullopt` and
  `timer_repaints()` is left at its default rather than claiming surfaces nothing repaints.

* **Everything the slicer does to the audio, the model and the card goes through `SliceOps`.**
  `Slicer` reaches its work through four `gui/` globals -- `sampleBrowser` (`:600`, `:749`),
  `soundEditor` (`:452`, `:612-614`, `:634`), `instrumentClipView` (`:481`, `:747`) and `PadLEDs`
  (`:378`) -- and `scenes-no-root-ui` / `scenes-includes-gui` forbid every one of them. The six calls
  that matter are collected into one interface (`slicing::SliceOps`), one virtual per legacy call
  site, and the scene keeps everything else: the slice table, the modes, the guards, the
  `LatestWins` coalescer, the `storage::Owner` dispatch and the re-entry gate. That split is what
  makes the port measurable -- the parts an oracle can compare against legacy are all on this side of
  it -- and it is the same seam `menu::model_stack_for()` and `browser::Listing` are. A null `ops()`
  is the honest default while nothing links `deluge_scenes` into an application; it is not a licence
  to leave a call out.
* **The commit does not need `RecorderScene`'s SD-routine guard, and that is a finding, not an
  omission.** `AudioEngine::sdRoutineBusy()` exists because freeing a `SampleRecorder` that is
  suspended part-way through its own `cardRoutine()` leaves that routine running on freed memory
  (`gui/ui/audio_recorder.cpp:168-176`). Three things say `Slicer::doSlice` is not that shape:
  `cardRoutine()` exists on exactly one class in this tree (`SampleRecorder`,
  `model/sample/sample_recorder.h:67`, pumped only by `AudioEngine::doRecorderCardRoutines`,
  `audio_engine.cpp:1682`) and `doSlice` frees none -- its one `delugeDealloc` (`slicer.cpp:703`)
  releases a `SoundDrum` allocated thirteen lines earlier, before `kit->addDrum` at `:711`, so
  nothing else holds it; the commit is reached only from a button press, which legacy already refuses
  from inside a card routine (`REMIND_ME_OUTSIDE_CARD_ROUTINE`, `:335-337`) and whose port is
  `InputLowering::set_sd_busy`; and the hazard `doSlice` *does* have is the one legacy names itself
  at `:353-362` -- re-entry of its own dispatch while the op is popping UIs -- guarded by the
  `currentUIMode` gate at `:363`, which the port carries as `SlicerScene::committing()`.
* **`on_resume()` is `focusRegained`, and the shell had to be corrected before it could be.**
  `closeUI` calls `focusRegained` on whatever is left on top (`gui/ui/ui.cpp:194-199`), so task 9
  re-ran it there. Task 10 measured that firing where legacy never would: a legacy popup is a
  *display overlay*, not a UI on `uiNavigationHierarchy`, while a shell popup **is** a scene --
  `Notices::show_popup_with_timeout` pushes `SceneKind::Popup` (`shell/notices.cpp:301-325`) and
  `Notices::tick` removes it once `kPopupTimeoutMs` (800 ms, `notices.cpp:31`) has passed
  (`notices.cpp:327-350`). `SceneStack::remove` used to notify the scene beneath whenever the scene
  it removed sat highest, so with the reset in place forty hand-placed slices vanished 800 ms after
  the user tapped a pad. The body was emptied for exactly one task and is **restored**: the fix was
  the shell's, and it landed as design spec §15.7 -- `SceneStack` now fires `on_resume()` only when
  `input_top()` changes hands, and an `input_transparent()` overlay never holds it.

  The shell defect had three parts, all now closed. It **over-notified**: removing a transparent
  layer announced a transition `input_top()` itself says did not happen. It **under-notified,
  symmetrically**: `pop`/`remove` resumed `stack_[n_ - 1]`, the raw top rather than the input top,
  so `[A, popup, B]` removing `B` notified the **popup** while `A`, which genuinely regains the
  input top, got nothing. And `Scene` had no `on_pause` for the edge to pair with -- deliberately
  still true, and now stated as a decision on `Scene::on_resume` rather than left open. `MenuScene`
  took the spurious edge too (`go_up_on_resume` guards only the `ascend()` at
  `menu_scene.cpp:332-335`; the tail resets five per-tick fields unconditionally at `:337-341`, and
  the scene raises `Notice::Kind::Console` itself at `:1256` and `menu/bindings.cpp:372`), but cost
  nothing by it: those five fields are drained at the end of every `after_render` (`:1358-1362`) and
  `render_oled` re-zeroes two of them at its own head (`:482-483`), while `Notices` is a `Service`
  and ticks at `Shell::tick` step 4 (`shell/shell.cpp:322-323`) -- before any render. They were already
  at their defaults every time the edge arrived.

  Both directions are asserted: `sim/scenes_spec/slicer_scene_spec.cpp`'s "keeps its slice table
  when a popup it raised itself expires" (a transparent layer must not reset it) and "resets its
  state when a real scene above it closes" (an opaque one must).

One transcription this port deliberately does **not** make, and it is the only one:
`renderMainPads`' MANUAL slice mark is `image[4][x]` with
`x = startPos / lengthInSamples * 16` (`slicer.cpp:123-128`), which is **16** -- the first sidebar
column -- once `horizontalEncoderAction`'s clamp (`:220-221`) has driven a slice to the end of the
sample. Legacy composes the grid top-down (`doAnyPendingGridRendering`, `gui/ui/ui.cpp:305-349`), so
the sidebar UI beneath overwrites that pad later in the same pass; the shell composes bottom-up
(`Shell::render_grid`, `shell/shell.cpp:700-816`), so reproducing the write would leave a stray pad
visible where legacy's is not. The port paints the marks inside the main-column region, which clips
it: behaviour preserved by clipping rather than by transcription.

## The audio clip view (`audio_clip/`, E4 tasks 11 and 12) — the first ROOT scene

`AudioClipScene` is the port of `AudioClipView` (`gui/views/audio_clip_view.{h,cpp}`), and it is the
first scene in this tree that is a **root view**: `AudioClipView` -> `ClipView` ->
`ClipNavigationTimelineView` -> `TimelineView : public RootUI` (`gui/views/timeline_view.h:27`), and
`grep "public RootUI"` finds exactly two classes in the whole tree. So `Scene::is_root()` returns
true here for the first time, and the group of legacy global button arms `services::GlobalControls`
gates on that predicate (`services/global_controls.cpp:580`) — `SAVE`, `LOAD`, `SYNC_SCALING`,
`TAP_TEMPO`, `BACK` and shift+`SELECT_ENC` — becomes reachable for the first time as well.

Three things about it that are not obvious from the class:

* **The play cursor is not painted by `renderMainPads`.** `AudioClipView::graphicsRoutine`
  (`audio_clip_view.cpp:263-311`) hands a column to `PadLEDs::setTickSquares`, and the *driver*
  substitutes the colour per pad on the way out, in `PadLEDs::prepareColour`
  (`hid/led/pad_leds.cpp:283-298`). The shell has one immediate-mode `render_grid`, so the port
  paints both halves there — the waveform, then `ui::grid::tick_squares` last — and the spec's
  oracle runs legacy's `graphicsRoutine`, `renderMainPads`, `renderSidebar` **and** `prepareColour`
  so the two are comparable at all.
* **It is not the voice walk tasks 9 and 10 could not drive.** That is `Slicer::graphicsRoutine`
  (`gui/ui/slicer.cpp:156-203`). This one reads `playbackHandler`, `Song::isClipActive` and
  `ClipView::getTickSquare`, all of which a spec drives, and the last of which is already
  `timeline::TimelineState::tick_square`.
* **`AudioClipView::renderSidebar` uses neither of `ui/grid/sidebar.h`'s colour helpers.** It fills
  both columns black (`audio_clip_view.cpp:245-246`) and, only while `UI_MODE_HOLDING_SONG_BUTTON`,
  draws `view.renderMacros` (`:247-249`). `status_pad`/`audition_pad` belong to the session and
  instrument-clip views; the entry point here is `ui::grid::sidebar_columns`. The macro arm is not
  ported: `view.clipArmFlashOn`, `matrixDriver.isPadPressed` and `view.flashPlayEnable()` are all
  outside this layer, `View::renderMacros` is shared with `SessionView`, and the mode that gates it
  is set by an arm E4 task 12 owns.

`ui::oled::output_name`'s `name_empty` branch draws a **slot**, which is legacy's substitution for an
unnamed MIDI or CV output (`view.cpp:2162-2187`). An `OutputType::AUDIO` output with no name matches
none of `drawOutputNameFromDetails`' three arms, so legacy draws the type text alone — the scene
therefore passes the empty name with `name_empty = false`, and the "unnamed output" example in
`sim/scenes_spec/audio_clip_scene_spec.cpp` is what found it.

### The spec constraint that makes a pad comparison vacuous, and the assertion that closes it

`deluge::sample::find_peaks_per_col` takes its cached whole-cluster arm only when a display column
spans two or more clusters (`model/sample/waveform_scan.cpp:220-230`); on any other arm it opens a
`SampleFrameReader`, which fails with no card mounted, sets `hadAnyTroubleLoading` and returns
**false**. Both sides then draw nothing — the port because `render_grid` returns, legacy because
`renderMainPads`' `image == PadLEDs::image` arm returns (`:151-154`) — and a pad-for-pad comparison
passes on two black grids. It was written that way first: a `loop_length` of 384 at `x_zoom = 48`
puts a column boundary 16 bytes from the end of the last cluster, and the "grey region" example
passed with an entirely black grid.

**It took two more rounds to fix properly, and the second is the first one level up.** Round one:
`expect_grid_has_content` was a separate call, it landed on eight of the twelve pad comparisons, and
the docs said "every render example" — the four it missed were the blink's second phase and the
three "no cursor is drawn" examples, *precisely* the shape where both sides legitimately go black.
Review demonstrated it by forcing a failed scan on the count-in example: 27/27 passing on two black
grids. Round two: the four were fixed, but the claim was then "every caller does X" with a one-off
script behind it — an absolute claim resting on a convention, in a file **E4 task 12 extends with
more pad comparisons**, so the next unguarded comparison would have been written by someone reading
a docstring saying the invariant already held.

**The invariant is now a property of the helper's signature, not a discipline.**
`expect_pads_match(self, r, legacy, expected)` takes a `PadExpectation` with no default and no
"nothing" value, applies it before comparing, and asserts its non-black floor is positive — so there
is no spelling of "compare these two grids" that does not also say what makes the comparison worth
making. The factories are `waveform(n)`, `waveform_with_cursor(n)` and `waveform_without_cursor(n)`
(the last being the sharper predicate the "no cursor" examples needed: the waveform IS there **and**
no pad carries the play cursor's colour, which can fail in both directions), with
`.and_column(x, colour)` for the blink's two phases. The fixture's `Sentinel` docstring carries the
cluster arithmetic and the whole history.

**Any later spec that compares a rendered grid against legacy wants this shape**: fold the
non-vacuity assertion into the comparison rather than asking callers to precede it with one.

### The edits (E4 task 12), and the three things they closed

Task 12 adds `buttonAction`, `padAction`, the two marker edits, the two length edits, the encoder
arms and `on_model_changed`. Four things about it are worth knowing without reading the class:

* **The `BACK` + horizontal-encoder collision is closed.** `AudioClipView` claims `BACK` for "clear
  clip" while `currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON`
  (`audio_clip_view.cpp:435-465`) and returns before the `else` at `:466`, so legacy's clip-clear
  wins over `View::buttonAction`'s undo. Task 11 made `is_root()` true without shipping that arm, so
  for one task the port undid where legacy cleared. `handle()`'s `Back` arm now claims it, and the
  spec asserts both directions: claimed with the mode set, **not** claimed without it, and not
  claimed when the song button is held too — because `:417` compares the whole word for equality.
* **The scene carries a two-value `currentUIMode`.** `UI_MODE_HOLDING_SONG_BUTTON` and
  `UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON` are set by this view's own button arms and read by its
  own encoder arms, so they are transcribed (`gui/ui/ui.h:79`, `:81`, `:89`) along with
  `enterUIMode`/`exitUIMode`/`isUIModeActive`'s exclusive-vs-flag arithmetic. **The mode is a latch,
  not a question about held buttons**: pressing `X_ENC` with shift held enters no mode at all
  (`audio_clip_view.cpp:394-397` does nothing and never delegates), which is exactly what leaves
  `horizontalEncoderAction`'s first arm reachable.
* **`stop_following()` landed one function lower than the ruling said.** Task 11 (and task 12's
  brief) placed it at "the first statement of the horizontal-encoder arm, above every gate", naming
  `AudioClipView::horizontalEncoderAction`. Legacy's clear is the first statement of
  `ClipNavigationTimelineView::horizontalEncoderAction`
  (`gui/views/clip_navigation_timeline_view.cpp:30-34`), which is reached only through the **third**
  arm of `ClipView::horizontalEncoderAction` — below the shift length edit, the `Y_ENC` shift and the
  `currentlyScrollableAndZoomable()` guard, none of which clear it. The higher placement would have
  been a behaviour change, not a placement detail. Both cases are specced: a turn the UI-mode gate
  refuses still clears, and a shift length edit still does not.
* **`setClipLengthEqualToSampleLength` is real, and nothing dispatches it.** It is the first of
  `menu/action.h`'s five parked `MenuAction`s to have a callee. The spec drives the method directly
  **and** through legacy's own `X_ENC`+`Y_ENC` chord; `MenuScene::take_action()` is still an outbox
  with no consumer, because the dispatcher is the wiring plan's.

Two spec-side facts that will bite the next author of this file:

* **`Button{Back}` never comes out of `InputLowering::next()`.** It is one of the widget-owned
  inputs held back from routing (`shell/input_lowering.h`'s "held-back split"), delivered after
  render from `held_back()`. A rig that drains only `next()` never calls `handle` with a `Back`
  event at all — and a "this is not claimed" assertion then passes for entirely the wrong reason.
* **`Buttons::buttonAction` cannot be called from a spec process**, because its first act after
  recording the press is `getCurrentUI()->buttonAction(...)` (`hid/buttons.cpp:134`) and
  `getCurrentUI()` is null with no UI open. Legacy's held-button state is set through bare `extern`s
  on `Buttons::buttonStates` / `shiftCurrentlyPressed` instead. `AudioClipView::focusRegained()` is
  out for the same class of reason: it reaches `view.focusRegained()`, which segfaults there.

## Repaint and the timer fold (`Shell::tick` step 8b, E3 tasks 10-11)

Two shell-level corrections this plan made, both of which change how a scene asks to be redrawn:

* **A timer now dirties the surfaces it names.** `Scene::next_timer()` used to mean only "wake me
  then" — it did not dirty anything, so a scene that ticked an animation forward had to *also*
  remember a matching `RepaintGate::request_repaint_after` from inside its own `render_*`. Three
  scenes shipped with that bug (E2's cursor blink, then the marker blink, then the recorder's
  completion poll, where it costs more than an animation: without the arm `after_render` never runs
  on a quiet tick and the recording never notices it finished). `Shell::tick` step 8b now folds the
  two together, arming, at a scene's own `next_timer()` deadline, the surfaces it claims **and can
  actually paint**.
* **"Can actually paint" is the whole of the third correction** (final review, A2). The first cut
  armed from every stacked scene, but the render functions draw from a base up — `render_grid` from
  the grid base (`Shell::grid_base`), `render_oled` from that role's lowest opaque scene
  (`Shell::oled_lowest`) — so a *covered* scene armed a gate it could never appear in. Two live
  costs: a covered `MenuScene` with source pads on its focused node kept the grid gate at 20 ms for
  the whole life of the scene above it, and `RecorderScene` under its own opaque clipping popup
  armed both OLED panels every 10 ms, which is a 100 Hz two-panel blit with no pixel changing. The
  per-surface mask below cannot catch either: the surface is the right surface, the scene is just
  not the drawn one. Step 8b and the render functions now ask the same two helpers, and
  `compute_next_wake` skips a scene only when an arm **actually happened** — a covered scene still
  needs its wake, because `tick_stack` advances its clock whatever the render pass decides.
* **The fold is per-surface, and all-or-nothing was wrong.** `Scene::timer_repaints()` returns
  `RepaintSurfaces{oled, grid}`. The base body returns both, and since the E4 final review the
  struct's own members default to **false**, so a designated initializer arms only what it names --
  `return {.grid = true};` no longer silently claims the OLED as well. It matters because **the two
  flushes are not
  alike**: the grid's diffs against `last_sent` and sends nothing when a repaint changed no pixel,
  while the OLED's blits unconditionally once rendered. So over-arming is *free* on the grid and a
  permanent cost on the OLED — and `MenuScene::next_timer` returns a constant 20 ms whenever the
  focused node has source shortcut pads, which under a blanket fold meant sitting on a patched-param
  leaf blitted **both panels at 50 Hz forever** on a battery device. `MenuScene` overrides
  `{oled = false, grid = true}`: its timer is the shortcut-pad blink cadence, a grid concern whose
  OLED content never depends on it. No test could see the regression, which is exactly why it was
  fixed rather than recorded.
* **`Action::Kind::RepaintAll` never reached the grid at all.** `shell/scene.h` documented it as
  "invalidate every surface", but `SurfaceRole` names OLED panels only and `Shell::tick`'s grid step
  consulted the grid's own gate and nothing else — and that gate is armed by `render_grid`, which has
  already happened by the time post-render input arrives. So a scene that changed what the *pads*
  show in response to a post-render event had no way to say so: `MarkerScene`'s select turn moved the
  marker on the OLED and left the pads up to a 200 ms blink phase behind. `Router::grid_repaint_requested()`
  is the fix; `BrowserScene::handle` had the identical latency.

### The `Service` half of the same fold (E4 task 5)

`Service::timer_repaints()` exists for the same reason `Scene::timer_repaints()` does, and it
**defaults the other way round** — `{false, false}`, arming nothing. Until E4 task 5, a service's
`next_timer()` fed `Shell::compute_next_wake` and nothing else; every service written before then
(`ModKnobs`, `GlobalControls`, `Notices`, `Screensaver`) was written on that understanding, so a
`{true, true}` default would have silently started arming both gates for four objects that never
asked. `Shell::tick` step 8c is the service loop, right after the scene loop.

`services::MidiLearn` is the one claimer, and it claims `{.oled = false, .grid = true}`. Legacy's
`View::midiLearnFlash` toggles `midiLearnFlashOn` and then asks the **root UI** to repaint
(`gui/views/view.cpp:814-819`, `getRootUI()->midiLearnFlash()`), and every reader of that bit in the
tree is a pad renderer — `gui/views/session_view.cpp:1548`, `:2550`, `:3237`,
`gui/views/instrument_clip_view.cpp:5992`, `:6036`, `gui/views/arranger_view.cpp:675`,
`gui/views/view.cpp:2636`. Arming an OLED surface at that 60 ms cadence would be a permanent
two-panel blit with no pixel changing, exactly as `MenuScene`'s blink was before its own override.

**One thing the service loop cannot do that the scene loop does:** narrow the arm to a participant
that can actually paint. `oled_paintable`/`grid_paintable` answer "is this scene at or above the
surface's base", and a service has no stack position — it draws nothing at all, and what its timer
arms is the repaint of whichever *scene* reads its state. So a service's claim is honoured
unconditionally, which is why the OLED half of it is the dangerous half and is documented as such on
the hook.

## Defects this port found in shipping firmware

Eleven, none of them introduced by the port, none of them fixed by it, and all eleven recorded at
their own site as well as here. They are listed together because a ledger is not where a release note
looks.

Five of the eleven are in the three files this stack cites most and **none of the five is annotated
in the legacy file**, for the reason the `browser.cpp` row below already gives. Two are in
`gui/views/view.cpp`, which is cited by line from `src/scenes/services/`,
`src/scenes/menu/context.cpp` and `sim/scenes_spec/` more than a hundred times; two are in
`gui/views/clip_view.cpp`, which E4 task 12's three files alone cite 35 times; and the eleventh is
in `gui/ui/sound_editor.cpp`, the file this whole library is a port of. Inserting a comment in any
of them would shift every one of those citations. Their port-side `@warning`s are complete.

| defect | severity | where it is recorded |
|---|---|---|
| **`Slicer::renderMainPads` never checks that it was given an image.** `UI`'s own header says every override must (*"These \*must\* check whether image has been supplied - if not, just return"*, `gui/ui/ui.h:141-142`) and its sibling `SampleMarkerEditor::renderMainPads` does (`gui/ui/sample_marker_editor.cpp:1362-1366`). `closeUI` quizzes every UI from the top down to and including the one being closed with the **no-argument** form (`gui/ui/ui.cpp:184-192`, `redrawMainPads \|= thisUI->renderMainPads()`), so `image` is null; nothing between that call and the write guards it. Closing the slicer takes that path on every exit -- `buttonAction`'s BACK arm calls `close()` (`slicer.cpp:391`), and so does the commit, through `sampleBrowser.exitAndNeverDeleteDrum()`. In REGION mode the null reaches `WaveformRenderer::renderFullScreen`'s clear (`gui/waveform/waveform_renderer.cpp:57-59`), which memsets 48 bytes at eight addresses from 0 to 432; in MANUAL mode it reaches the squash loop, the slice-mark loop and the slot-grid loop directly (`slicer.cpp:117-149`), and the squash loop additionally reads `myImage` uninitialised whenever the scan returned early (`waveform_renderer.cpp:52-54`, before its own clear). **Established by reading the enclosing braces**, not reproduced on a device; whether address 0 faults on an RZ/A1 depends on the boot-time memory map, which is why it has survived. The port has no equivalent -- `render_grid` is always handed a real frame -- and does not patch legacy. | **memory corruption / undefined behaviour, on every exit from the slicer** | `src/scenes/slicing/slicer_scene.h`'s "Two legacy defects" section |
| **`Slicer` declares 432 bytes of stack it never touches, twice.** `renderMainPads`' REGION arm opens with `RGB myImage[kDisplayHeight][kDisplayWidth + kSideBarWidth];` (`gui/ui/slicer.cpp:107`) and then renders into the caller's `image`, not into it (`:108-109`); `buttonAction`'s BACK arm declares the same array (`:376`) and then renders into `PadLEDs::image` (`:377-379`). Both are uninitialised, unread and unwritten -- 8 x 18 x 3 = 432 bytes of stack each, on a device whose UI runs on a small one. Almost certainly the residue of the MANUAL arm at `:113`, which is the one place the array is real. **Read the braces on both**: neither name appears again inside its own scope. Harmless as written, and not fixed here (a legacy edit this plan does not authorise); worth a line because a reader comparing the three arms will otherwise assume the two dead ones matter. The port has no counterpart -- `render_grid` snapshots what it needs from the frame and `button_action` renders nothing. | cosmetic / stack pressure | this row; `src/scenes/slicing/slicer_scene.cpp`'s `button_action` BACK comment |
| **`Slicer::selectEncoderAction`'s slice-count wrap only fires on an exact hit.** The REGION arm is `numClips += offset; if (numClips == 257) numClips = 2; else if (numClips == 1) numClips = 256;` (`gui/ui/slicer.cpp:249-257`) -- equality, not a range -- and `offset` is whole detents, so a fast turn delivers more than one at a time and steps straight over both ends. 255 + 2 is 257 and wraps; 256 + 2 is 258 and does not. Downward it is worse: from the opening 16, a single -20 turn lands on **-4**, and the page then reads "-4"; nothing between there and `doSlice(sliceCount)` re-checks the sign. Reachable with an ordinary flick of the select encoder. The port reproduces it exactly, including the `int16_t` width the wrap is written against, and `sim/scenes_spec/slicer_scene_spec.cpp` asserts both halves against legacy's own `selectEncoderAction` -- a one-sided fix would be a silent divergence, which is what every other example there exists to catch. | user-visible, not a crash | `src/scenes/slicing/slicer_scene.cpp`'s `select_encoder` comments; the "steps past the wrap" and "drive the REGION count negative" examples in `sim/scenes_spec/slicer_scene_spec.cpp` |
| **Use-after-free in `AudioRecorder::finishRecording`.** It frees through the member (`discardRecorder` → `~SampleRecorder` + `delugeDealloc`) and only *then* clears `recorder` and `recordingSource`. In that window the pointer dangles while the channel still says LEFT/STEREO — and `AudioEngine::setMonitoringMode`, reached from `renderAudio` on the **priority-0 audio task**, tests that pair and dereferences `audioRecorder.recorder`. The Rust scheduler routes priority 0 onto the **preemptive** SGI executor, so the interrupt can land on any instruction inside `discardRecorder`; the `recorder != nullptr` double-check does not help, because the pointer is non-null and freed for the whole window. `audio_engine.cpp`'s output/stem-recording paths read the same member the same way. The port takes the correct order (channel first, then pointer, then free, with a compiler barrier between the two stores) and does **not** patch legacy, because those members are written from five other legacy sites this port is not entitled to re-order. | **crash / memory corruption** | `gui/ui/audio_recorder.cpp`'s `finishRecording` @warning; `AudioEngine::setForegroundRecording`'s header contract |
| **`Browser`'s default-directory fallback is dead**, and so is `LoadInstrumentPresetUI`'s. Both test `Error::FOLDER_DOESNT_EXIST`, but the read path they take reports a missing directory as `Error::FILE_NOT_FOUND` — `FOLDER_DOESNT_EXIST` is produced in exactly one place in the tree, `storage_manager.cpp`'s file-*open* path, which neither call reaches. So the fallback, and `browser.cpp`'s mkdir arm behind it, cannot run. The port accepts **both** codes as a documented divergence rather than patching, because which folder a user should land in when one is missing is a product decision. | user-visible, not a crash | `gui/ui/load/load_instrument_preset_ui.cpp`'s @warning; `src/scenes/browser/listing.cpp`'s divergence note, which carries the full trace. Deliberately **not** annotated in `browser.cpp` itself: inserting lines there would shift ~100 legacy citations from this stack, and the port-side record is complete |
| **`View::modButtonAction` dereferences a pointer its own sibling null-checks — twice.** `ModControllable::getModKnobMode()` returns `nullptr` for anything that does not support mod-knob banks (`model/mod_controllable/mod_controllable.cpp:47-49`, whose declaration at `mod_controllable.h:55` says so in a comment: *"Return NULL if different modes not supported"*). `View::getModKnobMode` (`gui/views/view.cpp:1697-1706`) tests it before dereferencing. `View::modButtonAction` does not, at **`view.cpp:1534`** (a read, inside the `!rootUIIsClipMinderScreen()` arm) and again at **`:1549`** (a *write*, outside `!rootUIIsClipMinderScreen()` and therefore reached on a press in a clip-minder view, where `:1534` is not). Both are inside `if (…modControllable)` (`:1527`), `if (on)` (`:1528`) and the `isUIModeWithinRange(modButtonUIModes) \|\| rootUI == &performanceView` gate (`:1529`), past the automation-editor early return at `:1518-1522`, so a release and a press during any other UI mode reach neither. Six classes override the default — `GlobalEffectableForSong`, `SoundInstrument`, `SoundDrum`, `Kit`, `AudioOutput`, `MIDIInstrument` — and the one class in the tree that can be an active mod controllable and does **not** is `CVInstrument` (`model/instrument/cv_instrument.h:31`, a `NonAudioInstrument` whose `toModControllable()` returns `this`, `non_audio_instrument.h:57`). So the mechanism is: mod button pressed while a CV clip's knobs are the active target. **Established by reading, not reproduced** — no device here, and no fixture in `sim/` builds a CV clip. The port guards both sites; the guard can only differ from legacy in the case legacy faults in. | **crash (null dereference), if reachable as read** | `src/scenes/services/mod_knobs.cpp`'s `mod_button_action` @warning |
| **`View::setKnobIndicatorLevel` reads an uninitialised `int32_t`.** `gui/views/view.cpp:1427` declares `int32_t knobPos;` with no initialiser. Its three-way branch assigns it when the param has an `autoParam` (`:1430`), when `paramId == 255` (`:1471`), and when the descriptor is not just a param (`:1477`) — and leaves it **unassigned** on the fourth path, a param with no `autoParam`, a `paramId != 255`, and `(paramId & 0x0000FF00) == 0x0000FF00`. The value is then passed to `indicator_leds::setKnobIndicatorLevel` at `:1485`, which truncates it to `uint8_t` and lights the gold knob's four-segment column from it. Undefined behaviour with a visible symptom: an indicator column showing whatever was on the stack. The port declares **nothing** for that knob instead, which leaves the column as it was — the shell's own rule for an undeclared indicator (`Shell::flush`). | undefined behaviour; user-visible as a wrong indicator | `src/scenes/services/mod_knobs.cpp`'s `indicator_level` @warning; `src/scenes/services/mod_target.h`'s `ModIndicatorInfo` @warning |
| **`ClipView::getTickSquare` compares a scroll against the zoom.** The arm that stops following an auto-extending linear recording and restores the scroll it started from reads `if (newXScroll != currentSong->xZoom[NAVIGATION_CLIP])` (`gui/views/clip_view.cpp:382`). The value on the left is a **scroll**: it came from `xScrollBeforeFollowingAutoExtendingLinearRecording` (`:379`), which the follow arm eleven lines up filled from `currentSong->xScroll[NAVIGATION_CLIP]` (`:366`). Comparing it against `xZoom` is a comparison between two unrelated quantities, and the guard's evident purpose — skip a restore that would move nothing — needs `xScroll`. Two consequences, both live: the restore is **skipped** whenever the remembered scroll happens to equal the current zoom, leaving the view wherever the follow pushed it instead of back where the user was; and it is **performed** in the case the guard exists to skip, `newXScroll == xScroll`, costing a redraw (and, when no popup is up, a scroll-position popup) for a move of zero. Reached whenever a linear recording auto-extends past the right edge and then stops — the ordinary case for recording a clip longer than one screen. **Established by reading, and by driving legacy's own `getTickSquare` from both sides**; not reproduced on a device. The port reproduces it rather than fixing it, because every other example in `sim/scenes_spec/timeline_spec.cpp` asserts that the port and legacy agree, and a one-sided fix would be a silent divergence. | user-visible, not a crash | `src/scenes/timeline/timeline.h`'s `tick_square` @warning; `src/scenes/timeline/timeline.cpp`'s file comment; the two-sided assertion in `sim/scenes_spec/timeline_spec.cpp`. Deliberately **not** annotated in `clip_view.cpp` itself: this task's three files alone cite it by line 35 times, and inserting a comment would shift every one |
| **`ClipView::lengthenClip` and `shortenClip` dereference `actionLogger.firstAction[BEFORE]` without a null check.** `lengthenClip` reads `actionLogger.firstAction[BEFORE]->type` unguarded at `gui/views/clip_view.cpp:98`, and `shortenClip` opens with the same unguarded read at `:129`. The nearest null-guarded read of that pointer is `lengthenClip`'s `undoing` test at `:80-82` -- **eighteen lines up, in a different `if` whose result is not carried down to `:98`** -- and `shortenClip` has no earlier test at all, which is what makes this easy to miss rather than an obvious oversight. Both are reached from `changeClipLength` (`:277`, `:298`) and from `AudioClipView::adjustLoopLength` (`:762`, `:770`), i.e. from a shift + horizontal-encoder length edit and from the `X_ENC`+`Y_ENC` set-length-to-sample chord. It survives on the device because an `Action` almost always exists by the time a length edit runs -- `actionLogger` is written by nearly every gesture -- so the window is a fresh boot (or an `actionLogger.deleteAllLogs()`, which `SoundEditor::setup` does on **every menu open**, `sound_editor.cpp:1754`) followed immediately by a length edit. **Established by driving it**: `sim/scenes_spec/audio_clip_scene_spec.cpp` segfaulted here rather than failing, which is why its `Rig::reseed` seeds one inert `ActionType::MISC` action before either side runs. The port reads null as "not a `PATTERN_PASTE`" and is **not** a faithful reproduction, because a port cannot faithfully reproduce a segfault. | **crash (null dereference) on a fresh boot** | `src/scenes/audio_clip/audio_clip_scene.cpp`'s `lengthen_clip` comment; `Rig::reseed`'s `@note` in `sim/scenes_spec/audio_clip_scene_spec.cpp`. Deliberately **not** annotated in `clip_view.cpp` itself, for the reason the `getTickSquare` row above gives |
| **`SoundEditor::setup` dereferences a null `Clip*`, on an everyday gesture.** `sound_editor.cpp:1876`'s `else if (clip->type == ClipType::AUDIO)` runs whenever `currentSound` is null -- `currentSound = newSound` at `:1856`, while `newSound` is only ever assigned inside the `else if (clip)` arm at `:1679-1744` -- so a `setup()` reached with the default null `clip` dereferences null. Four of the fourteen `openUI(&soundEditor)` sites pass one: `gui/views/view.cpp:477` (SHIFT+SELECT), `gui/views/session_view.cpp:569` (plain SELECT from the session view -- *open the Song FX menu*), `gui/views/arranger_view.cpp:346` and `gui/views/performance_view.cpp:846`. **Established by driving it**, not by reading: sites 1 and 2 both SIGSEGV at `:1876` under gdb, in the **toggle-off** leg of the level-3 A/B (`tests/ui-scripts/app/menu.txt`), on an unmodified tree; sites 3 and 4 reach the same line by inspection. Not a crash on the device, where address 0 is mapped -- and that is the hazard rather than the reprieve: `ClipType::AUDIO == 1`, so a byte of 1 at `0 + offsetof(Clip, type)` **takes** the branch and seeds `currentSampleControls` and `currentPriority` with near-null pointers that later menu code both reads and **writes** through. The port has no equivalent -- `openShellMenu` resolves a `MenuContext` and never dereferences the clip -- so the guest makes it **strictly less** reachable, which is why the A/B script stays on a clip-carrying site: the leg that would crash is the one that has to run. Not fixed: a legacy behaviour change, and not annotated in `sound_editor.cpp` either, which this stack cites by line several hundred times. | **undefined behaviour on an everyday gesture; a write through a near-null pointer** | `tests/ui-scripts/app/menu.txt`'s header; migration spec 10.5; F1 task 11's report |
| **The first gold-knob turn after boot is always swallowed.** `InputLowering::action_gold_turn`'s wiggle debounce asks "has this knob been still for `short_press_ms`", comparing against a `last_turn_ms_` that starts at 0 — so on device, where `now_ms` is uptime, the answer is always *yes* by the time a user reaches a knob, and the first turn of every session takes the recorded-but-swallowed arm. `was_short_press` carries a deliberate guard against exactly this shape; this function has none. It also means **all 22 pre-existing UI scripts** turn gold before `t = 500` — inside the window, by luck — so every committed golden depicts the arm the device never presents first. Recorded rather than changed: whether this is a defect or legacy behaviour worth keeping needs a device. | user-visible, not a crash | `src/shell/input_lowering.h`'s gold-knob debounce @warning; `tests/ui-scripts/`'s own docstring |

## The action seam (`menu/action.h`, E2 task 11b)

`MenuState::pending_scene` is the **noun** half of the seam: an item names a scene, and
`MenuScene::after_render` — the first hook after `enter_leaf` that has a `Result` — turns the
request into an `Action::Kind::Push`. It works because every scene a menu wants to open lives in
`src/scenes`.

Five ported items are not like that: `perform_editor`, `start_stem_export`, `set_clip_length`,
`song_midi_learn` and `configure_song_macros` each run a piece of legacy **root-UI** behaviour —
`openUI(&performanceView)`, `audioClipView.setClipLengthEqualToSampleLength()`, and so on. Every one
of those names a symbol in `gui/views/`, `gui/ui/` or `gui/context_menu/`, which `src/scenes` may
not reach. That is not an accident of the checker: the `scenes-no-root-ui` and `scenes-includes-gui`
rows in `scripts/check_layer_rules.py` are what make the seam necessary rather than merely tidy.

So `action.h` adds the **verb** half, in the same shape:

1. `MenuAction` — a *closed* enum of the actions a menu item may request. Closed on purpose: a
   dispatcher can `switch` over it and a new enumerator breaks that switch loudly.
2. `MenuActionRequest` — that enumerator plus one `uint16_t` payload, for the one action whose
   meaning is not fixed by its identity (`OpenPerformanceView` carries the editing mode).
3. `MenuState::pending_action` — the field an item writes, exactly as it writes `pending_scene`.
4. `MenuScene::after_render` drains it **where `pending_scene` is drained**, and parks it in
   `MenuScene::take_action()`.

**`take_action()` is a one-slot mailbox with a silent overwrite, and nothing obliges per-tick
polling.** Two requests filed between two polls lose the first. That is the contract as it stands,
not an oversight to be discovered later: a menu item cannot file two actions in one tick, so the
slot is sufficient *today*, and the wiring plan's dispatcher is the thing that must poll every tick
for it to stay sufficient. If the dispatcher ever wants to be lazy, the slot has to become a queue
first.

**Nothing performs any of these actions.** There is no consumer of `take_action()` in this
repository. `sim/scenes_spec/menu_actions_spec.cpp` asserts that the right item emits the right
identity and payload, driven through the real input path; it cannot and does not assert that
anything happens as a result. A green suite here proves intents are emitted, nothing more.

One related debt, the same shape as `set_shortcuts_version`: `nodes::PerformEditorItem` shows a mode
that is a **mirror** of two `PerformanceView` bools, because a class member offers no bare-`extern`
escape the way a namespace-scope global does. The wiring plan owes four sync calls into
`menu::performance_editing_mode()`; until then the displayed mode can be stale.

## Boot wiring this layer does not do for itself

**Three things, and F1 task 8 closed two of them.** The section that follows kept its full account
of each rather than deleting the closed ones, because what the wiring *is* is exactly what the next
boot author needs, and two of the three turned out not to be boot calls at all.

**1. `menu::set_shortcuts_version()` (`menu/shortcuts.h`) — CLOSED, with a one-way gap.** The
shortcut layout is the one menu setting whose value does not live in a global the existing settings
loader already populates: legacy keeps it in `SoundEditor::shortcutsVersion` and loads it in
`FlashStorage::readSettings` via `soundEditor.setShortcutsVersion(buffer[60])`
(`storage/flash_storage.cpp:568`), and `SoundEditor` is a `gui/` class this layer may not name — so
the port keeps its own copy beside the tables the setting selects. F1 task 8 pushed it from
**inside `SoundEditor::setShortcutsVersion` itself** (`gui/ui/sound_editor.cpp:112`), which is the
sole writer of `shortcutsVersion` and therefore the one place all four change paths funnel through:
boot, the legacy settings item, and **both** factory-reset menus via `FlashStorage::resetSettings`
(`storage/flash_storage.cpp:311`). That last pair is why it is one call inside the function rather
than three at its call sites — one of the two is `scenes/context_menu/context_menu_scene.cpp`'s own
reset, *the ported menu running under the toggle*, and a version that mirrored at three hand-picked
call sites lost it.

> **Still open, and it is the other direction.** The **ported** settings item
> (`nodes::set_shortcuts_version`, `menu/nodes/settings.cpp`) calls `menu::set_shortcuts_version`
> directly and never reaches flash byte 60, so a layout chosen from inside the guest is gone at the
> next power-on. Like the two ported screensaver setters, it needs the `MenuAction` dispatcher that
> does not exist — migration §10.2 item 3, F2's.

**2. `AudioClipScene::set_flash_cursor()` — STILL OPEN, the one that is genuinely unwired.**
`PadLEDs::flashCursor` is the same shape of problem as the shortcut layout — it lives on `PadLEDs`
(`hid/`, which this layer may never include) and `FlashStorage::readSettings` writes it straight
there (`storage/flash_storage.cpp:440`), so there is nothing in `model/` or `storage/` to read it
back from. The scene keeps its own copy, defaulting to `FlashCursor::Slow`, which is both the
factory default (`flash_storage.cpp:287`) and the only one of the three modes that reaches a pixel
at all (`ui/grid/columns.h`'s `tick_colour` warning). Consequence today: a user who turns the play
cursor off still sees it in the audio clip view. Whoever wires it should pass flash byte 36 through,
from wherever legacy's own writer is — the shortcut-version fix above is the shape to copy, not a
call in `deluge_boot`.

**3. Installing the three services — CLOSED, both halves.** Until F1 task 2 there was no *mechanism*;
until F1 task 8 there was no *caller*. `services::ModKnobs`, `services::GlobalControls` and
`services::MidiLearn` are each constructed from a `SceneStack&` (and the first two from an
`InputLowering&`), and they *use* them — `ModKnobs::show_popup` pushes `overlay::ParamPopupScene`
onto that stack (`services/mod_knobs.cpp:413`) and `ModKnobs::popup_suppressed` (`:351`) and
`ModKnobs::mod_encoder_button_action` (`:564`) read `stack_.input_top()`; `GlobalControls::handle`
gates its whole `View::buttonAction` group on `stack_.input_top().is_root()`
(`services/global_controls.cpp:580`); `MidiLearn::handle` reads the same
(`services/midi_learn.cpp:491`). The only `SceneStack` and `InputLowering` in an application are the
ones the **`Shell` owns**, so a real service cannot be constructed before the shell it serves. And
`Services` was passed **by value into the `Shell`'s constructor** and fixed there, so it could not be
installed after it either.

**`Shell::set_services` (`shell/shell.h`) is the mechanism** (F1 task 2): build the shell with
`Services{}`, construct the three over `shell.stack()` and `shell.lowering()`, then install them; the
call re-seats all three things that hold the slots — `services_`, the `Router`'s own by-value copy,
and the deduplicated `service_list_` — and is well defined on a re-install. Shell spec §15.9 has the
account, and `sim/scenes_spec/scene_scripts_spec.cpp`'s forwarding `LateService` shim is gone with
its 34 goldens unmoved.

**`src/deluge/gui/ui/shell_guest_services.{h,cpp}` is the caller** (F1 task 8): it owns the three,
builds them over `shellHostUI`'s own stack and lowering, and installs them from `deluge_boot` and
again from `ShellHostUI::opened()`. It is a separate translation unit rather than three members of
`ShellHostUI` because the host spec suite compiles `gui/ui/shell_host_ui.cpp` directly and never
builds the `deluge_scenes` closure.

> **Installing is not sufficient, and this is the part to read before installing a fourth service.**
> An unseeded `ModKnobs` is not inert: with `mod_controllable == nullptr` it reports `mode() == -1`,
> and every tick the guest runs it declares all eight mod LEDs **off**
> (`services/mod_knobs.cpp:725-735`) and level **0** on both gold knobs (`:683-690`). So the wiring
> re-seeds `currentSong`, `View`'s active mod controllable and mod region and both `ViewState`
> blocks, at boot and on every open — mirroring `view.activeModControllableModelStack` rather than
> keeping a second source of truth. And a service can reach *shared shell state out of band of the
> router*: `ModKnobs::pretend_knobs_untouched()` writes `InputLowering`'s own `last_turn_ms_` array
> at construction, which is the state the gold-knob wiggle debounce reads, so seeding a `mod_target`
> changes what the *next user gesture* does. That is the seam the level-2 re-record turned up
> (carry-table row 30) and the one to check for when the next service lands.

**A note on the two lists, because they are different threes.** This section's three are the
*scenes library's*: what `src/scenes` cannot read for itself. `registry.h`'s boot-wiring record
lists the **boot author's** three — the shortcut layout, `ShellConfig::screensaver_idle_ms` and the
service install — substituting the screensaver timeout (a `shell::Screensaver` setting, closed the
same way and with the same one-way gap on the ported setters) for the flash cursor, which is
`AudioClipScene`'s. `deluge_boot`'s own comment lists the three *settings*. Neither list is wrong;
each is scoped to the file it is in, and all three name every open item between them.

The same note is on `assigned_node_ids()` in `registry.h`, next to the one other thing an
application has to call — that is the file a boot author is already in. `deluge_boot` calls it
explicitly (`src/deluge/deluge.cpp:797`) even though `shellHostUI`'s own construction already
reaches it through `scene_registry()`, because that is a static-initialisation accident rather than
a guarantee and three features silently no-op on an unnumbered tree.

## Roadmap — what E3 and E4 leave

**The menu tree is done.** Nothing below is a menu item, and nothing below blocks the deletion of
`gui/menu_item/**` — that waits on spec F's wiring alone
(`docs/superpowers/specs/2026-08-26-ui-migration-design.md`'s deletion schedule).

**This section was written as "the E4 roadmap" and E4 went somewhere else**, which is worth stating
rather than quietly rewriting. It said E4's scope was the load/save family; E4 built the three
services, the three overlays, the shared timeline state and the first two application scenes. The
reordering was right and the reason is structural: `GlobalControls`' and `MidiLearn`'s `View`-arm
groups are gated on `stack_.input_top().is_root()`, so until a root scene existed those arms were
unreachable and untestable — `audio_clip::AudioClipScene` is what made them live. The load/save
family is unchanged in every other respect and is **E5's or E6's**.

Spec E §7 step 3 promised "BrowserScene family — nine UIs → one"; E3 ported the **two the menu tree
reaches** (`BrowserId::Sample`, `BrowserId::MidiDeviceDefinition`) and the other seven —
`gui/ui/load/load_song_ui`, `load_instrument_preset_ui`, `load_pattern_ui` and all of `gui/ui/save/`
(songs, presets, patterns, kit rows) — are still open. Two reasons they were left, both structural
rather than budgetary: those UIs are opened **from root UIs, not from menu items**, so this stack's
legacy-vs-port oracle (`tables_*_spec.cpp`, which walks the menu tree) cannot exercise them at all;
and they carry a second axis `BrowserScene` has none of — `SlotBrowser`'s brand-new-name machinery
(`mayDefaultToBrandNewNameOnEntry`, `getUnusedSlot`), which is a *saving* concern rather than a
browsing one. Whoever takes them also inherits the two seam limits named above: the leaf calls that
do not claim the catalogue at all, and the claim covering a listing rather than a session.

**The two remaining root scenes** are `KeyboardScene` (`gui/ui/keyboard/`, 5,252 lines) and
`PerformanceScene` (`gui/views/performance_view.cpp`, 2,249), both deliberately out of E4. Both are
named in `scenes-no-root-ui`'s alternative list already, so the ratchet is in place ahead of the
work, exactly as it was for the eleven file/sample nodes E3 landed.

The short list first; the long prose below it is E1's carry list with E2's and E3's answers folded
in, kept because it names the legacy routines each item has to be checked against.

| carried | where it is recorded | size |
|---|---|---|
| **The browser/recorder "heading to" return path** — legacy re-roots the navigation record onto the oscillator's horizontal page when a file is accepted or a recording finishes, so the user does not land back on the item that opened the scene. `SoundEditor::maybeGetParentMenu` (`gui/ui/sound_editor.cpp:2056`) writes the pair; `SampleBrowser::claimCurrentFile` (`gui/ui/browser/sample_browser.cpp:1075-1086`) and `osc::AudioRecorder::beginSession` (`osc/audio_recorder.h:40-48`) read it back. The port's equivalent record is `MenuState::path`/`depth`, which only `MenuScene` may write. | `browser/browser_scene.h`'s `Descriptor::accept` note (the `take_choice()` dispatcher owns it) and `menu/nodes/sample.cpp`'s two `on_focus` bodies | one `MenuScene` entry point; costs one extra BACK press until then |
| **A `Kind::Custom` node cannot draw a row icon in its parent's list.** `MenuScene::render_oled`'s `row_icon` gives a checkbox to `Kind::Toggle` nodes only, so `midi_device_linked` — legacy's `Linked`, a `Toggle` whose `renderSubmenuItemTypeForOled` (`gui/menu_item/midi/device_definition/linked.h:56-78`) draws a checked/unchecked box and, when checked, the linked file's base name — shows a bare label. The node must stay `Kind::Custom`: its ON arm opens a browser, and the committed dump JSON carries the node's type. Consequence on device: the toggle is write-only from the user's side. | `menu/nodes/output_items.h`'s divergence list, and `menu_scene.cpp`'s `row_icon` | one `CustomItem` hook `row_icon` can ask, plus the second text row |
| ~~**`midiDeviceMenu`'s subtree**~~ — **done, E3 task 8** (`menu/nodes/midi_device.{h,cpp}`): eight nodes, three bespoke (the MPE arm), a per-cable binding family over `MenuContext::midi_cable`, and `midi_devices`' `select_chain`. What it leaves behind is one gap, of exactly the kind `menu/nodes/dynamic.h` already records for the formatted "CV OUTPUT N" titles: `midi::Device::getTitle()` is the cable's own display name (`gui/menu_item/midi/device.h:8`) and a `Node::title` is one static id, so the page's title bar carries "MIDI DEVICES" rather than the device's name. | `menu/nodes/midi_device.h`'s "What did not survive" | — |
| **A `Node` cannot compute its own title** — three shipped pages want a runtime one and get a static id instead. `midi::Device::getTitle()` is the selected cable's display name (`gui/menu_item/midi/device.h:8`), so the per-device page's title bar reads "MIDI DEVICES" — the *same string as its parent row*, so a user cannot tell which cable they are editing; `cv::Selection`'s `setCvNumberForTitle` and `gate::Selection`'s `gateModeMenu.format(value + 1)` (`cv/selection.h:43`, `gate/selection.h:50`) put the output number into the destination page's title, so those two read "CV OUTPUT N" unnumbered. A `CustomItem` can already draw whatever title it likes (`MpeDirectionItem` does); what has no way to is a **`Kind::Submenu`** or any node `MenuScene::render_vertical`/`render_leaf_*` titles from `node.title`. | `menu/nodes/midi_device.h`'s "What did not survive" and `menu/nodes/dynamic.h`'s "What is still owed", both of which point here | one hook — `CustomItem::title(const MenuContext&, const MenuState&)` returning an optional `string_view`, consulted by the four `render_*` title draws, plus a `Node::custom` on the two submenus that want one. Moves no JSON (the dump prints `Node::title`, which stays) |
| **Three `IntegerWithOff` nodes draw "0" where legacy draws "OFF"** — `record_count_in`, `saturation`, `voice_count`. `Binding::style.off_at_zero` and `nodes::with_value_style` exist (E3 task 8) and the two nodes that task ported use them; these three predate the modifier. `voice_count` additionally needs an *icon* (`OLED::infinityIcon` in the horizontal menu, `voice/polyphony.h:88-94`), which `ValueStyle` cannot express. | `menu/nodes/detail.h`'s `with_value_style` note, which names all five legacy subclasses | a family sweep with a golden re-record per node, not three incidental edits — which is why E3 task 8 did not fold it in |
| ~~**Splitting session from arranger**~~ — **done, E3 task 9.** `song_midi_learn` and `configure_song_macros` used to stay visible in the arranger because `MenuContext` could not tell the two apart. The conflation was three-way — `sound_editor.cpp:1762-1765 setup` selects the song root for `sessionView`, `arrangerView` *and* `automationView`, and legacy hides these two from all but the first — and is now carried by two bools rather than three: `context_for` takes `session_view_root` (`getRootUI() == &sessionView`) beside the existing `session_or_arranger_root` (`!rootUIIsClipMinderScreen()`), `MenuContext::is_session_view` reports it, and `relevance::is_session_view` guards both nodes. Nothing distinguishes the arranger from the song automation view, because no legacy `isRelevant` does. Cost as built: the one parameter, one field and one predicate the estimate named, plus **85** positional-bool call sites (not ~62 — the estimate undercounted) and two `available_when` keys in the dump JSON, which moved no node id. | `menu/nodes/actions.h`, `menu/context.h`'s `is_session_view` | — |
| **Mod-matrix pad blinking** — `PatchCables::blinkShortcuts`/`blinkShortcutsSoon`/`timerCallback` and the three `shouldBlinkPatchingSourceShortcut` overrides. `MenuScene::render_grid` keys its single blink cycle off the focused *node*; consulting the focused `CustomItem` needs a hook and a second blink source. **Still open after E4, and moved to E5/E6 rather than closed** — no task here touched it. What E4 *did* close is a different blink defect that would have broken this one too: a declared `LedState::Kind::Blink` was not a wake source, so `compute_next_wake` (`shell/shell.cpp`) never woke for it and any blink on a quiet system froze mid-phase (E4 task 4 fix round 1; `MenuScene`'s own Back and Learn blinks were affected). A blink hook added on top of a shell that could not wake for it would have shipped frozen. | detailed below | the same shape of change F1 task 10 made to route MIDI into `MenuScene::learn_*`: a hook on the focused `CustomItem`, and a second blink source |
| ~~**The row-scrolling path is untested, not missing.**~~ — **done, E3 task 10.** `ui/oled/list.cpp:156` does run `scroll_text` on the focused row with motion allowed, and `MenuScene::render_vertical` hands it `st_.scroll[depth]`. `tests/spec/ui/list_spec.cpp`'s "scrolls a selected label..." already pinned the widget's own mechanics; what was missing was the OTHER half, a real focused over-long node driven through `MenuScene` itself. `menu_scene_spec.cpp`'s "a SELECTED over-long row scrolls, held across quiet ticks" opens the real `nodes::runtime_features` page, parks the cursor on `feature_alt_delay_params` ("Alternative Golden Knob Delay Params"), and holds across quiet ticks past `kScrollTime` -- mutation-checked by reverting `list.cpp`'s selected-row draw to plain `text()`, which fails it. | E2 task 12's record; `menu_scene_spec.cpp` | — |
| ~~**Seven self-duplicate goldens owed cleanup**~~ — **done, E3 task 10, corrected fix round 1.** Five were exact duplicates of both halves of the frame right before them: `menu_enum`/`menu_toggle`'s `{after_select_press,after_gold_turn}` (four) duplicate `after_select_turn`, and `menu_shortcut/after_select_turn` duplicates `after_shortcut`. The other two, `menu_int/{after_select_press,after_select_turn}`, are NOT full duplicates of anything and this row's first pass wrongly said they were: their `.pbm` is byte-identical to `idle`'s, but their `.state` gained a `pad` line `idle`'s does not have (the shortcut/source blink lighting up shortly after the screen opens) -- harmless, because the kept `after_gold_turn` snapshot carries the identical line, so no distinct fact went untested, but not the same claim as the other five. Each script's `.txt` drops the `snapshot` line and keeps the input step, with a comment naming why the frame added nothing (a `claim_turn()`-and-ignore leaf, a `changed`-false select press, an unclaimed gold turn, or -- for the two `menu_int` steps -- naming the `.pbm`/`.state` split explicitly); the matching spec in `menu_scripts_spec.cpp` lowers its `result.snapshots` count and leans on the surviving final-state assertion as the dropped step's own proof, same shape `menu_decimal`/`menu_shortcut` already used for the three E2 task 9 deleted. | E2 task 12's record | E2 task 9 deleted three of exactly this kind, so leaving seven was an inconsistency, not a policy |
| **`perform_editor`'s owed sync calls** — the displayed mode mirrors two `PerformanceView` bools. | the action seam section above; `menu/nodes/actions.h` | four calls into `menu::performance_editing_mode()` |
| **The action seam's dispatcher** — nothing consumes `MenuScene::take_action()`. | `menu/action.h`, the action seam section above | one `switch` over five enumerators, plus per-tick polling (the slot is a one-shot mailbox) |
| ~~**`script_runner.h`'s `run_script` never pops its own stack**~~ — **done, E3 task 11.** The gap this row described stopped being latent the moment a script drove `RecorderScene`: its `on_enter` refuses on its own leaked `recorder_` (`recorder_scene.cpp:149`) and `setup_recording` publishes a live foreground recording that nine other subsystems read (`AudioEngine::setForegroundRecording`), so `recorder.txt` would have left both behind for every later block in the process. `run_script` now tears its stack down through a scope guard declared right after the `Shell` — a destructor rather than a line before each of the eight `return`s, three of which are error paths, which are exactly when the stack is most likely to be somewhere strange. `menu_scripts_spec.cpp`'s recorder block asserts what that buys: nothing armed, and `AudioEngine::foregroundRecordingSource()` back at `NONE`. | `tests/spec/shell/support/script_runner.h`'s `run_script` | — |
| ~~**`Action::Kind::RepaintAll` never reached the grid**~~ — **found and fixed, E3 task 11.** `shell/scene.h` documents it as "invalidate every surface for the next tick", but `SurfaceRole` names OLED panels only (`shell/profile.h:60`) and `Shell::tick`'s grid step consulted the grid's own gate and nothing else — so a scene that changed what the *pads* show in response to **post-render** input had no way to say so. Its own gate is not one: `render_grid` is what arms that, and the render has already happened by the time the event arrives (`Shell::tick` step 8). `MarkerScene::handle`'s select turn is the case that showed it: the marker moved a column on the OLED and stayed put on the pads until the blink timer fired, up to a full 200 ms phase later. `Router::grid_repaint_requested()` is the fix, and `marker.txt`'s `after_select_turn` golden is where it is pinned — the one frame in the whole suite whose two halves used to disagree. `BrowserScene::handle` files the same action and had the same latency; nothing else in the shipped scenes does. | `shell/router.{h,cpp}`, `shell/shell.cpp`'s grid step | — |
| **`MenuScene::ascend()` at depth 0 is a silent no-op where legacy exits the editor.** `SoundEditor::goUpOneLevel` at `navigationDepth == 0` calls `exitCompletely()` and returns (`gui/ui/sound_editor.cpp:710-716`); `ascend()`'s loop does a bare `return;` (`menu_scene.cpp:1093-1095`) and its own comment cites that legacy routine without acknowledging the difference. Invisible for as long as everything that ascends is reached by descending — a menu path always has a level to go back to. It becomes visible the moment a path is *re-rooted* on a leaf: the shortcut-pad jump does that (`MenuScene::handle`'s `Replace`), so a jump onto one of the nine `nodes/sample.h` items puts it at depth 0, and `MenuState::go_up_on_resume` — the thing that steps back out of the item when the scene it opened closes — ascends from 0 and does nothing. The user is left on the item's own bare title screen (`SampleSceneItem::render`, whose doc says it is drawn "for at most the one tick": true on the menu path, false on this one) where the only working input is BACK, which closes the menu. All three of E3 task 11's scene scripts end on that screen and their last golden IS it. **Not the `claimCurrentFile` re-rooting** — fix round 1 corrected that mis-diagnosis; this is a three-line divergence from a named routine, deliberately not taken in task 11 because closing it moves goldens well outside that task's subject. | `menu/menu_scene.cpp`'s `ascend()` | an `exitCompletely()` equivalent at depth 0 (`Remove{FirstApp}`, which `handle`'s root-`Back` arm already files), plus re-recording whatever goldens depend on the no-op |
| ~~**`CustomItem`'s missing backward arm**~~ — **done, E3 task 10.** `CustomItem::on_focus_backward` (`node.h`) is what `ascend()` calls now, instead of the old blanket `Kind::Custom` exclusion; do-nothing default, so every item the exclusion used to cover for free (`CvSelectionItem`, `MidiDevicesItem`, `PatchCablesItem`, `SourceSelectionItem`) is unchanged. `GateSelectionItem` (`nodes/dynamic.cpp`) is the one real override, and the reason it needs one: `gate::Selection`'s own backward arm restores `soundEditor.currentSourceIndex`, which `selectButtonPress` never writes for the minimum-off-time row (`gate/selection.h:44-47`) — so "keep the cursor" leaves it on that row, where legacy snaps back to the last real output. Pinned by `menu_scene_spec.cpp`'s task-10 example, which fails on the old exclusion. | `menu/node.h`, `menu/nodes/dynamic.{h,cpp}`, `menu_scene.cpp`'s `ascend()` | — |
| **The load/save browser family** — seven legacy UIs (`load_song_ui`, `load_instrument_preset_ui`, `load_pattern_ui`, and all of `gui/ui/save/`) that `BrowserScene` was shaped for but does not yet carry, because they are reached from root UIs rather than menu items and carry `SlotBrowser`'s brand-new-name axis. **Was billed as E4's main body of work; E4 did the services and the first root scenes instead**, so this is E5's or E6's. | this section's preamble; `browser/browser_scene.h`'s scope note | new `Descriptor`s or a second scene, plus a `SlotBrowser` equivalent, plus an oracle that is not the menu walk |
| **`FileCatalogue`'s claim covers a listing, not a session.** The leaf calls — `predictExtendedText`, `getUnusedSlot`, the save UIs' default-name search — do not claim at all, so a listing dispatched elsewhere can still replace `fileItems` under one of them. This is exactly the exposure legacy always had; closing it means claim-on-open/release-on-close and a change to `Browser`'s lifecycle. | `storage/file_catalogue.h`'s `claim` contract | belongs with whoever wires the browser up for real — i.e. E4 |
| **`take_choice()` has no dispatcher, and it owes two things.** `Descriptor::accept`'s `Defer` outcome parks the chosen file and closes; the dispatcher that drains it must perform `SampleBrowser::claimCurrentFile` (which reaches the sound and the audio-file manager through paths `src/scenes` may not name) **and** legacy's `maybeGetParentMenu` re-rooting, which lands the user on the source page rather than back on the item that opened the browser. The recorder carries the identical pair. | `browser/browser_scene.h`'s two `Descriptor::accept` notes; `menu/nodes/sample.cpp`'s two `on_focus` bodies | one dispatcher, in the wiring layer; costs one extra BACK press until then |
| **`model/clip/audio_clip.cpp` still includes `gui/waveform/waveform_renderer.h`** — the one surviving `model-includes-ui` row of the waveform split, and it is the *method* that is inverted, not the include: `AudioClip::renderAsSingleRow` is a `model/` class that draws into an `RGB*`. Its extracted form takes a `GridFrame` that `model/` cannot build, so closing it means moving clip-row rendering into the scene layer. | the include site itself; `scripts/layer_rules_baseline.txt` | `SessionScene`'s job |
| **`slicing::SliceOps` has no implementation.** Six virtuals, no-op defaults, wired by nobody — the whole audio/model/card half of the slicer. `Slicer` reaches that work through four `gui/` globals `scenes-no-root-ui`/`scenes-includes-gui` forbid (`sampleBrowser` `:600`/`:749`, `soundEditor` `:452`/`:612-614`/`:634`, `instrumentClipView` `:481`/`:747`, `PadLEDs` `:378`), so the port names the calls at a boundary and keeps the state, the guards, the coalescer, the dispatch and the gate. Until a wiring layer implements it the slicer selects, deletes, renders and snapshots correctly and makes no sound and no kit. `SliceOps::play_position` is the **third** consumer of the voice walk no spec can drive — `Slicer::graphicsRoutine` and `AudioClipScene`'s playhead are the other two — so the fixture that unblocks it (a `Song` with a booted `AudioEngine` and one sounding voice) unblocks all three. | `slicing/slicer_scene.h`'s `SliceOps`; the slicer section above | six virtuals over ~195 lines of legacy (`Slicer::previewForTarget` 40, `Slicer::doSlice` 155) plus the oracle neither has today — a task, not a wiring detail |
| **Eleven firmware defects, recorded not fixed** — three from E3 (the `AudioRecorder::finishRecording` use-after-free, the dead `FOLDER_DOESNT_EXIST` fallbacks, the swallowed first gold turn), **seven from E4** (`View::modButtonAction`'s two unguarded `getModKnobMode()` dereferences, `View::setKnobIndicatorLevel`'s uninitialised `knobPos`, `ClipView::getTickSquare` comparing a scroll against the zoom, `ClipView::lengthenClip`/`shortenClip`'s unguarded `actionLogger.firstAction[BEFORE]`, `Slicer::renderMainPads`' missing image null-check, `Slicer`'s 432 bytes of dead stack declared twice, and `Slicer::selectEncoderAction`'s equality-only count wrap), and **one from F1** — `SoundEditor::setup`'s null-`Clip*` dereference at `sound_editor.cpp:1876`, which is the most reachable of the eleven (plain SELECT in the session view) and the only one **driven** rather than read. | "Defects this port found in shipping firmware" above, and each defect's own site | each needs a device to validate; none is this stack's to change |
| **`ClipView`'s `Y_ENC` shift arm is a silent no-op for a real gesture, and must not survive E5.** `AudioClipScene`'s horizontal-encoder arm CLAIMS the Y_ENC (and `X_ENC`-held + CLIP) chord exactly as legacy does (`gui/views/clip_view.cpp:249`) and then does nothing, because two of the three arguments `Clip::shiftHorizontally` takes are decided by `getCurrentUI() == &automationView` (`:203-206`) — a `scenes-no-root-ui` read this layer may not make, and with no `AutomationScene` a question with no answer — and its undo consequence `alloc_sdram`s a `ConsequenceClipHorizontalShift` (`:240-245`), which Global Constraint 6 forbids from library code. Claiming it is right (letting it fall through would hand a Y_ENC turn to `GlobalControls`, which legacy never does); leaving it empty once an `AutomationScene` exists is not. **The instruction to whoever writes that scene: port this arm in the same change.** | the `@warning` at `audio_clip/audio_clip_scene.cpp`'s Y_ENC arm, this row, and migration spec §10.4 | one arm plus its consequence, once `AutomationScene` can answer the question |
| ~~**Every menu golden's gold-turn step was recorded with `Services{}`**~~ — **the install landed UNSEEDED, F1 task 11 fix round 2; seeding is what is still owed.** The twelve gold-turn scripts in `menu_scripts_spec.cpp` now install `ModKnobs`/`GlobalControls`/`MidiLearn` through `sim/scenes_spec/support/real_services.h` with `svc.song` set and `svc.mod_target` left null; the other twelve keep `Services{}` because they drive no gold turn. Cost, measured: **57 `.state` files, 114 insertions, 0 deletions, 0 `.pbm`, 0 value assertions**, every inserted line one of `indicator lower 0 0 0 0` / `indicator upper 0 0 0 0` — `ModKnobs::leds` declaring both rings dark for want of a target. `led Mod1 on` does not appear; that comes from seeding. Task 11 first refused this on E4 task 13's rule and the re-review reversed it by doing the recording: `Services{}` is three nulls, *further* from the application than an unseeded `ModKnobs`, so this was never right-fixture-versus-wrong but very-wrong versus one seeded step from right, and 114 lines of one repeated pair is not the ambiguous two-cause diff that rule exists to prevent. **Caution that keeps these goldens honest: unseeded is exactly the configuration in which the swallowed-turn defect cannot bite**, so they settle nothing about gold turns under real services. **Owed, as a named edit:** seed `svc.mod_target`, arm the debounce in each script the way `tests/ui-scripts/shell/menu_gold_turn.txt` does (one leading detent inside `short_press_ms`), and re-record; the measured cost of that step is `led Mod1 on` on 57 frames plus 11 `.pbm` files moving — `menu_chain` 3, `menu_int` 1, `menu_patch_cables` 1, `menu_range` 4, `menu_submenu_horizontal` 2 — and two value assertions changing. **Why seeding swallows a turn:** `InputLowering::action_gold_turn` runs on the gold path unconditionally, gated on no service — the Continuous arm of `InputLowering::apply_encoder_edges` (`shell/input_lowering.cpp:191`), which `on_encoder_edges` forwards to (`:158-163`) — and swallows a turn unless `(now_ms - last_turn_ms_[role]) < short_press_ms` (`:214`, 500 ms, `input_lowering.h:26`). `ModKnobs::pretend_knobs_untouched()` (`services/mod_knobs.cpp:193-197`) writes that same array through `InputLowering::pretend_knob_untouched` (`input_lowering.cpp:451-461`, `last_turn_ms_ = now_ms - 1000`), and `RealServices::installer()` reaches it twice at construction — `set_target_with_timeline` (`mod_knobs.cpp:120`) and `set_mod_region` (`:165`) — **only when a `mod_target` is seeded**. At `menu_int.txt`'s `+2` at t=300: unseeded `300 - 0 < 500` → acted → 4; seeded `300 - (0-1000) = 1300 ≥ 500` → swallowed → 0. `menu_gold_turn` reads 4 on the same node because its `+1` at t=100 arms the debounce first. **A service mutated shared lowering state at construction and it surfaced in a debounce no routed event passes through** — the seam to know about before installing the next service. | this row; `tests/ui-scripts/shell/menu_int.txt`; `sim/scenes_spec/menu_scripts_spec.cpp`'s header; migration §10.2's blocker list | seed the mod target, arm each script's debounce, re-record, and look at each one |
| **Six of the fourteen `openUI(&soundEditor)` sites still open the legacy `SoundEditor` with the toggle on**, so the firmware then has *both* menus reachable by different gestures. Each is a site whose root node has no port — the reason for each is in `gui/ui/shell_menu_entry.cpp`'s fourteen-entry-point table, which is the authority. The other eight are gated on `shellMenuEnabled()` and, with the toggle **off**, run the exact legacy statements they ran before: every removed line reappears as a stripped-identical line in the `else`, checked at all eight. | `gui/ui/shell_menu_entry.cpp`'s table; migration §10.2 item 2 | one ported root node per site, then one gate each |
| **The ported menu blacks the grid where legacy greys the sidebar**, and it has **two** causes — fixing either alone leaves it wrong. (a) `GuestSurfaceSet::send_columns` pushes the menu's all-black grid into `PadLEDs::image`; (b) legacy's greyout walk never runs at all, because `ShellHostUI` never re-requests grid rendering, so `SoundEditor::getGreyoutColsAndRows` (`gui/ui/sound_editor.cpp:252-294`), `reassessGreyout` and `prepareColour` are never reached. **Proved by probe, not inspection**: disabling (a) alone brought the sidebar back **ungreyed** (`pad 16 0 0 255 6`, boot's colour, not the off leg's `3 160 7`). The most user-visible thing this branch produced, and it is adjacent to `Shell::dim_grid`'s deferred "spec design, not a port" invention — arguably the same question arriving empirically. | migration §10.2 item 2; the four `.state` diffs in `tests/golden/ui/app/menu{,_shell}/`; `src/shell/README.md`'s composition section | a grid-repaint request from the guest **and** a transparency answer from `send_columns`, together |
| **Gold-knob MIDI learn of a menu param has no port at all.** Legacy's `SoundEditor::modEncoderAction` reaches `MenuItem::learnKnob` through its `UI_MODE_MIDI_LEARN` arm (`gui/ui/sound_editor.cpp:1627-1636`), and `SoundEditor::pitchBendReceived` reaches it again at `:1613-1614`; the only `learnKnob` hits under `src/scenes` are comments. F1 task 8's note naming it as that task's to close was simply wrong — installing the service and routing the turn would not have helped, because there is nothing to route *to*. | `src/deluge/gui/ui/shell_host_ui.h`'s `@par MIDI learn`; migration §10.2 item 8 | a `learn_knob` hook on `CustomItem`/`Binding`, plus the arm that reaches it |
| **`MenuScene` owes `SoundEditor::buttonAction`'s LEARN arm, and until it lands both MIDI-learn transports carry nothing.** Every legacy learn dispatcher is gated on `currentUIMode == UI_MODE_MIDI_LEARN`; two lines in the tree assign it (`gui/views/view.cpp:493`, `gui/ui/sound_editor.cpp:520`) and neither runs while the guest is the current UI. The port's own `MidiLearn::active_` is a different bit `PlaybackHandler` cannot see. **Two separate open items would each make the transports live** and they are distinct mechanisms: this one (direct hardware-button handling inside `MenuScene`, gated on `allowsLearnMode()`), and the missing `MenuAction` dispatcher, whose `song_midi_learn` entry reaches the same mode by the context-menu route. | `gui/ui/shell_host_ui.h`; `playback/playback_handler.cpp`'s learn arms; `menu/node.h:432`; migration §10.2 item 8 | one `button_action` arm plus the `allows_learn_mode()` hook `wants_learn_blink()` is **not** a synonym for |
| **The ported shortcut-version setter never reaches flash byte 60**, so a layout chosen from inside the guest is gone at the next power-on. `nodes::set_shortcuts_version` (`menu/nodes/settings.cpp`) calls `menu::set_shortcuts_version` directly; the legacy direction is closed (F1 task 8 mirrors from inside `SoundEditor::setShortcutsVersion`, covering all four writers), this one is not. The same one-way shape as the two ported screensaver setters. | "Boot wiring this layer does not do for itself" above; migration §10.2 item 5 | the `MenuAction` dispatcher (row above), which is what a ported setter needs to reach `gui/` |
| **Six doc-comment tables are mangled by clang-format's comment reflow** and do not render: `menu/nodes/midi_device.h`, `menu/nodes/patch_cables.h`, `recorder/recorder_scene.h`, `shell/scene.h`, `shell/scene_stack.h`, `shell/shell.h`. Rows merge mid-line into ` | | ` runs, so a reader gets one unreadable paragraph where a table was intended. E4 task 9 fixed `slicing/slicer_scene.h`'s two and task 14 fixed `marker/marker_scene.h`'s one, both by rewriting the table as a bulleted list, which survives the reflow (verified by re-running `clang-format` on it). Find them with `grep -rn "| |" --include='*.h' src/`. Not swept here because each one's citations have to be re-verified as it is rewritten — `marker_scene.h`'s five all resolved, but that was checked line by line, not assumed. | this row | six files, a list rewrite and a citation check each |

Beyond the menu tree, the remainder of spec E §2's scene inventory is `SessionScene`,
`NoteClipScene`, `ArrangerScene`, `KeyboardScene`, `PerformanceScene`, `AutomationScene` and the
load/save half of the browser family — **the overlays are done** (E4 tasks 1, 6 and 7 landed the
last three). **The wiring plan (F1) has landed and delivered most of what this paragraph used to
owe**: the link (task 1), `Shell::set_services` and its boot caller (tasks 2 and 8), the toggle
(task 3), the guest's surfaces, lifecycle, render, input and timer (tasks 4-7), eight of the
fourteen entry points (task 9), the MIDI transports (task 10) and a level-3 A/B that runs the whole
firmware twice and diffs the two legs (task 11). **What F1 deliberately left to F2** is migration
§10.2's items 3, 4 and 6 — the `MenuAction` dispatcher, `take_choice()`'s dispatcher, and
`perform_editor`'s four sync calls — plus phase 5's `LegacySceneAdapter`; the five carry rows above
are the rest. Three of
`MidiLearn`'s four entry points had **no producer in `src/`**, and F1 task 10 closed part of that.
Two corrections to what this paragraph used to say, both found by re-reading rather than by
re-running a gate: `thing_pressed` was never producerless — it has three callers in the service's own
`handle()` (`services/midi_learn.cpp:513`, `:521`, `:529`, the TAP/PLAY/RECORD arms) — and only
`note_on_received` and `cc_received` were, `cc_received` re-entering `note_on_received` internally.
Both now have a transport: `UiEvent::Kind::MidiNoteOn` and `Kind::MidiCc` (`shell/scene.h`), carried
by `Router::run_chain`'s `midi_learn` step. **Only the note-on half has a producer**
(`ShellHostUI::noteOnReceivedForMidiLearn` -> `Shell::route_midi`); the CC half cannot get one
without a new `UI` virtual, and neither can do anything until a scene answers `learnable_at(pad)` and
calls `thing_pressed`, which is E5's and E6's. See migration §10.2 item 8.

### The carry list in detail

E1 ships the menu tree, its interpreter (`MenuScene`), and the ten root tables — the rest of spec E
(§2's scene inventory: `SessionScene`, `NoteClipScene`, `BrowserScene`, the overlays, …) is later
work. Within the menu tree itself, carried to E2: `source_shortcut_pads` (needs patch cables); a
`Kind::Action` re-introduction with its first real consumer; per-pad blink frequency masks;
two context-dependent blink suppressions;
settings-root shortcut jumps (blocked on `MenuContext` gaining an additive `background_root` field
so it stops conflating "which root menu is open" with "what view is behind it"); the four
`hid/`-side write side effects a menu write can trigger that `src/scenes` cannot reach directly (pad
brightness, screensaver suppression, canvas flags, sticky shift);

**`selectButtonPress()` chaining is now ported** (E2 task 3) as `Node::select_chain` plus
`Node::chain_targets` — a hook that names the menu a select press opens (null = "ascend", legacy's
own spelling) and a static span of every node that hook may return, which is what lets
`assign_node_ids`, `validate_tree` and `deluge_menu_dump` reach the seven pages no children span
names. Six of the eight rows in the design spec's sweep are implemented: `arp_preset` →
`arpOctaveModeToNoteModeMenu`, or its drum twin on a kit row, when the preset is CUSTOM
(`arpeggiator/preset_mode.h:109-118`); `arpOctaveModeToNoteMode` / its drum twin → the note mode
(`arpeggiator/octave_mode.h:90`, `:99`); `unison_count` → `unison::stereoSpreadMenu`
(`unison/count.h:76-79`); `polyphony` → `polyphonicVoiceCountMenu` when POLY
(`voice/polyphony.h:139-143`); `cv_selection` → `cvSubmenu` (`cv/selection.h:41-46`);
`gate_selection` → `gateOffTimeMenu` / `gateModeMenu` (`gate/selection.h:43-54`). The seven
chain-only pages that made "declaring them and implementing chaining are one job" true are declared
with it: the four arp ones (`menus.cpp:511-521`) and `cvSubmenu` / `gateModeMenu` /
`gateOffTimeMenu` (`:1223-1232`).

**`PatchedParam::selectButtonPress` is now ported too** (E2 task 9): every node with
`bind.param.kind == PATCHED` — **53** of them, reached through three delegators
(`master_transpose.h:67`, `transpose.h:33`, `patched_param/integer.h:42`) — carries
`with_patch_chain` (`nodes/patch_cables.h`) and opens `nodes::source_selection_regular`, the port of
`source_selection::regularMenu`. Task 3 had to carry it because its target was a patch-cable page
nobody owned and because `SelectChainFn` takes a `const MenuState&` and so cannot write
`soundEditor.patchingParamSelected`; the resolution is that the *target* writes it, from
`SourceSelectionItem::on_focus`, reading the param out of the node the chain came from
(`path[depth - 1]`). The shift-held automation-delete arm is still not ported — `SelectChainFn` is
not passed the button state, so no node in the tree can express it. That divergence is also recorded
as a named exclusion **at the comparison** (`kPatchedParamShiftArmPorted`,
`sim/scenes_spec/tables_2_spec.cpp`), with a `static_assert` on `SelectChainFn`'s signature so the
exclusion has to be re-argued if the signature ever grows a way to see shift.

That task also declared the rest of the patch-cable graph — `patch_cable_strength_regular`,
`source_selection_range`, `patch_cable_strength_range` — plus the range picker `multi_range`, and
gave `patch_cables` (MOD MATRIX), `vibrato` and `sidechain_volume` their real items. Two consequences
worth knowing: the strength/range pair is the tree's **first cyclic chain** (legacy's own — from the
range-strength page a select press goes back to the range source picker and straight back again), so
`deluge_menu_dump` grew the `"ref"` stub its `fail_on_chain_cycle` comment had been asking for; and
`MenuScene::enter_child` now *acts* on `Permission::MustSelectRange` by descending into the picker,
which is legacy's `multiRangeMenu.menuItemHeadingTo` substitution (`sound_editor.cpp:1840-1841`).

Still carried from the sweep: `osc0_type` / `osc1_type` → `dxMenu` on a DX7 osc (`osc/type.h:120-125`), which waits on
`dxMenu` itself — three bespoke DX items (`menus.cpp:1717`) that are E2's browser work, not a page
this task could declare honestly; and the two other dropped arms the sweep turned up, `Submenu`'s `shouldForwardButtons()`
forwarding (`submenu.cpp:187-194`) and `Automation::selectButtonPress`'s shift-held automation
delete (`automation/automation.cpp:37`), which a `Param`/`UnpatchedParam` item inherits directly and
a `PatchedParam` one reaches only through the shift branch above.
**Carried by E2 task 9, and owed to the eventual MIDI/grid wiring plan** (which does not exist as a
named task yet, so it is listed here rather than deferred to one): the mod-matrix list's **patching
pad blinking**. `PatchCables::blinkShortcuts` / `blinkShortcutsSoon` / `timerCallback`
(`gui/menu_item/patch_cables.cpp:113-151`) light the selected cable's destination pad through
`soundEditor.setupShortcutBlink` and its one or two source pads through
`updateSourceBlinks` + `shouldBlinkPatchingSourceShortcut`, throttled by a 500 ms
`TimerName::UI_SPECIFIC`. `MenuScene::render_grid` owns this port's single blink cycle and keys it
off the focused *node* (`menu_scene.h`'s `kShortcutBlinkMs`); making it consult the focused
`CustomItem` instead needs a hook on `CustomItem` and a second blink source, which is the same shape
of change as routing MIDI to `MenuScene::learn_*`. The three
`shouldBlinkPatchingSourceShortcut` overrides (`patch_cables.cpp:153-162`,
`patch_cable_strength/{regular,range,fixed}.cpp`) and `patchingSourceShortcutPress` go with it.

`Source::getOrCreateFirstRange()` on a zero-range sample item is **no longer carried**: E2 task 9
put it in `menu::ensure_first_range` (`menu/context.h`), which `MenuScene::enter_child` runs ahead of
the permission question for every `range_dependent` descent — exactly where
`checkPermissionToBeginSessionForRangeSpecificParam:1938` runs it. Also carried: per-node cursor
memory across sessions; **gold-knob acceleration
(`calcNextKnobSpeed`) — no longer carried: E4 task 2 closed it.** The arithmetic had been ported per
encoder role in plan D as `InputLowering::calc_next_knob_speed` (`shell/input_lowering.h`, pinned by
`tests/spec/shell/input_lowering_spec.cpp` against `hid/encoders.cpp:46-77`); what was missing was a
caller, since legacy applies it one line before `modEncoderAction`
(`hid/encoder_input.cpp:192`, `offset * encoder.calcNextKnobSpeed(offset)`).
`services::ModKnobs::handle` is `modEncoderAction`, so it is that caller
(`scenes/services/mod_knobs.h`, "Acceleration: the carry item this closes"), and
`sim/scenes_spec/mod_knobs_spec.cpp` drives a fast repeat and a slow one and reads the difference
out of the model. Two divergences from legacy survive, both plan D's shape and both named at
`scenes/services/mod_knobs.h`'s acceleration section: legacy's `last_offset`/`last_encoder_time` are
function-local statics **shared by both gold knobs** while `InputLowering` keeps them per encoder
role (so alternating the knobs resets the ramp on the device and not here), and legacy's
sub-millisecond `getSystemTime()` versus the port's whole-millisecond clock caps the acceleration
term. Menu-side acceleration (`HorizontalMenu::calcNextKnobSpeed`,
`gui/menu_item/horizontal_menu.cpp:630`) is a separate copy of the same arithmetic and is **not**
closed by this. Still carried: real implementations behind every `nodes::kPending` placeholder this task
left in the tree (E2 task 2 closed one general prerequisite for those -- `Kind::Custom` now routes
input to `CustomItem::handle`, ahead of the scene's own button handling -- and gave two of the
placeholders, `firmware_version`/`battery_level`, their own finished `CustomItem`s (reproducing
`battery::Level`'s charging heuristic with `mutable` state on the item, the way legacy keeps it on
the menu item instance); E2 task 5 gave the three name editors theirs, `nodes::EditNameItem`, which
opens `rename::RenameScene` — the port of legacy's five-class `gui/ui/rename/` directory — through
`MenuState::pending_scene`; E2 task 7 the five reset items and task 8 the twelve MIDI commands; E2
task 9 the two patch-cable pages, the two source pickers, the mod matrix, the two fixed cables and
the range picker; E2 task 10 the CV and gate output selectors and the MIDI device list
(`menu/nodes/dynamic.h`); E2 task 11b the five action items, the two preset-scale checklists and the
audio-source selector; E3 task 6 the two file browsers, the five marker items and the two
record-to-sample items (`menu/nodes/sample.h`); and E3 task 7 the last two, `midi_device_linked`
and `specific_output_selector` (`menu/nodes/output_items.h`), **so none is left**). See the design
spec's
"Amendments during implementation" section for the full, cited account of each.
