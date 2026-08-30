# deluge_scenes

The first UI layer allowed to include the model (spec: `docs/superpowers/specs/2026-08-26-ui-scenes-menus-design.md`,
spec E of the UI stack redesign). E1 is complete as of the twelve tasks of
`docs/superpowers/plans/2026-08-27-ui-scenes-menus-1.md`: the library target and the shared CppSpec
CMake module, the l10n bridge and `scenes::deluge_profile()`, the declarative `Node` tree with all
ten root tables, `MenuContext` resolution, `bindings::read`/`write`, the `MenuScene` interpreter,
the shortcut tables, the validator, the node-id registry and `deluge_menu_dump`.

> **Read this before adding a menu item.** The shipping firmware still runs the *legacy*
> `SoundEditor` and its ~200 hand-written `MenuItem` classes under `src/deluge/gui/menu_item/`.
> **Nothing links `deluge_scenes`** — `src/CMakeLists.txt`'s `add_subdirectory(scenes)` builds the
> library, and its only consumer is its own spec executable (see "Link direction" below). So during
> the coexistence:
>
> * **A menu item users must be able to open goes into `gui/menu_item/` + `gui/ui/menus.cpp`**, the
>   legacy tree, exactly as before. That is the system the device runs.
> * **A node added here is a port of a legacy item**, kept in step with it and cross-checked
>   against it by `sim/scenes_spec/tables_*_spec.cpp`. It changes nothing a user can see until E2
>   gives `deluge_scenes` a real entry point.
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
3. No heap in library code; no wall clock (later tasks: `Scene::next_timer()` and
   `RepaintGate::request_repaint_after` are how a blink stays alive).

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

The shipped `Kind` enum is `{Submenu, Toggle, Enum, Int, Decimal, SyncLevel, Custom}` — narrower
than spec E's original draft, which also listed `Bipolar`, `Range`, `MultiRange`, `Colour` and
`Action`. Those five were dropped: zero nodes across all ten roots needed them, failing spec E §4's
own "a kind exists only if ≥3 items use it" rule at zero (see the design spec's amendments section
for the full account; `Action` returns in E2 with its first real consumer). Before adding a new
`Kind`, count actual users the same way — a kind that will not clear three real nodes belongs as a
`Custom` item instead.

### Adding a `Custom`

`Kind::Custom` is the escape hatch for a bespoke item that fits no `Kind`, capped by
`validate_tree`'s `TooManyCustom` rule. **The cap is not spec E §4's "at most 10", and the rule
counts nodes, not implementations** (fix round 2, I4; recorded as an amendment in the design spec):

* The rule used to count *distinct `CustomItem` instances*. E1 ships the shape of its bespoke items
  and points every one of them at the single shared `nodes::kPending` placeholder, so that count was
  **1** for the whole tree and `TooManyCustom` could never fire — the rule was switched off for
  exactly the phase it was meant to police.
* It now counts **nodes that point at a custom item**. The shipped tree has **59** of them
  (`validate.h`'s `kShippedCustomNodes`, asserted against the live tree by
  `sim/scenes_spec/shortcuts_spec.cpp`), behind roughly twenty distinct legacy bespoke classes --
  fifty-seven of them still share the single `kPending` placeholder; `battery_level` and
  `firmware_version` (E2 task 2) are the two real, distinct exceptions (`BatteryLevelItem`,
  `FirmwareVersionItem`), so the distinct-instance count that spelling used to be inert on is now
  3, not 1.
* So the cap is `kMaxCustomNodes == 60`, one above reality — a ratchet against the escape hatch
  growing unnoticed, not a claim that ten was ever achievable. Adding a custom node means bumping
  both `kShippedCustomNodes` and, if you have run out of room, `kMaxCustomNodes` — deliberately two
  places.

To add one:

1. Implement `CustomItem` (`node.h`): `render(OledFrame&, const MenuContext&, MenuState&)` and
   `handle(UiEvent, UiContext, const MenuContext&, MenuState&)`.
2. Point the node at a `static`/`inline const` instance via the `custom(name, &item, relevant)`
   builder. A null `custom` pointer is itself a validator error (`Issue::Kind::NullCustom`) — there
   is no "no-op custom" shorthand.
