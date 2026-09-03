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
and the eleven `kPendingCustomNodes` of the file/sample family are E3's, along with the rest of the
carry list at the end of this file. **E3 tasks 6 and 7 have since closed all eleven**: task 6 the
nine of the file/sample family (`menu/nodes/sample.h`: the two file browsers, the four loop points,
the audio clip's waveform view and the two record-to-sample items) and task 7 the last two,
`midi_device_linked` and `specific_output_selector` (`menu/nodes/output_items.h`). **The menu tree
has no `Kind::Custom` placeholders left**; `kPendingCustomNodes` is 0 and
`ValidateOptions::allow_placeholder_custom` is gone with them.

> **Read this before adding a menu item.** The shipping firmware still runs the *legacy*
> `SoundEditor` and its ~200 hand-written `MenuItem` classes under `src/deluge/gui/menu_item/`.
> **Nothing links `deluge_scenes`** — `src/CMakeLists.txt`'s `add_subdirectory(scenes)` builds the
> library, and its only consumer is its own spec executable (see "Link direction" below). So during
> the coexistence:
>
> * **A menu item users must be able to open goes into `gui/menu_item/` + `gui/ui/menus.cpp`**, the
>   legacy tree, exactly as before. That is the system the device runs.
> * **A node added here is a port of a legacy item**, kept in step with it and cross-checked
>   against it by `sim/scenes_spec/tables_*_spec.cpp`. It changes nothing a user can see. E2
>   finished the *items* and did not give `deluge_scenes` an entry point: nothing routes buttons,
>   pads or MIDI into the scene stack, nothing performs a `MenuAction`, and no boot path calls
>   `menu::set_shortcuts_version` or `assigned_node_ids`. That is the wiring plan's job, and until
>   it lands this library is a very well tested tree that no user reaches.
> * **Adding an item to one tree only silently diverges them.** A new legacy item with no node here
>   fails no test today; a node here with no legacy item fails the lockstep walk immediately. If you
>   add to `gui/menu_item/`, add the node too.

Rules:
1. `src/scenes/**` may include `scenes/**`, `shell/**`, `ui/**`, `gfx/**`, `notify/**`,
   `libdeluge/**`, the standard library, and -- being the first model-aware layer --
   `model/**`, `modulation/**`, `playback/**`, `processing/**`, `storage/**`, `gui/l10n/**`,
   `definitions_cxx.hpp`.
   Never `gui/` otherwise, never `hid/`. `model/model_stack.h` is narrower still: only
   `menu/context.cpp` and `menu/bindings.cpp` may include it, and `menu::model_stack_for()` is
   the single entry point everything else uses to reach a `ModelStack` (task-3 ruling 6).
2. `snake_case` functions/variables, `PascalCase` types, `k` constants; namespace
   `deluge::scenes`.
3. No heap in library code; no wall clock — `Scene::next_timer()` is how a blink stays alive, and
   since E3 task 10 that is all it takes: `Shell::tick`'s step 8b arms every surface the scene is
   drawn on at the deadline the hook names, so a scene no longer has to remember a matching
   `RepaintGate::request_repaint_after` from inside its own `render_*` (several still make one,
   more tightly, and `Scene::timer_repaints()` is the opt-out for a timer nothing on screen follows).

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
- **`sim/scenes_spec/`** -- model-backed specs, ten files today: `l10n_bridge_spec.cpp`,
  `profile_spec.cpp`, `context_spec.cpp`, `bindings_spec.cpp`, `menu_scene_spec.cpp`,
  `menu_scripts_spec.cpp`, `shortcuts_spec.cpp` and the three table cross-checks
  `tables_1_spec.cpp` / `tables_2_spec.cpp` / `tables_3_spec.cpp` --
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

   **The learn virtuals are inert without routing.** `MenuScene::learn_note_on`/`learn_cc`/
   `learn_program_change`/`unlearn` exist and are specced, but nothing in this repository routes
   MIDI into the scene stack. `bindings::learn_target` is a *different* path — PARAM learn, not
   global-command learn.
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
custom item, not the distinct `CustomItem` instances, since 11 of them share the single
`nodes::kPending` placeholder today (see "Adding a `Custom`" above and the design spec's
amendment); no
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
`gui/views/automation_view.cpp:1641` and no `Node` reaches it, so it is deliberately absent rather
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
(`edit_name.cpp:50`, read by `SoundEditor::focusRegained`, `sound_editor.cpp:280-283`): without it,
closing the pushed scene would land back on the item and `on_focus` would open it again.

This is the pattern for every other bespoke item that opens a UI (the file selector, the sample
marker editor, the loop points, the DX browser, the audio recorder) — all five set the same legacy
flag beside the same `openUI` call.

#### …and how an item says its `on_focus` is really a *press*

`on_focus` is `beginSession`, and `MenuScene` runs it on **every** focus transition: a descent from
the parent list, a `SceneId` deep link, the shortcut-pad jump, and `restore_state` rebuilding a saved
path. Legacy runs `beginSession` on all of those too (`SoundEditor::focusRegained` → `beginScreen`,
`sound_editor.cpp:292`, `:1015`), so for the items above the port matches and there is nothing to do.

It does **not** match for an item whose legacy body is `selectButtonPress` rather than
`beginSession` — legacy never navigates into such an item at all (`shouldEnterSubmenu() == false`),
so the port descends for one tick and acts from `on_focus` instead (`menu/nodes/actions.h`). That is
right for a press and wrong for every other way in. E3 task 7 found it live: restoring a menu parked
on `midi_device_linked` replayed the toggle and **cleared the user's linked definition file**, and a
deep link onto `start_stem_export` re-filed the export. Legacy guards exactly this —
`Toggle::selectButtonPress` returns without toggling when `soundEditor.getCurrentMenuItem() == this`
(`gui/menu_item/toggle.h:35-37`), i.e. when it was reached without a press on its row.

The port of that guard is two pieces: `MenuScene::enter_leaf` takes a `FocusCause`
(`Descended` from `descend_to`, `Restored` from `retarget`, `restore_state` and `ascend`), and
`CustomItem::on_focus_is_press()` says which items it applies to. Both are needed — a blanket skip on
restore would regress the shipped ruling above (`EditNameItem` *must* reopen its editor), and a flag
with no cause cannot tell a press from a rebuild. **The line is "does it act without a press", not
"does it do anything":** opening a scene the user can back out of keeps the flag false; writing the
model or firing a one-shot verb sets it. Two items set it today, `MidiDeviceLinkedItem` and
`ActionItem`.

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

One thing, and it is easy to miss because everything around it is automatic. **`menu::set_shortcuts_version()`
(`menu/shortcuts.h`) has no caller at boot.** The shortcut layout is the one menu setting whose value
does not live in a global the existing settings loader already populates: legacy keeps it in
`SoundEditor::shortcutsVersion` and loads it in `FlashStorage::readSettings` via
`soundEditor.setShortcutsVersion(buffer[60])`, and `SoundEditor` is a `gui/` class this layer may not
name — so the port keeps its own copy beside the tables the setting selects. Consequence today: a
user who selects the pre-3.0 layout gets the current one back on the next power-on. Whoever wires the
scene stack to `deluge.cpp` should pass flash byte 60 through. Wiring it from `storage/` instead
would be a storage-depends-on-scenes edge and is the wrong direction.

The same note is on `assigned_node_ids()` in `registry.h`, next to the one other thing an
application has to call — that is the file a boot author is already in.

## E3 roadmap — what E2 leaves

The short list first; the long prose below it is E1's carry list with E2's answers folded in, kept
because it names the legacy routines each item has to be checked against.

| carried | where it is recorded | size |
|---|---|---|
| **The browser/recorder "heading to" return path** — legacy re-roots the navigation record onto the oscillator's horizontal page when a file is accepted or a recording finishes, so the user does not land back on the item that opened the scene. `SoundEditor::maybeGetParentMenu` (`gui/ui/sound_editor.cpp:2018`) writes the pair; `SampleBrowser::claimCurrentFile` (`gui/ui/browser/sample_browser.cpp:1075-1086`) and `osc::AudioRecorder::beginSession` (`osc/audio_recorder.h:40-48`) read it back. The port's equivalent record is `MenuState::path`/`depth`, which only `MenuScene` may write. | `browser/browser_scene.h`'s `Descriptor::accept` note (the `take_choice()` dispatcher owns it) and `menu/nodes/sample.cpp`'s two `on_focus` bodies | one `MenuScene` entry point; costs one extra BACK press until then |
| **A `Kind::Custom` node cannot draw a row icon in its parent's list.** `MenuScene::render_oled`'s `row_icon` gives a checkbox to `Kind::Toggle` nodes only, so `midi_device_linked` — legacy's `Linked`, a `Toggle` whose `renderSubmenuItemTypeForOled` (`gui/menu_item/midi/device_definition/linked.h:56-78`) draws a checked/unchecked box and, when checked, the linked file's base name — shows a bare label. The node must stay `Kind::Custom`: its ON arm opens a browser, and the committed dump JSON carries the node's type. Consequence on device: the toggle is write-only from the user's side. | `menu/nodes/output_items.h`'s divergence list, and `menu_scene.cpp`'s `row_icon` | one `CustomItem` hook `row_icon` can ask, plus the second text row |
| ~~**`midiDeviceMenu`'s subtree**~~ — **done, E3 task 8** (`menu/nodes/midi_device.{h,cpp}`): eight nodes, three bespoke (the MPE arm), a per-cable binding family over `MenuContext::midi_cable`, and `midi_devices`' `select_chain`. What it leaves behind is one gap, of exactly the kind `menu/nodes/dynamic.h` already records for the formatted "CV OUTPUT N" titles: `midi::Device::getTitle()` is the cable's own display name (`gui/menu_item/midi/device.h:8`) and a `Node::title` is one static id, so the page's title bar carries "MIDI DEVICES" rather than the device's name. | `menu/nodes/midi_device.h`'s "What did not survive" | — |
| **A `Node` cannot compute its own title** — three shipped pages want a runtime one and get a static id instead. `midi::Device::getTitle()` is the selected cable's display name (`gui/menu_item/midi/device.h:8`), so the per-device page's title bar reads "MIDI DEVICES" — the *same string as its parent row*, so a user cannot tell which cable they are editing; `cv::Selection`'s `setCvNumberForTitle` and `gate::Selection`'s `gateModeMenu.format(value + 1)` (`cv/selection.h:43`, `gate/selection.h:50`) put the output number into the destination page's title, so those two read "CV OUTPUT N" unnumbered. A `CustomItem` can already draw whatever title it likes (`MpeDirectionItem` does); what has no way to is a **`Kind::Submenu`** or any node `MenuScene::render_vertical`/`render_leaf_*` titles from `node.title`. | `menu/nodes/midi_device.h`'s "What did not survive" and `menu/nodes/dynamic.h`'s "What is still owed", both of which point here | one hook — `CustomItem::title(const MenuContext&, const MenuState&)` returning an optional `string_view`, consulted by the four `render_*` title draws, plus a `Node::custom` on the two submenus that want one. Moves no JSON (the dump prints `Node::title`, which stays) |
| **Three `IntegerWithOff` nodes draw "0" where legacy draws "OFF"** — `record_count_in`, `saturation`, `voice_count`. `Binding::style.off_at_zero` and `nodes::with_value_style` exist (E3 task 8) and the two nodes that task ported use them; these three predate the modifier. `voice_count` additionally needs an *icon* (`OLED::infinityIcon` in the horizontal menu, `voice/polyphony.h:88-94`), which `ValueStyle` cannot express. | `menu/nodes/detail.h`'s `with_value_style` note, which names all five legacy subclasses | a family sweep with a golden re-record per node, not three incidental edits — which is why E3 task 8 did not fold it in |
| ~~**Splitting session from arranger**~~ — **done, E3 task 9.** `song_midi_learn` and `configure_song_macros` used to stay visible in the arranger because `MenuContext` could not tell the two apart. The conflation was three-way — `sound_editor.cpp:1762-1765 setup` selects the song root for `sessionView`, `arrangerView` *and* `automationView`, and legacy hides these two from all but the first — and is now carried by two bools rather than three: `context_for` takes `session_view_root` (`getRootUI() == &sessionView`) beside the existing `session_or_arranger_root` (`!rootUIIsClipMinderScreen()`), `MenuContext::is_session_view` reports it, and `relevance::is_session_view` guards both nodes. Nothing distinguishes the arranger from the song automation view, because no legacy `isRelevant` does. Cost as built: the one parameter, one field and one predicate the estimate named, plus **85** positional-bool call sites (not ~62 — the estimate undercounted) and two `available_when` keys in the dump JSON, which moved no node id. | `menu/nodes/actions.h`, `menu/context.h`'s `is_session_view` | — |
| **Mod-matrix pad blinking** — `PatchCables::blinkShortcuts`/`blinkShortcutsSoon`/`timerCallback` and the three `shouldBlinkPatchingSourceShortcut` overrides. `MenuScene::render_grid` keys its single blink cycle off the focused *node*; consulting the focused `CustomItem` needs a hook and a second blink source. | detailed below | same shape of change as routing MIDI to `MenuScene::learn_*` |
| ~~**The row-scrolling path is untested, not missing.**~~ — **done, E3 task 10.** `ui/oled/list.cpp:156` does run `scroll_text` on the focused row with motion allowed, and `MenuScene::render_vertical` hands it `st_.scroll[depth]`. `tests/spec/ui/list_spec.cpp`'s "scrolls a selected label..." already pinned the widget's own mechanics; what was missing was the OTHER half, a real focused over-long node driven through `MenuScene` itself. `menu_scene_spec.cpp`'s "a SELECTED over-long row scrolls, held across quiet ticks" opens the real `nodes::runtime_features` page, parks the cursor on `feature_alt_delay_params` ("Alternative Golden Knob Delay Params"), and holds across quiet ticks past `kScrollTime` -- mutation-checked by reverting `list.cpp`'s selected-row draw to plain `text()`, which fails it. | E2 task 12's record; `menu_scene_spec.cpp` | — |
| **Seven self-duplicate goldens owed cleanup** — `menu_int/{after_select_press,after_select_turn}`, `menu_enum/{after_select_press,after_gold_turn}`, `menu_toggle/{after_select_press,after_gold_turn}`, `menu_shortcut/after_select_turn` are each byte-identical to their own `idle`, i.e. named for an event they do not show. | E2 task 12's record | E2 task 9 deleted three of exactly this kind, so leaving seven is an inconsistency, not a policy |
| **`perform_editor`'s owed sync calls** — the displayed mode mirrors two `PerformanceView` bools. | the action seam section above; `menu/nodes/actions.h` | four calls into `menu::performance_editing_mode()` |
| **The action seam's dispatcher** — nothing consumes `MenuScene::take_action()`. | `menu/action.h`, the action seam section above | one `switch` over five enumerators, plus per-tick polling (the slot is a one-shot mailbox) |
| ~~**`CustomItem`'s missing backward arm**~~ — **done, E3 task 10.** `CustomItem::on_focus_backward` (`node.h`) is what `ascend()` calls now, instead of the old blanket `Kind::Custom` exclusion; do-nothing default, so every item the exclusion used to cover for free (`CvSelectionItem`, `MidiDevicesItem`, `PatchCablesItem`, `SourceSelectionItem`) is unchanged. `GateSelectionItem` (`nodes/dynamic.cpp`) is the one real override, and the reason it needs one: `gate::Selection`'s own backward arm restores `soundEditor.currentSourceIndex`, which `selectButtonPress` never writes for the minimum-off-time row (`gate/selection.h:44-47`) — so "keep the cursor" leaves it on that row, where legacy snaps back to the last real output. Pinned by `menu_scene_spec.cpp`'s task-10 example, which fails on the old exclusion. | `menu/node.h`, `menu/nodes/dynamic.{h,cpp}`, `menu_scene.cpp`'s `ascend()` | — |

Beyond the menu tree, the rest of spec E §2's scene inventory (`SessionScene`, `NoteClipScene`,
`BrowserScene`, the overlays) is still unwritten, and **nothing links `deluge_scenes`** — the wiring
plan owes the entry point, the MIDI routing, the boot calls and the `MenuAction` dispatcher.

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
which is legacy's `multiRangeMenu.menuItemHeadingTo` substitution (`sound_editor.cpp:1802-1803`).

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
memory across sessions; gold-knob acceleration
(`calcNextKnobSpeed`); and real implementations behind every `nodes::kPending` placeholder this task
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