3. **If the real implementation is a later task's job** (E1 shipped the *shape* of several bespoke
   items — the file selector, the audio recorder, loop points, name editors, patch-cable pages, the
   multi-range picker — without their real behaviour), point the node at the shared
   `nodes::kPending` placeholder (`menu/nodes/sound_osc.{h,cpp}`) instead of a real implementation.
   `kPending` draws the item's title and consumes no input; `CustomItem::is_placeholder()` reports
   `true` for it, so `validate_tree` flags every node pointing there as
   `Issue::Kind::PlaceholderCustom` **unless** the caller opts in via
   `ValidateOptions::allow_placeholder_custom` — which is how a spec says out loud, in one place,
   that the tree is not yet complete rather than have that fact go unchecked.

### The relevance registry (`menu/relevance.h`, `relevance.cpp`)

Every `Node::relevant` predicate is a plain `bool(const MenuContext&)` function pointer — no
virtual dispatch, no per-node closure. `relevance::name_of(RelevantFn)` is the reverse lookup Task
10's `deluge_menu_dump` needs to print *which* predicate a node uses (not just that it has one): a
table of `{predicate, name}` rows, guarded by a `static_assert` on the row count so adding a
predicate without adding its row is a build failure rather than a dump that silently prints `"?"`.
`name_of(nullptr)` answers `"always"` — a node with no `relevant` hook is unconditionally relevant.

### The validator (`menu/validate.h`, `validate.cpp`)

`validate_tree` walks every node reachable from a set of roots and checks: every `Submenu` is
non-empty under at least one context; every `ParamRef` resolves for its context; every `Enum` has
`names` or `dynamic_names`; every shortcut cell points at a node; `Custom` count ≤ 60 — counting the *nodes* that point at a
custom item, not the distinct `CustomItem` instances, since 57 of them share the single
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
`Issue` names its node and a `Kind`; `ValidateOptions::allow_placeholder_custom` is the one opt-out,
for the `kPending` placeholders above.

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
invented shape would not even match the schema it claims to preserve. `scripts/menu_dump_check.sh`
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

## E2+ roadmap

E1 ships the menu tree, its interpreter (`MenuScene`), and the ten root tables — the rest of spec E
(§2's scene inventory: `SessionScene`, `NoteClipScene`, `BrowserScene`, the overlays, …) is later
work. Within the menu tree itself, carried to E2: `source_shortcut_pads` (needs patch cables); a
`Kind::Action` re-introduction with its first real consumer; per-pad blink frequency masks; the
`SHORTCUTS_VERSION_1` pre-3.0 shortcut layer; two context-dependent blink suppressions;
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

Still carried from that sweep: `osc0_type` / `osc1_type` → `dxMenu` on a DX7 osc
(`osc/type.h:120-125`), which waits on `dxMenu` itself — three bespoke DX items (`menus.cpp:1715`)
that are E2's browser work, not a page this task could declare honestly; the two write halves of the
CV/gate chains (`MenuContext::source_index` is not writable from a `SelectChainFn`, and both
selectors are still `kPending` placeholders whose renderer never reports a select press — see
`nodes/settings.h`'s note); and the two other dropped arms the sweep turned up, `Submenu`'s
`shouldForwardButtons()` forwarding (`submenu.cpp:187-194`) and `Automation::selectButtonPress`'s
shift-held automation delete (`automation/automation.cpp:37`), which every param item inherits.
Also carried: `Source::getOrCreateFirstRange()`
on a zero-range sample item; per-node cursor memory across sessions; gold-knob acceleration
(`calcNextKnobSpeed`); and real implementations behind every `nodes::kPending` placeholder this task
left in the tree (E2 task 2 closed one general prerequisite for those -- `Kind::Custom` now routes
input to `CustomItem::handle`, ahead of the scene's own button handling -- and gave two of the
placeholders, `firmware_version`/`battery_level`, their own finished `CustomItem`s (reproducing
`battery::Level`'s charging heuristic with `mutable` state on the item, the way legacy keeps it on
the menu item instance); the other ~55 still point at `nodes::kPending`). See the design spec's
"Amendments during implementation" section for the full, cited account of each.
