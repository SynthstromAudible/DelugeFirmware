# deluge_ui_scenes

`deluge_ui_scenes` is the first UI layer allowed to include the model. It holds the application's
**scenes** (the `ui::shell::Scene`s a `Shell` stacks: the menu interpreter, the file browser, the
slicer, the audio clip view, …), the **services** that port `View` (`ui::shell::Service`s: the gold
knobs, the global buttons, MIDI learn), and the **declarative menu tree** the menu interpreter walks.
Everything on screen comes from the `src/ui` toolkit; the shell (`src/ui/shell`) owns the stack, the
tick, input lowering and the flush. This layer is what knows about songs, sounds and clips, and it
reaches them through `ui/scenes/bind` and a `MenuContext` rather than through legacy's globals.

The library is linked into the firmware and the host app and is the menu.

A menu item is a node here: add it to the node table of its root (`menu/nodes/*.h`), and `ui-menu-dump` shows it
in `website/src/data/generated/menu-tree.json`.

## Layout

| path | what it is |
|---|---|
| `registry.{h,cpp}` | the application's `SceneRegistry` (`scene_registry()`), the menu singleton (`menu_scene()`), `assigned_node_ids()`, and the boot-wiring record |
| `l10n_bridge.{h,cpp}`, `ui/scenes/profile.{h,cpp}` | the l10n lookups the shell's profile leaves null, and `scenes::deluge_profile()` that fills them in |
| `menu/` | the `Node` data model (`node.h`), `MenuContext` (`context.h`), `bindings`, the ten root tables (`menu/nodes/`), the shortcut tables, the validator, the node-id registry, and `MenuScene` with its units |
| `bind/` | `deluge_ui_scenes_bind`, its own library: what a node edits (`bind::Target`), read from the model's records and the engines' descriptors; the ports (`bind/ports/`, collected in `bind::Ports`) a scene reads and writes the host through; writes leave as requests |
| `rename/`, `context_menu/`, `browser/`, `marker/`, `recorder/` | the scenes a menu item opens |
| `overlay/` | the param popup, the count-in digit and the screensaver drawing |
| `slicing/`, `audio_clip/`, `timeline/` | the slicer, the audio clip view (the first root scene), and the scroll/zoom state clip views share |
| `services/` | `ModKnobs`, `GlobalControls`, `MidiLearn` and what they share (knob pushes, source names, view state, the waveform read and paint, the peak retry, favourites) |

| `SceneKind` | scene |
|---|---|
| `FirstApp` | `menu::MenuScene` |
| `FirstApp + 1` | `rename::RenameScene` |
| `FirstApp + 2` | `context_menu::ContextMenuScene` |
| `FirstApp + 3` | `browser::BrowserScene` |
| `FirstApp + 4` | `marker::MarkerScene` |
| `FirstApp + 5` | `recorder::RecorderScene` |
| `FirstApp + 6` | `overlay::ParamPopupScene` |
| `FirstApp + 7` | `overlay::CountInScene` |
| `FirstApp + 8` | `slicing::SlicerScene` |
| `FirstApp + 9` | `audio_clip::AudioClipScene` (root) |
| `Screensaver` | `overlay::ScreensaverScene` — the one shell-owned kind the application fills in, because `shell::Screensaver` pushes it by name and the shell leaves that slot alone |

`registry.h`'s file comment is the authority for the numbering. Every scene is a function-local
static and the stack stores `SceneId` values, so popping a scene runs `on_exit` and destroys
nothing.

## Layer rules

`scripts/check_layer_rules.py` enforces these; the include list itself is the comment at the top of
`src/ui/scenes/CMakeLists.txt`.

* **Includes.** `ui/scenes/`, `ui/shell/`, `ui/`, `ui/gfx/`, `base/`, `libdeluge/`, the standard library,
  and — as the model-aware layer — `model/`, `modulation/`, `playback/`, `processing/`, `storage/`,
  `io/`, `gui/l10n/`, the engines' and model's words (`engines/`, `plugin/`, `serialization/`,
  `sequence/`, `project/`, …) and three root-level headers named in that comment
  (`definitions_cxx.hpp`, `src/deluge/deluge.h` and the generated `version.h`, each included by its bare
  name). Never the rest of `gui/`, never `hid/` (`scenes-includes-outside`, `scenes-includes-gui`). The
  checker cannot see a slash-less include, so a fourth root-level header must be justified in that
  comment by hand.
* **No model stack, no Edits, no engine pointers.** `model/model_stack.h` and
  `modulation/automation/auto_param.h` are not included anywhere in the layer
  (`scenes-model-stack`); a node edits through `bind::Target` and its write leaves as a request a
  host applies as an Edit (`bind::WriteSink`, the edit sink, and the other write ports), so the layer names no `model/edits/` header
  (`scenes-no-edits`) and holds no `Sound*`, `ParamManager*`, `Output*`, `AudioClip*`, …
  (`scenes-no-engine-pointers`). `currentSong` is never named (`scenes-currentSong`): the song
  arrives through `MenuContext` or `Services`.
* **No legacy UI.** No root-UI global or UI-stack call (`scenes-no-root-ui`: `getRootUI(`,
  `openUI(`, `sessionView`, `renameClipUI`, `loadSongUI`, …), no `display->` (`scenes-no-display`), no legacy output
  driver (`scenes-no-legacy-hid-drivers`: `uiNeedsRendering(`, `PadLEDs::`, `indicator_leds::`,
  `uiTimerManager.`), no legacy waveform renderer or navigator (`scenes-no-legacy-waveform`), no stem-export process
  call (`scenes-no-stem-export-process`). Where a ported item has to run a root-UI behaviour it names it for the host
  instead — see "The action seam".
* **Drawing.** No `OledFrame`/`GridFrame` primitive (`scenes-no-frame-primitives`): pixels come from
  `ui::widgets::oled` and `ui::widgets::grid` widgets, so layout rules live in one place.
* **No wall clock** (`shell-no-clock`): the frame's `now_ms()` and `tick(dt)` are the only time a
  scene knows, and `next_timer()` is how it asks to be woken.
* **No input lock** (`scenes-no-input-lock`): no scene files `Action::Kind::LockInput`.
* **The file catalogue is dispatched only through the listing seam**
  (`scenes-listing-through-the-seam`): `FileCatalogue`'s six dispatching entry points are named only
  in `browser/listing.{h,cpp}`.
* **A service never claims the OLED from its timer** (`services-no-oled-timer-claim`).
* **No heap in library code.** Static footprint is the thing to watch instead: the node-id registry
  is two arrays sized by `kMaxNodes` (1024), 8 KB on the device.
* Style: `snake_case` functions and variables, `PascalCase` types, `k` constants, namespace
  `deluge::ui::scenes`.

## Building and testing

`src/CMakeLists.txt` and `src/platform/host/CMakeLists.txt` add this directory after `deluge_app`, because
`deluge_ui_scenes` compiles against it. The link is `$<COMPILE_ONLY:deluge_app>` (a real link of an
OBJECT library would archive all of `deluge_app` into this one), so **every consumer of
`deluge_ui_scenes` must link `deluge_app` itself**. The Rust BSP's build archives both into the
firmware.

| tree | what it holds |
|---|---|
| `tests/unit/ui/scenes/menu/`, `tests/unit/ui/scenes/services/` | the parts that need no model: the `Node` builders, the id registry, the validator over fake trees, view state |
| `tests/unit/ui/scenes/bind/` | `deluge_ui_scenes_bind` against a `project::Song` with no engine runtime |
| `tests/integration/ui/scenes/` | the model-backed specs, linked into the host app and run one file per process by the `ui-scenes-spec` harness (`tests/registry.toml`); the table cross-checks against legacy, every scene, every service, the level-2 scripts |
| `tests/integration/scenarios/menu_dump.cpp` | the `ui-menu-dump` gate: the app dumps every root as JSON, which must equal `website/src/data/generated/menu-tree.json`; `./dbt test --update ui-menu-dump` regenerates it |

Nothing under `src/ui/scenes` depends on a spec tree existing.

### The l10n bridge and the profile

`src/ui/shell` may not include `gui/l10n/`, so `ui::shell::deluge_profile()` leaves
`BoardProfile::text`/`error_text` null. This layer may, and supplies them: `text_lookup` is
`l10n::getView`, and `error_lookup` is a hand-kept copy of `getErrorMessage(Error)`'s switch
(`hid/` cannot be called from here). `scenes::deluge_profile()` is the shell's profile copied
field-for-field with those two filled in. **The application and every scenes spec pass
`scenes::deluge_profile()`**, never the shell's; `tests/integration/ui/scenes/profile_spec.cpp` asserts every
other field stays identical to the shell's copy.

## The menu tree (`menu/`)

`menu/node.h` is the data model: `Node`, `Binding`, `ParamRef`, `Kind`, `Layout`, and the
`constexpr` builders (`param`, `toggle`, `enumeration`, `submenu`, `custom`, …) that assemble a tree
as plain data. The ten root tables live in `menu/nodes/` (one file per legacy subtree), gathered by
`MenuScene::roots()` and indexed by `MenuContext::Root`. Each table carries a
`| node | legacy global | legacy class | kind |` comment naming where every node came from.

`MenuContext` (`context.h`) is everything a node needs to know about what it edits; `context_for`
resolves one from the song and the view, so UI facts arrive as arguments (`is_session_view`,
`session_or_arranger_root`, …) rather than as reads of `getRootUI()`. `menu::bindings` is the only
place the menu reads and writes: a node's `Binding` is a `bind::Target` or a `get`/`set` pair, and
`bindings::read`/`write`/`relevant` turn that data into a value, a write request, or a visibility
answer.

### The menu scene and its units

`MenuScene` (`menu_scene.{h,cpp}`) is the one scene that interprets any `Node` tree. It owns the
`MenuContext` and the `MenuState` and keeps the scene's own contracts — the lifecycle, the order
`after_render` drains its mailboxes in, the order `handle` offers an event around — and hands the
rest to units it owns by value. A unit is given the context and state per call (`MenuCore`,
`menu_core.h`, which also holds the read-only tree queries) and keeps no pointer back to the scene.

| unit | file | what it does |
|---|---|---|
| `MenuState`, `MenuTick`, `MenuWalkState` | `menu_state.h` | the path, the widgets' state, the tick's observations and the walk's choices, each reset by value |
| presentation | `present.{h,cpp}` | node text, horizontal columns, notifications, row icons |
| chains | `chains.{h,cpp}` | the horizontal-menu chains and where a named node lands |
| `MenuWalk` | `walk.{h,cpp}` | re-rooting, descending, ascending, the range picker, the remembered rows, saved state |
| `MenuView` | `menu/view.{h,cpp}` | the OLED render, and `owned_input`, the claims it makes |
| `ValueSession` | `value_session.{h,cpp}` | loading a leaf's value into the edit storage and committing it |
| `MenuLearn` | `learn.{h,cpp}` | learn mode, the LEARN button and LED, learn delivery |
| `VoiceUsageClock` | `voice_usage.{h,cpp}` | the voice usage page's counters and sampling |
| page buttons | `page_buttons.{h,cpp}` | SYNTH/KIT/MIDI/CV/CROSS_SCREEN/SCALE on a horizontal page |
| `MenuPads` | `pads.{h,cpp}` | the shortcut blinks, the SHIFT + pad jump, SHIFT + a patching-source pad, the greyout |
| shortcut blinks | `shortcut_blink.{h,cpp}` | `SoundEditor::updatePadLightsFor`'s plan (`origin/main:src/deluge/gui/ui/sound_editor.cpp`): the param pad, the patched-param fallback, the sources |
| SHIFT overlay | `shortcut_overlay.{h,cpp}` | the shortcut colour map and the patching-source column while SHIFT is held |

The per-visit facts — learn mode, the value session, the pending blink restart, the shift level the
overlay follows — sit in the scene's private `Visit`, reset by value whenever the scene is entered.
Each submenu's remembered row lives on `MenuWalk` for the life of the firmware, as `main`'s
per-`Submenu` `current_item_` does (`origin/main:src/deluge/gui/menu_item/submenu.h`);
`reset_process_state()` forgets them, as a boot does.

**Where things happen.** The toolkit's rule is that a widget draws and consumes its input in the same
call (`ui/widgets/core/frame_context.h`, rule 2: *reading is claiming*), so `render_oled` is where input is
observed, and it may not touch the model. `after_render()` turns a widget's `Response` into
navigation or a write; `handle()` gets what no widget can claim — a pad (positional, never masked)
and a button nothing owned. `owns(event)` tells the shell, before the render, which widget-owned
shapes the render will take (Back below the root, and whatever the focused page's widgets read), so
everything else reaches `handle()` first. A frame of a scene beneath the input top is **covered**:
it reads no pads, no shift and no held Select (`FrameInput::covered`).

### `MenuState`'s one-shot mailboxes, and the order `after_render` drains them

A hook with no `Result` of its own — a `CustomItem`'s `on_focus`, `handle` or `render`, a widget's
response — asks for something by writing a field on `MenuState`, and `MenuScene::after_render`
performs it and clears the slot. The six mailboxes are `range_choice`, `source_choice`,
`midi_device_choice`, the `value_changed`/`value_node` pair, `pending_action` and `pending_scene`;
`range_picker_pending` is a two-tick handshake, not a mailbox; `pending_after_range` is the picker's memory of
its item, kept until the next substitution or re-root, as `main`'s `menuItemHeadingTo` is.

**The order is a contract: `ctx_` mutations first, then navigation, then the repaint decision, then
scene pushes.** A descent decided by a `select_chain` reads the `MenuContext` the mailbox above it
just wrote, so `source_choice` draining below navigation would chain off the previous output. A new
mailbox goes in whichever band its effect falls into. The per-band table is on `MenuState`
(`menu_state.h`, "The one-shot mailboxes"); `MenuScene::drain_trace()` witnesses the order, and
`tables_3_spec.cpp`'s *drains the selector's output index BEFORE the navigation arm* fails if
`source_choice` moves.

### Relevance, validation, ids and the dump

* **Relevance.** A node's `relevant` is a plain `bool(const MenuContext&)` (a target node is shown
  when its descriptor's conditions hold). `relevance::name_of` is the reverse lookup the dump
  prints; its row table is guarded by a `static_assert` on the row count, so a predicate without a
  row is a build failure rather than a `"?"` in the dump. `name_of(nullptr)` is `"always"`.
* **The validator** (`validate.h`). `validate_tree` walks every reachable node and checks: no empty
  `Submenu`, every `ParamRef` resolves, every `Enum` has names, every shortcut cell points at a node,
  no `NullCustom`, no `DuplicateName` among siblings that can be on screen together, the
  `range_specific` and half-precision sets equal what the caller expects, at most
  `kMaxCustomNodes` custom nodes, and no `PlaceholderCustom` — unconditionally; the one opt-out left
  is `skip_shortcut_sweep`, for a caller validating a subtree.
* **Node ids** (`node_id.h`). `assign_node_ids(roots)` numbers every statically reachable node
  depth-first, 1..n (0 is "no node"). A menu position crosses a scene boundary as a 16-bit
  `SceneId::arg`, so the shortcut-pad jump, `save_state`/`restore_state` and a deep link all name a
  node by id, and a `.state` golden spells the open node `app16/<id>`. **The application must have a
  numbered tree**: `assigned_node_ids()` (`registry.h`) numbers it once, and `menu_scene()`,
  `scene_registry()` and `deluge_boot` all call it; on an unnumbered tree those three features
  silently do nothing. Only `Node::children` is walked — a `dynamic_children` node's children have
  no id.
* **The dump** fails closed on a `dynamic_children` node (it prints the node and exits non-zero)
  rather than invent a shape the schema does not have. No shipped node sets `dynamic_children`: a
  runtime list (MIDI devices, CV/gate outputs) is a self-drawing `CustomItem`.

### Adding a node

1. Find the `MenuItem` subclass in `main` the node matches (under `origin/main:src/deluge/gui/menu_item/`; the base is
   `origin/main:src/deluge/gui/menu_item/menu_item.h`) and read its `getName`/`getTitle`/`isRelevant`/
   `getValue`/`setValue`/`checkPermissionToBeginSession`/`selectButtonPress`.
2. Pick a builder matching its `Kind`, and add the node to the relevant `menu/nodes/<subtree>`
   table with a row in that table's comment.
3. Bind it: a `bind::Target` when the value is a setting, a param or a cable the descriptors name;
   otherwise a `get`/`set` pair. If its relevance is model state, write or reuse a predicate in
   `relevance.cpp` and add its row.
4. If a select press opens another page, give it a `select_chain` and list every node that hook may
   return in `chain_targets`, so ids, the validator and the dump reach pages no `children` span
   names.
5. Add the row to the `tables_*_spec.cpp` that walks its root, and run
   `python3 scripts/check_layer_rules.py` and `./dbt test --update ui-menu-dump`.

### Adding a `Kind`

The `Kind` enum is `{Submenu, Toggle, Enum, Int, Decimal, SyncLevel, Range, Custom}`. **A kind exists
only if at least three nodes use it.** `Range` is exactly three (`default_tempo`,
`default_swing_amount`, `default_key`, `main`'s `menu_item::Range` and its two subclasses,
`origin/main:src/deluge/gui/menu_item/range.h`), pinned by
a spec so a fourth consumer or a lost third is a deliberate change. A legacy class named after a
shape is not the test: the pad colours are plain `Enum`s with hooks, because legacy's `Colour` is a
`Selection` plus a rendering side effect. An action is not a kind either: an item names the action
for the host to run (see "The action seam"). A shape that will not clear three real nodes is a
`Custom` item.

### Adding a `Custom`

`Kind::Custom` is the escape hatch for a bespoke item. **The cap counts nodes that point at a custom
item, not distinct `CustomItem` instances**, so many nodes sharing one stand-in cannot hide behind a
count of one. The shipped tree has **62** custom nodes behind 62 distinct, real items
(`validate.h`'s `kShippedCustomNodes`, asserted against the live tree by
`tests/integration/ui/scenes/shortcuts_spec.cpp`), and the cap `kMaxCustomNodes` is **63**, one above: a ratchet,
so adding one means bumping both constants on purpose. `kPendingCustomNodes` is 0 and must stay 0
(`output_items_spec.cpp` asserts it three ways).

1. Implement `CustomItem` (`node.h`). `render` and `handle` are pure; everything else has a
   do-nothing default, so implement what `main`'s item has (`origin/main:src/deluge/gui/menu_item/menu_item.h` declares
   each but `writeCurrentValue`, which is `origin/main:src/deluge/gui/menu_item/value.h`'s): `on_focus`
   (`beginSession`), `on_focus_backward`, `on_focus_is_press`, `commit` (`writeCurrentValue`), `owned_input`, `leds`
   (the LEDs `beginSession` lights), `row_icon` (`MenuItem::renderSubmenuItemTypeForOled`; the default is the submenu
   arrow), the learn hooks
   (`learnNoteOn`/`learnCC`/`learnProgramChange`/`unlearnAction`/`shouldBlinkLearnLed`), the blink
   and patching-source hooks (`patched_param_to_blink`, `source_blink`, `source_press`,
   `blink_delay_ms`), and the automation hooks (`is_automation_item`, `automation_address`; a cable item's `learn_descriptor` is also its
   identity to the automation view, `menu::automation_param`).
2. Point the node at a `static`/`inline const` instance through `custom(name, &item, relevant)`. A
   null item is a validator error. An item that must know which node it is focused as needs one
   instance per node, because `on_focus` is handed a `MenuContext`, never its own `Node`.
3. **`handle` runs first.** A focused custom item gets every event before the page buttons, the
   shortcut-pad jump and the root Back, as `main` runs a `MenuItem`'s own `buttonAction` before
   `SoundEditor::buttonAction`'s (`origin/main:src/deluge/gui/ui/sound_editor.cpp`). Return an unconsumed `Result` and
   the scene carries on.
4. A table that must declare a node before its item exists may point it at `nodes::kPending`; the
   validator then reports it, and nothing silences that.

#### `on_focus` is the forward arm only

`on_focus` runs when the item becomes focused going *down* — a descent, a `SceneId` deep link, the
shortcut-pad jump, `restore_state` rebuilding a path — which is where legacy runs `beginSession`.
`MenuWalk::ascend` never calls it: reaching a leaf going *up* calls `on_focus_backward` instead
(legacy's `beginSession(navigatedBackwardFrom != nullptr)`), whose default leaves `MenuState`
alone. `GateSelectionItem` is the one override, because keeping the cursor would leave it on the
minimum-off-time row where legacy snaps back to the last real output.

A second, separate question is whether there was a **press on this row**. `ValueSession::enter_leaf`
takes a `FocusCause` — `RowPress` from `descend_to`, `NoRowPress` from a re-target, a restore or an
ascend — and `CustomItem::on_focus_is_press()` says which items care. An item whose legacy body is
`selectButtonPress` (it acts without opening anything) sets it, so a restore or a deep link does not
replay the action; `Toggle::selectButtonPress` guards the same case. **The line is "does it act
without a press":** opening a scene the user can back out of keeps it false (`EditNameItem` must
reopen its editor on a restore); writing the model or firing a verb sets it
(`MidiDeviceLinkedItem`, `ActionItem`). `CustomItem`'s doc in `node.h` has the table of the three.

#### How a menu item opens a scene

`on_focus` returns `void` — it is also reached from `retarget()` and `restore_state()`, which have
no `Result` — so an item that opens a scene writes `MenuWalkState::pending_scene`, and
`after_render` turns it into an `Action::Kind::Push`. It sets `MenuWalkState::go_up_on_resume` at
the same moment (legacy's `shouldGoUpOneLevelOnBegin`): without it, closing the pushed scene lands
back on the item and `on_focus` opens it again. `MenuWalkState::scene_opener` records which page
opened the scene, and `MenuScene::reroot_to_scene_opener_page()` lands the user back on that page
(legacy's `maybeGetParentMenu`), which the browser's claim completion calls.

### The action seam (`menu/action.h`)

`pending_scene` works because every scene a menu opens lives in `src/ui/scenes`. Five items run a legacy root UI's
behaviour instead: entering the performance view (`openUI(&performanceView)`), the stem export,
`audioClipView.setClipLengthEqualToSampleLength()`, and the song MIDI-learn and macros context menus. Those name the
action for the host, as `MenuAction::OpenPerformanceView`, `StartStemExport`, `SetClipLengthToSampleLength`,
`OpenSongMidiLearn` and `OpenSongMacros`:

1. `MenuAction` is a **closed** enum, so a dispatcher can `switch` over it with no `default:` and
   `-Werror=switch` breaks on a new enumerator.
2. `MenuActionRequest` is the enumerator plus one `uint16_t` payload.
3. An item writes `MenuWalkState::pending_action` from `on_focus` or `render`.
4. `after_render` drains it in the `pending_action` band and parks it in `MenuScene::take_action()`.

**`take_action()` is a one-slot mailbox with a silent overwrite.** A menu cannot file two actions in
one tick, so one slot holds only while its consumer polls every tick.
`gui::ui::dispatchShellActions` (`gui/ui/shell_action_dispatch.{h,cpp}`) is that consumer, drained
from `ShellHostUI::timerCallback` after every `Shell::tick` and above the close check, so no tick
skips it. If it ever wants to be lazy, the slot has to become a queue first.

**A setting's side effect is the host's, not an action.** A setting that is a DEVICE row has no setter: its node
writes the device port (`Ports::device`), whose host store does what the item's write does, and has done it when the
write returns: both screensavers take a new mode or timeout, the pads dim to a new brightness and re-render for a new
colour, main's canvas and the guest's panels take a new highlighting, the play cursor leaving its slow flash clears
the tick squares, sticky shift reaches the shift machines, rounded corners restyles the display, a trigger clock's
pulses resync the ticks, a MIDI clock starts or stops, a CV output's voltage goes out again and a gate switches. A
write the device applies only after a restart (USB audio's enable) says so, and the menu raises "restart to apply".
What the open menu does with a written row is its node's `Binding::after_write`: the horizontal-menus feature's re-lays
out the menu. The file selector ends a keyboard audition the same way, through the view actions port
(`Ports::view_actions`, `end_audition(AuditionEnd::KeyboardWhenAuditioning)`).

**The browser's work goes to its host.** `BrowserScene` hands what this layer may not name to a `BrowserHost`
(`ui/scenes/browser/browser_scene.h`): `perform` takes a `BrowserChoice` (the claim of a chosen file, the folder imports
of a kit or multisamples, the slice, the deletes of a sample or a `.syx`, the MIDI-definition load, the `.syx`
cartridge read) and `preview` a `PreviewRequest`. Until one is installed (`BrowserScene::set_host`) the browser asks
`null_browser_host()`, which performs nothing. `gui::ui::wireShellBrowserHost`, which `wireShellGuestServices()`
calls, installs the application's `ShellBrowserHost` (`gui/ui/browser_host.cpp`); it performs each request
when the browser makes it. How a request ended comes back through the scene's answer methods (`deferred_load_done`,
`long_press_done`, `slice_ready`, `file_deleted`, `preview_settled`). An answer produced while its request is still
being performed (a refusal, a claim that ends in its first pass, the preview's stop) is held, and
`gui::ui::pollShellBrowserHost` delivers it after the tick, so no answer reaches a scene from inside the hook that
asked; `ShellHostUI::closeGuest` and `ShellHostUI::uisTakenDown` deliver what their stack's exits left. A new
preview drops an older preview's held answer, which would otherwise end the new one's wait. Any other answer (an
asynchronous load ending, the `.syx` read, the preview's load) is delivered when it arrives.

Two more outboxes stay beside `take_action()`. `BrowserScene::take_scroll()` parks the scroll-in the host runs
from the image store (see the file browser below); `gui::ui::pollShellBrowserHost` takes it as its last step, after
the held answers, so a scroll an answer asks for starts before the next render. The automation view's is `MenuScene::take_transition()`: it parks one
change of the value node per tick with its cause (a select release, BACK, a column button, a pad's jump;
`menu/transition.h`), and `gui::ui::dispatchMenuTransitions` (`gui/ui/shell_automation_glue.{h,cpp}`) does to
`automationView` what `main`'s sound editor does for that change. The buttons and encoders the view takes from under
the menu (CLIP, X_ENC, the gold knobs in its editor, the horizontal and vertical turns) are forwarded by the host
ahead of the shell from the same file, as are the sound editor's KEYBOARD, SESSION and Fill arms
(`forwardMenuViewButton`); `MenuScene::route_horizontal_turn` says where a horizontal turn goes.

`nodes::PerformEditorItem` shows the performance view's editing mode, which it reads as
`UiFacts::performance_editing_mode()`; the host answers it from `performanceView`'s two bools
(`gui/ui/shell_host/ui_facts.cpp`), and the item's press files `MenuAction::OpenPerformanceView`, whose dispatcher writes
them.

### The note-range picker (`menu/nodes/multi_range.h`)

`MultiRangeItem` ports `MultiRange`, the page that stands in front of a range-dependent item while a
source has more than one range and none is chosen. The header has the whole of it; four points
reach past the page.

* **Its inputs arrive in `render`.** The horizontal turn (the edge), BACK while an edge is on, and
  Shift + a select turn (insert) are owned inputs, so the picker reads them from the frame snapshot.
  At depth 0 (a shortcut opens it at the root) the menu leaves BACK to `handle()`;
  `menu::owned_input` merges the item's `back` claim so the picker sees it there too.
* **The range memory is the source's own** (`SourceResources::defaultRangeI`), read through
  `SoundFacts::default_range` and written through `SoundWrites::set_default_range`, as legacy's
  `beginSession` and flick do. Insert and delete are their own sound-write verbs, which kill the voices and
  keep the other ranges' samples.
* **Shift+SAVE reaches it from the host.** The guest's SAVE arm (`forwardMenuSaveButton`, in
  `gui/ui/shell_guest_services.cpp`) hands the press to `MenuScene::delete_range_press`.
* **The root views ask the guest through `util/sound_menu_open.h`.** `rangePickerOpen`,
  `rangeDependentMenuOpen`, `noteWithinCurrentRange` and `changeRangeForNote` answer for the
  menu, so a note played under the picker selects its range and the
  sidebar highlights the range's notes; `refreshShellRangeDisplay` repaints the root after each
  guest tick when the range, the source, the highlight or the range count changed.

### The DX7 pages (`menu/nodes/dx.h`)

The DX7 is a sound's engine, the synth mode DX7, and its pages edit that engine's patch. In DX7 mode OSC1's place in the
sound menu is the DX7's page (`source0`, named DX7; `nodes/sound_dx7.h`): the mode, drawn DX7, then transpose, amp mod,
retrigger phase and level; OSC2's page and the mixer hide. `dx_menu` ports `dxMenu`, "DX7 source": SELECT on the synth
mode in DX7 mode enters it (`mode_chain`, from the master page or the DX7's page, as SELECT on `main`'s oscillator 1
type set to DX7 did), and MOD7+SYNTH opens it on a new DX7 synth (site 11 of `gui/ui/menu_entry.cpp`'s table). It
lists the browse row,
"DX7 parameters" (the globals and "DX7 operator N") and the engine version; both lists open the parameter page,
`DxParamItem`. Eight points reach past the pages.

* **The state is legacy's singletons'.** `MenuState::dx` holds the param the page shows and the two lists' cursors,
  which `MenuScene::open` carries from one menu to the next; `MenuScene::land_dx` sets it.
* **The tables are legacy's own**, in `nodes/dx_tables.h`, compared with legacy row by row in `dx_pages_spec`. The
  32 algorithm diagrams are the menu's own art, `nodes/dx_algorithms.h`; their `.rodata` is in the scenes' SDRAM
  section.
* **Every write is the whole patch**, `SoundWrites::write_dx_patch`, over legacy's `SetDxPatch` Edit, which records no
  undo; the engine follows the bytes: an auto patch whose new algorithm needs MkI runs the MkI engine.
* **The pad blink is the item's own** (`CustomItem::own_blink`): none as the page opens, because legacy's screen entry
  unsets the blink the page sets up; the param's pad after a horizontal step. Shift + a horizontal turn where legacy
  searches the operators forever stays put (DIFFERENCES D87).
* **Shortcut pad (4,0)** on a DX7 sound opens the parameter page on the param it showed last, from a clip view (site 9)
  and inside the menu alike; oscillator 1's type pad lands on the DX7's page, on the mode (`nodes::dx_shortcut_fallback`).
  The DX7's differences from `main` are DIFFERENCES D193–D197.
* **The parameter page has its own pads** (`main` gave them only while its DX7 engine feature was on; the DX7 is
  built in here): `DxParamItem::shortcut_pad` takes every Shift + pad before it is a shortcut
  (`CustomItem::shortcut_pad`, asked first by `menu::pads`), a pad picking its param (`dx::pad_param`, the inverse of
  the blink's `dx::param_pad`), and the Shift overlay is `main`'s DX7 map (`kDxShortcutColours`).
* **The keyboard's DX column** (site 15): Shift + a column pad opens the parameter page on the operator's level, or
  with the page open toggles and carries the param as `openForOpOrGlobal` does (`dx::column_param`,
  `MenuScene::dx_column_press`, which re-targets the menu from another page). The column flashes the edited
  operator's row white, 100 ms in every 400 from each param change (`MenuScene::dx_column_flash`, read by the column
  through `util/sound_menu_open.h`'s `dxColumnFlashRow`), and over the isomorphic keyboard a keyboard scaling param
  blinks its breakpoint note's pad (`dx::breakpoint_pad`, frequency 2) in place of its own.
* **The browse row opens the `.syx` browser** (`browser::BrowserId::DxSyx`) over the menu. Accepting a file hands it
  to the browser host and keeps the browser up (`AcceptOutcome::DeferAndWait`) while the host reads the bank
  through `gui::ui::DxCartridgeRead`, which legacy's browser shares; a bank read closes the browser and lands on the
  cartridge page (`dx_cartridge`, `MenuScene::land_dx_cartridge`) under the browse row, so BACK reopens the browser,
  as `main`'s `DxSyxBrowser::enterKeyPress` does with `soundEditor.enterSubmenu`
  (`origin/main:src/deluge/gui/ui/browser/dx_browser.cpp`). The page lists `SoundFacts::dx_cartridge`, loads a
  program per turn through `SoundWrites::load_dx_program`, and closes the menu on SELECT
  (`MenuWalkState::exit_completely`).

## The scenes

**Choosing the predicates.** A scene that sits over the base view's pads is `is_grid_overlay()`;
`occludes_grid()` is true only when it paints every pad the scene beneath would (it stops the clocks
beneath it, so claiming it wrongly stops the scene that owns the sidebar); `oled_transparent(role)`
lets the panel beneath show through; `input_transparent()` passes input to the scene beneath (the
overlays); `grid_pad_claim()` decides, under the guest, which pads the shell takes from legacy;
`is_root()` is legacy's `RootUI`, which gates `GlobalControls`' `View::buttonAction` arms. Read the
legacy UI's own overrides (`renderMainPads`, `renderSidebar`, `renderOLED`) before choosing; each
scene's header gives its reasons.

### Rename (`rename/`)

`rename::RenameScene` replaces legacy's `RenameUI` and its four subclasses, which differed only in
three virtuals; those are a `rename::Target` row indexed by `SceneId::arg`. Three targets ship —
`Clip`, `Output`, `Drum` — because those are what the tree reaches; `renameMidiCCUI` is opened only
by the automation view. The pixels are the toolkit's (`ui::widgets::grid::text_keyboard`,
`ui::widgets::oled::text_edit`); the scene owns the typing rules and legacy's backspace auto-repeat
(`kBackspaceRepeatDelayMs` 500, then `kBackspaceRepeatMs` 80), read from `TextKeyboardState::backspace_held`.
While the hold is up, the keys, Enter, BACK, SELECT and the sidebar do nothing.

### The context menu (`context_menu/`)

`context_menu::ContextMenuScene` is one scene plus a `Descriptor` table, opened with
`Action::Kind::Push` and a `DescriptorId` argument. Nine descriptors ship: the five settings resets,
the audio clip's source selector, the sample browser's two long-press menus (SAMPLES for a kit,
LOAD FILES for a synth), and its DELETE? menu, which blinks SAVE as `main`'s saving menus do
(`Descriptor::for_saving`). The browser's folder menus use the last three hooks: `options_now` (legacy's
`isCurrentOptionAvailable`, re-asked before every render, turn and accept), `pad` (the keyboard
beneath reads the press; `passes_pads_beneath`) and `busy` (BACK, Select and pads refused while
their load runs). The window is `ui::widgets::oled::option_window`, not `ui::widgets::oled::list`: legacy insets the
label by 1, sizes the highlight at 106 and pins the cursor to the bottom row, none of which `list`
does. The box is a fixed 100×40 `PopupStyle::Permanent` square with the title drawn at x 22.

### The file browser (`browser/`)

`browser::BrowserScene` is one browser with a `Descriptor` per legacy subclass. Three ship, the three the
menu tree reaches: `BrowserId::Sample`, `BrowserId::MidiDeviceDefinition` and `BrowserId::DxSyx`.

* **`accept` has three outcomes.** `Close` and `Stay` are `main`'s; `DeferAndWait` hands the choice to the browser
  host (`BrowserHost`) and stays up until the host's completion answers `deferred_load_done`, because the sample claim,
  the MIDI definition load and the `.syx` read reach objects this layer may not name, and each of `main`'s browsers
  stays up until its load ends. A claim's success re-roots the covered menu onto the page the chain lists the file
  selector on (an oscillator's `source_group`, focused on the selector) before the browser closes over it.
* **Shift+Select on a file while editing a kit** is `main`'s whole `editingKit() && shift` branch,
  read with the press: a brand-new kit asks the browser host to slice, and the host opens the slicer
  once the file has loaded; any other kit refuses with `main`'s popup; a synth claims.
* **The long press.** Holding Select for `kLongPressMs` (400) does what
  `SampleBrowser::timerCallback` does: a popup, SAMPLES for a kit's lone drum, or LOAD FILES for a
  synth, answered once at the `next_timer` deadline. A folder menu's accept asks the browser host for the import and
  stays up; the host's completion ends it (`BrowserScene::long_press_done`).
  While a sample load runs (`CardFacts::sample_load_in_flight`) the browser refuses every button
  and key, as legacy's gate does.
* **The preview.** Wherever `main`'s `SampleBrowser` calls `previewIfPossible` (a folder's listing
  landing, a move, a typed search that lands on another file) and on leaving, the sample browser asks
  the browser host to preview the file's path, or to stop on an empty path. The host previews it with
  the shared `gui::ui::SamplePreviewer` (the legacy browser's own load and sound) and answers through
  `preview_settled` from its poll after the tick; `next_timer` polls while the answer is owed. Each load's path
  becomes `deluge::model::lastFilePathLoaded`, which the browser opens beside when its holder names no
  file in SAMPLES/ (`menu::host_last_sample_path`).
* **The pads.** `BrowserPads` is what the main pads show: `Root` (the root view greyed), `Waveform` (the preview's,
  zoomed and scrolled with X_ENC) or `Keyboard`. The preview's waveform is read at the PREVIEW address through
  `menu::host_waveforms()`, the one scene that still reaches the port by its hook ("The waveform views" below), and
  `shown_waveform` hands the slicer the view it shows of a file. Beside it the scene keeps legacy's own flags
  (`qwertyVisible`, the keys drawn, the preview showing, KEYBOARD's always-visible), because legacy's image does not
  always follow them. The keyboard appears on the first main-pad press (typing only below the favourites row) or a
  horizontal turn, and hides on every move and every folder listing unless KEYBOARD keeps it. The audition column
  stays the instrument clip view's (`GridPadClaim::AllButAudition`), and the mute column closes the browser.
  The BROWSE pad's blink is `BrowseBlink`, `main`'s `SoundEditor::blinkShortcut` timer
  (`origin/main:src/deluge/gui/ui/sound_editor.cpp`): the file selector's 200 ms blink from the
  open (the drum creator's door has none), surviving the open's own preview; stopped by a later preview, a move off a
  shown preview and a pad's or turn's keyboard reveal; re-armed at 400 ms when a preview clears. It flashes over
  whatever the pads show.
* **The scroll-in.** Where `SampleBrowser` calls `PadLEDs::horizontal::setupScroll`, the scene files a
  `ScrollRequest` in `take_scroll()`; the host runs legacy's own animation from the image store (the root view, or
  the guest grid's main columns once the waveform is drawn there), leaving the guest's main columns to it until
  `ShellHostUI::scrollFinished`.
* **The buttons.** LOAD's release toggles auto-load (not for an audio clip): each landed preview is claimed with
  `BrowserChoice::keep_open`, which neither closes the browser nor re-roots the menu. RECORD opens the recorder in
  the browser's place when no UI mode is up (a held audition pad is one), and the host answers it `DEALT_WITH` so the global RECORD stays out. Shift+SAVE on a file asks
  the host to free it (`menu::host_release_sample_file`), refusing with `main`'s popup when the song uses it, and
  otherwise opens the DELETE? menu, whose OK the browser host performs before the file leaves the list
  (`BrowserScene::file_deleted`). The vertical encoder goes to the instrument clip view beneath
  (`forwardBrowserVertical`), or recolours the current favourite with SHIFT.
* **The favourites.** While the keyboard shows, row 7 (row 6 with banks, which take row 7) draws the "SAMPLES"
  favourites through `services::Favourites` (`menu::host_favourites`, over `favouritesManager`): a press recalls
  one, Shift+press sets or unsets it, the current one flashes every 500 ms.

#### The listing seam and the catalogue claim

A scene may not block a tick, so a directory read is a seam: `storage::FileCatalogue` is legacy's
listing code (legacy `Browser` delegates into it), and `browser::Listing` is a request/poll object
over it — `request()`, `poll()` on later ticks, `take_result()`.

* **A request is refused, never overwritten:** `Busy` while anyone holds the catalogue claim,
  `ResultPending` while a landed result is uncollected. There is one `fileItems` vector, so an
  overwrite would interleave two reads over one buffer — a jam, not a loss, mitigated by polling
  every tick. There is no cancel, and `~Listing` asserts it is not in flight: own one as a scene
  member and nothing else.
* **The single-flight guard is on the resource.** `FileCatalogue::claim(owner)`/`release(owner)` is a
  CAS from `nullptr`, taken by both dispatch paths (`Listing::request` and legacy
  `Browser::beginListing`). Hold it until the consumer has finished reading `fileItems`: legacy
  releases on completion, the seam on `take_result()`.
* The claim binds the two dispatch paths only: `emptyFileItems` and friends do not check it, and the
  leaf calls (`predictExtendedText`, `getUnusedSlot`, the save UIs' name search) do not claim.

The shipped dispatcher, `scenes::run_inline`, runs the op on the interaction tier's own stack;
`tests/integration/ui/scenes/listing_spec.cpp` installs a deferring dispatcher to observe the in-flight state.

### The sample marker editor (`marker/`)

`marker::MarkerScene` ports `SampleMarkerEditor`, opened by the four loop-point items and the audio clip's waveform
item with the `MarkerType` as `SceneId::arg`. Legacy draws **no waveform on the OLED** — a title, a lock badge, a
duration and a sample count — so the OLED half is `ui::widgets::oled::sample_position` and the pad half `ui::widgets::grid::markers`
over the waveforms port's columns at the range's or the audio clip's address, navigated by
`views::waveform::WaveformView` ("The waveform views" below). One legacy render bug is fixed in the port: `renderOLED`
draws the h/m unit letters at a fixed `x = 6`, over their own digits, past a minute; `ui::widgets::oled::sample_position` draws
them at the cursor.

A zoom slides as `PadLEDs::renderZoom` does, through `ui::widgets::grid::ZoomTransition`: from the last settled
frame to the new view's bare waveform over 110 ms, a step every 50 ms, every input refused while it slides.
The playhead is the newest voice's column on every row, painted with the flash cursor Slow and flashed with it Fast; the host walks
the voices (`Waveforms::play_position`). A vertical turn reaches the clip view beneath through the host
(`forwardMarkerVertical`), and SHIFT + a main pad with no mode up is the menu's shortcut jump
(`MenuScene::shortcut_jump`), leaving the editor for that item's page, any item even over the automation view. With
the automation view as the root, SHIFT + (0,6) and (0,7) are the editor's own presses. The audition column is the
host's (`GridPadClaim::AllButClipAudition`), and auditioning is a UI mode the editor's gates read
(`UiFacts::auditioning`).

### The audio recorder (`recorder/`)

`recorder::RecorderScene` ports `AudioRecorder`'s record-to-sample UI; the output-recording half
(resampling, stem export) is not a UI and is the engine's (`deluge::recording`,
`processing/recording/foreground_recording.h`). Legacy's `process()` loop in
the item's own stack frame is the shell's tick plus a poll in `after_render`, every 10 ms.

**The take goes through its port, and the host publishes it.** Many subsystems read the foreground recording's
channel as "a recording is live", `setMonitoringMode` among them, so the take is published, not re-derived:
`bind::TakeActions::begin` makes the take and publishes it through `AudioEngine::setForegroundRecording` in one step,
`finish` unpublishes and frees a settled take in the order that keeps the pointer's visible window around the
channel's (see the use-after-free in the ledger below), and `orphan` hands a running take to the foreground recording
on an exit. The scene reads the take on `bind::AudioFacts::input_take` and its file on `input_take_path`, holds no
recorder, and opens with two calls of the opener's: the tick squares cleared (`bind::Indicators`) and a held audition
ended (`bind::ViewActions`).

The scene is a grid overlay that draws no pad region: the view beneath keeps its pads, greyed by the recorder's
all-columns mask, and the scene adds only `opened()`'s shortcut blink on its source's (x, 4) pad.

A threshold take (the song's threshold mode on) draws `main`'s level-meter screen through `ui::widgets::oled::threshold_meter`,
blinking on the audio clock as legacy does, and the take counts as started once it has captured more than its
pre-roll margin.

A take begun on the SAMPLE RECORDER page returns to its source's page with the file selector focused
(`MenuWalkState::resume_landing`), as `osc::AudioRecorder::beginSession` re-roots before it opens the recorder; one
opened on the item alone (the drum creator's) leaves the menu.

### The overlays (`overlay/`)

`ParamPopupScene` (the mod-encoder value popup) and `CountInScene` are input-transparent grid
overlays. `ScreensaverScene` is the drawing half of legacy's `Screensaver`; the policy half (idle
deadline, wake on input, the inhibitors) is `shell::Screensaver`. `Rainfall` and `Starfield` live in
`ui/widgets/oled/`, and `tests/integration/ui/scenes/support/oracle_*` compare them frame by frame against a
transcription of legacy's.

### The timeline state (`timeline/`)

`timeline::TimelineState` is the state and pure arithmetic of legacy's `TimelineView` →
`ClipNavigationTimelineView` → `ClipView` chain: a plain aggregate a root scene owns, not a scene.
`timeline.h` has the symbol-by-symbol split. One `TimelineState` is one of `Song`'s two navigation
slots; `adopt` takes it from the clip-facts port's `clip_navigation()`, whose host alone names
`NAVIGATION_CLIP`. **Every horizontal turn clears the linear-recording restore point**
(`stop_following()`), including one the gates refuse —
it is the first statement of `ClipNavigationTimelineView::horizontalEncoderAction` — and a shift
length edit does not. The `PadLEDs` scroll and zoom animations are not carried.

### The slicer (`slicing/`)

`slicing::SlicerScene` ports `Slicer`, the page that cuts a kit into equal regions or hand-placed
slices. It is not a root (`Slicer` is a plain `UI` opened over the browser) and does not use
`TimelineState`: it navigates a sample through its own `views::waveform::WaveformView`, and reads its
columns from the waveforms port at the SLICE address. `occludes_grid()` is false, because `Slicer`
overrides `renderMainPads` only and the sidebar beneath keeps painting. The directory is `slicing/`
because `scenes-no-root-ui` forbids the whole word `slicer`.

* **Everything it does to the audio, the model and the card goes through `SliceOps`**: preview,
  voices, the playhead walk, restoring the range, `begin_commit`, and the close (`closed`). The host
  (`gui/ui/shell_slice_ops.cpp`) performs them with `gui::ui::SliceCore`, the core legacy's `Slicer`
  drives. The commit is asynchronous: until `SlicerScene::commit_done` the scene refuses buttons and
  pads, then removes itself and the browser closes with it.
* **Two doors open it:** SLICE in the kit's SAMPLES menu, and Shift+Select on a file in a brand-new
  kit. Either asks the browser host to slice; the host loads the file into the preview drum
  without sounding it, and `BrowserScene::slice_ready` pushes the slicer.
* **It holds its file as a `SliceFile`, the waveform's identity and its path**, never the sample. The SLICE address
  resolves through the sample the host opened it on, and to nothing once the slicer closes (`SliceOps::closed`: BACK,
  the mute column's exit, a landed slice) or a guest close releases the host. The scene keeps its file, so a reopen on
  the same file keeps its view.
* **It opens on the browser's preview view when that shows the same file** (`BrowserScene::shown_waveform`, asked by
  the file's identity), as legacy's slicer draws through the `waveformBasicNavigator` the preview left. The columns
  are the host's, kept for that file, so the slicer's first frame shows the columns the preview read. **Reopened on
  the same file, it keeps its view.**
* **`on_resume()` is `focusRegained`**, and resets the scene. It fires only when `input_top()`
  changes hands, so a popup the slicer raised expiring above it resets nothing, and a real scene
  closing above it does; `slicer_scene_spec.cpp` asserts both.
* **The commit needs no SD-routine guard**: `doSlice` frees no `SampleRecorder`, a press is already
  refused inside a card routine (`InputLowering::set_sd_busy`), and its own re-entry hazard is
  carried as `SlicerScene::committing()`.
* **The MANUAL slice mark is clipped, not transcribed.** Clamped to the end of the sample it lands
  in column 16, the first sidebar column; legacy composes top-down and the sidebar overwrites it,
  the shell composes bottom-up, so the port paints marks inside the main columns.

### The audio clip view (`audio_clip/`)

`AudioClipScene` ports `AudioClipView`, a `RootUI`, so `is_root()` is true and `GlobalControls`'
root-gated arms (SAVE, LOAD, SYNC_SCALING, TAP_TEMPO, BACK, shift+SELECT) reach it.

* **The play cursor is painted in `render_grid`.** Legacy hands a column to
  `PadLEDs::setTickSquares` and the driver substitutes the colour per pad; the shell has one
  immediate-mode render, so the waveform is drawn and `ui::widgets::grid::tick_squares` goes last.
* **BACK while holding the horizontal encoder clears the clip**, ahead of `View`'s undo, exactly when
  `currentUIMode` equals that mode — not with the song button held too.
* **The scene carries a two-value `currentUIMode`** (holding SONG, holding X_ENC) with legacy's
  `enterUIMode`/`exitUIMode` arithmetic. The mode is a latch, not a question about held buttons:
  X_ENC with shift held enters no mode.
* An `OutputType::AUDIO` output with no name draws the type text alone (legacy's
  `drawOutputNameFromDetails` matches none of its arms), so the scene passes `name_empty = false`.
* **It reads and acts through the ports the host installs** (`set_ports`) and the song's record `open` names: the clip
  and its play state on `ClipFacts`, the clocks and the arrangement's mode on `Transport`, the tempo grab on
  `TransportActions`, its clip edits on `ClipWrites` and the edit sink, and its waveform at the
  clip's AUDIO_CLIP address ("The waveform views" below). Its timeline starts from `ClipFacts::clip_navigation`.

**A spec that compares a rendered grid against legacy must make the comparison non-vacuous.** With no
card mounted the waveform scan fails on most arms and both sides draw black, so a pad-for-pad match
proves nothing. `expect_pads_match` takes a `PadExpectation` with no default (`waveform(n)`,
`waveform_with_cursor(n)`, `waveform_without_cursor(n)`, `.and_column(x, colour)`) and asserts its
non-black floor before comparing; fold that assertion into any new comparison rather than asking
callers to precede it.

### The waveform views

The marker editor, the slicer, the audio clip view and the browser's preview draw a sample's waveform on the main
pads. They share one read and one paint, and none of them holds the sample.

* **The scenes keep no peak cache.** A frame reads its waveform's 16 columns through the waveforms port
  (`services::read_waveform`: `peaks` at the view's window, then `info`, whose range scales the bars) and paints the
  PEAK columns (`services::paint_columns`, or the audio clip view's coloured bars).
* **`src/ui/views/waveform/` holds the arithmetic** (`deluge_ui_views_waveform`, header-only, standard library only): the window
  (`Window`), the view's navigation (`WaveformView`: open, scroll, zoom and the marker pins), the column answers
  (`ColumnState`, `ColumnRead`, `PeakRead`) and the paint arithmetic (`Bar`, `bar_of`, `single_row_brightness`, in
  Q8.24 pad units), with `base/divide.h` for every division by a runtime divisor.
* **The host answers the port.** `gui/ui/shell_host/waveforms.cpp` resolves the address to its sample and reads its
  columns from the sample's peak index (`sample::waveform_columns`, `model/sample/waveform_scan.h`; the index is the
  `storage/sample/peaks` crate, filled by the cluster fill, the recorder and the background scan), so each column is the
  peak of every frame it covers and the range is everything the index has scanned. A column over a block not scanned
  yet answers PENDING and asks for that block; a scene whose read left one pending asks again
  through `services::PeakRetry`, every `kPeakRetryPollMs` (10). The marker editor, the slicer and the browser preview
  give up after `kPeakRetryGiveUpMs` (300) with none newly answered and their pending columns' index no fuller
  (`PeakRead::progress`); the audio clip view asks until none is pending,
  as `main`'s does. A read at another window starts the count over.

## The services (`services/`)

| service | what it ports | LEDs it declares |
|---|---|---|
| `ModKnobs` | the gold knobs' focus, turns, pushes and acceleration, mod-button mode, the knob indicators and mod LEDs, for the guest's menus and for the root views alike (`View::modEncoderAction`, `modButtonAction`, `modEncoderButtonAction`, `setModLedStates`, `setKnobIndicatorLevel`, recorded in the specs) | the eight mod LEDs, both indicators |
| `GlobalControls` | transport, the record combo, tempo, tap tempo, sync scaling, the BACK long press, undo/redo (`Buttons::buttonAction`'s tail and `View::buttonAction`'s global arms) | Play, Record, TapTempo, SyncScaling, Save, Load, Clip |
| `MidiLearn` | learn mode, the held thing, the flash (`View::startMIDILearn`/`endMIDILearn`, the `*MidiLearnPadPressed` family, `midiLearnFlash`) | Learn; it **overwrites** three LEDs `GlobalControls` declares |

**They live here, not in `src/ui/shell/`**, because they touch the model, and `shell-includes-outside`
keeps the shell driveable by a fake surface set with no model linked. The `Service` interface is the
shell's; every model-aware implementation is here, as with `Scene`.

* **Router order is top scene → `mod_knobs` → `global_controls` → `midi_learn`** (`ui/shell/router.h`):
  legacy is current UI first and `View` as the base-class fallback, so a scene that wants a mod
  event (the menu's MIDI-learn interception) sees it before the service. `MidiLearn` is last so its
  flash composes over `GlobalControls`' LEDs.
* **A service has no stack position**, so its timer's repaint claim is honoured unconditionally.
  `Service::timer_repaints()` defaults to arming nothing; `MidiLearn` claims the grid only, because
  every reader of the learn flash is a pad renderer.
* **A service is built over the shell it serves.** Each takes the `Shell`'s own `SceneStack&` (and
  `ModKnobs`/`GlobalControls` its `InputLowering&`) and uses it — `ModKnobs` pushes the param popup,
  `GlobalControls` gates on `stack_.input_top().is_root()` — so build the shell with `Services{}`,
  construct the services over `shell.stack()`/`shell.lowering()`, then `Shell::set_services`.
* **Installing is not sufficient.** A `ModKnobs` with no focus declares all eight mod LEDs off and
  both indicators at 0 every tick, so the application's reads and moves `View::knobFocus()`
  (`use_focus`), and the wiring re-installs the ports and the song's record, and re-seeds the view state, on every
  open. The root views reach
  the same service through `gui/ui/root_knobs` (`turn`, `section_button`, `push`), shown through the legacy
  display; no engine object has a gold-knob path of its own. `root_knobs` numbers each physical control through
  `controls::map_input` and `kDelugeControls` before the service sees it, so the service speaks sections and slots.
* **Pages are assembled in `src/ui/controls`, not here.** The owners publish their pages (`plugin::RemoteControls`);
  `ui/scenes/bind` supplies the publishers (`app_publishers`), the focus a view names and the host's reading of a chain's
  view, and `controls::current_page` / `section_pages` build the page the knobs are on. A CV track and a MIDI or gate
  drum focus on `NoControlsFocus`, whose page is empty.
* **Learned MIDI and MIDI follow are not here either.** A learned CC or pitch bend is dispatched by
  `controls::dispatch_song_cc`, `dispatch_output_cc` and `dispatch_output_bend`, and a followed CC resolved by
  `controls::resolve_follow`, both in `src/ui/controls`; each write goes through the `Sink` the host binds
  as `Ports::controls_sink` over the `Facts` the host binds as `Ports::controls`. The menu's and the pads'
  learns and unlearns write the mapping table through the learn port (`Ports::learn`, `bind::LearnSink`), which names
  a device by its `midi::DeviceId`; `controls::TakeoverStore` holds the takeover states those entries key.

## The host's ports

A scene's context holds one `bind::Ports`: a pointer per port, null where the holder provides none. The ports are
`AudioFacts`, `CardFacts`, `UiFacts`, `SoundFacts`, `ClipFacts`, `TrackFacts`, `Transport` and `Waveforms` for reads;
`ClipWrites`, `SoundWrites`, the edit sink (`writes`), `controls_sink` and `LearnSink` for writes; `TransportActions`,
`DeviceActions`, `MidiFeedback`, `TakeActions` and `ViewActions` for calls; `Indicators` for the indicators a scene
changes outside its frames; `MidiDevices` for the per-device settings; `DeviceSettings` (`device`) for the device's
own settings, by DEVICE row, read and written outside any song and its undo; and `controls::Facts` for live param
values. Their vocabulary is `project::` addresses, `plugin::` ids, `instruments::classic::`, `effects::`, `controls::`, `midi::`
and `views::waveform::` words (`ui/views/waveform/peaks.h` only), never a legacy type; a MIDI device is a `midi::DeviceId`,
which the host's registry (`io/midi/device_registry.h`) maps to its cable.

* **`Transport`** answers whether playback is on, whether a clock drives it, what it records into
  (`TransportRecording`), whether the metronome is on, the count-in's digit, whether undo is allowed, whether any
  clip waits to overdub, and whether the playback mode is the arrangement's (`arrangement_mode`).
  **`TransportActions`** is PLAY, RECORD, tap tempo, the metronome toggle, the tempo popups, undo and redo, the output
  recording's start and end, fill, and the tempo grab from an audio clip's sample length (`grab_tempo`): each a
  one-to-one wrapper of the legacy call, popups included.
* **`AudioFacts`** also answers whether the audio routine is running (`audio_busy`), whether the foreground recorder
  is resampling, what it records from (`ForegroundRecording`: `UNSET`, `NONE`, `EXTERNAL`, `INTERNAL`), the input
  take `TakeActions` holds (`input_take`, an `InputTake`, and its file, `input_take_path`), and the audio clock in
  frames (`audio_sample_timer`).
* **`ClipFacts`** also answers the song's clip navigation (`clip_navigation`: the scroll, the zoom and the triplets
  view every clip view starts from), which `timeline::TimelineState::adopt` takes.
* **`Waveforms`** is a waveform by its `WaveformAddress` (a sound's source range, an audio clip, the browser's
  preview or the slicer's file), with three reads: `info` (the length, rate and channels, the centre and span the
  bars are drawn against, whether it is a take still being written, and its identity), `peaks` (the 16 columns at a
  window, each a peak, pending or past the end) and `play_position` (the frame a voice plays there now). The
  identity is the sample's lifetime serial (`Sample::serial`, through `gui::shell_host::waveform_identity`), so
  another file, a take, or a sample made later in a freed one's memory has another. See "The waveform views" above.
* **`TakeActions`** is an input take's lifecycle (`begin`, `end_soon`, `take_levels`, `take_clipped`, `finish`,
  `orphan`), each the legacy step it names; the host holds the take between `begin` and `finish` or `orphan`.
  **`ViewActions`** ends a held audition the way the scene's legacy opener does (`end_audition`, by `AuditionEnd`).
  **`Indicators`** clears the views' tick squares at once (`clear_tick_squares`).
* **`DeviceActions`** saves the device's settings and forgets the undo history. **`MidiFeedback`** is MIDI follow's
  feedback: whether it is on, which CC follows a param, and sending one CC or every followed param's. **`MidiDevices`**
  also answers how a song names a device (`reference`).
* **How a scene gets its ports.** A scene the menu opens reads its context's: the recorder and the marker editor
  read `ctx_.ports`, the `MenuContext`'s set, and so do the menu's nodes. A scene opened outside the menu holds a
  non-owning pointer the host installs, as a context holds its `Ports`: the slicer and the audio clip scene take
  `set_ports` (the whole set, read through `bind::port_of`, which answers null when no set is installed or the set
  has no such port); each service takes `set_ports` and `set_model` (the song's record); and each overlay the one
  port it reads (`set_transport` for the count-in, `set_device` for the screensaver). `wireShellGuestServices`
  installs the services at boot and on every guest open; `gui::shell_host::install_scene_ports()` installs the
  overlays', the slicer's and the audio clip scene's. A specification installs `&menu::host_ports()` or a set over
  fakes and keeps it alive while the scene or service runs. No scene or service calls `host_ports()`.
* **A port that is not installed reads its default and does nothing when called.** `Transport` reads not playing, no
  recording, no clocks, metronome off, count 0, undo not allowed, no overdubs and not the arrangement; `AudioFacts`
  reads no foreground source, not resampling, not busy and no take; `Waveforms` reads no waveform; `MidiDevices` knows
  no device, so `MidiLearn` leaves MIDI events unhandled; `DeviceSettings` reads each row's default; every call port
  is a no-op. With no set installed, the slicer reads not auditioning, a Slow play cursor and no waveform, and wipes
  no logs; the audio clip scene reads no clip fact, makes no writes, and reads a Slow play cursor, not playing and no
  clocks; neither scene's `next_timer` reads live.
* **A menu context binds exactly the ports it is given.** A caller that wants the device port passes it.

* **The host side** is `src/deluge/gui/ui/shell_host/`, one file per port, built into `menu::host_ports()` by
  `ports.cpp`. It is the only code that names legacy objects for the scenes.
* **The menu reads and writes only through the ports.** No file under `menu/` includes a legacy runtime header (the
  `scenes-reads-outside-bind` rule counts none there), and the menu calls none of the `menu::host_*` hooks left in
  `menu/bind_host.h`: those are the browser's and the context menu's. The services, the overlays, the recorder, the
  marker editor, the slicer, the audio clip scene and the timeline count none either; the rule's remaining rows are
  the browser's, the context menu's and the rename scene's.

### The DEVICE setting group

The device's own settings are a setting group, `project::SettingGroupId::DEVICE`, with its own owner
(`SettingContext::Owner::DEVICE`, `project::DeviceAddress`), so no song reader evaluates a DEVICE row.

* **The rows** are `project::device_table()` (`src/project/device_settings.{h,cpp}`), with their paths as
  typed `project::DevicePath` constants there. They cover the defaults, the pads and screen, the recording and
  sample settings, audio (input monitoring, metronome volume, USB audio), the trigger and MIDI clocks, MIDI and MIDI
  follow, the CV and gate outputs, the community features (`project/features.h`), the play cursor, and the stem
  export's options. A CHOICE row's value is its choice's index; the few rows a scene branches on have a neutral
  `enum class` (`ShortcutsVersion`, `FlashCursor`, `FavouritesLayout` and `FeedbackAutomation` beside the table,
  `ShortcutOverlay` beside the features).
* **A scene reads** a row through `bind::DeviceSettings::read` or `reader()` (`bind/ports/device_settings.h`), or
  typed through `bind/device_reader.h`; the menu's bindings dispatch DEVICE nodes there without reading
  `MenuContext::model`. A CHOICE value outside the row's choices reads clamped into them, as a choice widget shows it.
* **A scene writes** a row through `DeviceSettings::write`, the one place a value is refused, checked against the
  row. A DEVICE write does not go through `WriteSink` or the edit log: it is not an undoable song edit, and it works
  with no song.
* **The host stores them** where the device keeps them, in `gui/ui/shell_host/device_settings.cpp`: the flash
  settings, the pad controller's play cursor, the community features, the audio engine, USB audio, the playback
  handler's clocks, the MIDI engine and MIDI follow, MIDI transpose, the CV engine and the stem export. Its `store`
  does what the legacy item's write did, side effects included, and answers `STORED_FOR_RESTART` for a value that
  takes effect only after a restart.
* **A write never saves.** The host saves the whole batch when the settings root closes
  (`gui::ui::saveSettingsBeforeClose`). The stem export's options are DEVICE rows that nothing saves.
* **The specs' side** is `tests/support/ui/scenes/` (`deluge_ui_scenes_fakes`): one fake per port, the recording write ports,
  and `port_contracts.h`, whose cases run against both a fake and the host's port.

## Repaint and timers

The shell's tick is described in `src/ui/shell/README.md`; what a scene needs from it:

* **A timer is a repaint request.** `Shell::tick` arms, at a scene's `next_timer()` deadline, the
  surfaces its `timer_repaints()` names — only where the scene can actually be drawn (at or above the
  surface's base, `Shell::oled_paintable`/`grid_paintable`). A covered scene arms nothing and is woken
  by `compute_next_wake`, so its `tick(dt)` keeps its cadence.
* **The mask is per surface, and costs differ.** The grid flush sends nothing when no pixel changed;
  the OLED flush blits whenever it rendered. Over-arming is free on the grid and a permanent blit on
  the OLED. `RepaintSurfaces`' members default to false, so `return {.grid = true};` arms the grid
  alone. `AudioClipScene` and `MidiLearn` claim the grid only, because their timers are pad
  animations; `MenuScene` claims the OLED as well only on the voice usage page.
* **A pad change from `handle()` or `after_render()`** says so with `Action::Kind::RepaintAll`, which
  reaches the grid (`Router::grid_repaint_requested()`); the marker and browser select turns do.
* **Blinks share the shell's phase.** A button LED blinks with `LedState::shared(BlinkRate::Normal)`
  or `Fast`, restarted with `LedFrame::restart` where legacy calls `blinkLed` mid-blink. A model fact
  a scene raises (a `notify::Dirty` fact) is drawn on the next tick, with no zero-millisecond timer.

## The guest

The guest is `deluge::gui::ui::ShellHostUI`, a legacy `UI` holding a `Shell` built over this
registry. It is the seam the scenes run through while the root views are legacy `UI`s: every scene
opens over one of them, and the guest hands that view whatever no scene claims. It lasts until the
root views are scenes and the shell is the root (the root-view program, `docs/dev/roadmap.md`).
`openMenu` (`util/menu_entry.h`) is the menu's door: every entry site routes through it,
and `gui/ui/menu_entry.cpp`'s table names all fourteen.

* **Pads.** `ShellHostUI::claimsGridPad` asks the input top's `grid_pad_claim()`: `All` (the shell
  takes every pad), `ShortcutsOnly` (shift-held main-grid pads; the rest go to the legacy root view
  through `forwardGridPad`), or `None` (every pad is forwarded — the settings and note-editor roots,
  as legacy's `padAction` gate). The release of a pad the shell was fed is always the shell's.
* **Open.** `wireShellGuestServices()` re-installs the services over the guest's shell and re-seeds
  what they borrow from legacy (the song, the knob focus, the view state) on every open, as at boot.
* **Close.** When nothing stacked takes input — an empty stack, or only passive overlays — the
  guest's `timerCallback` closes it. `closeGuest` clears the knob indicators, runs the note mode's
  exit, the settings save, the drum-creator and slicer releases, and hands a still-showing knob
  popup to legacy's display.
* **Teardown.** `nullifyUIs` (a song load or a clear) reaches `ShellHostUI::uisTakenDown`, which resets the stack so
  every scene's `on_exit` runs (the recorder's take ends through `RecorderScene::taken_down`, with no notice),
  disarms the shell's working animation, releases the slicer and the drum creator, and forgets the menu's song. It
  hands no pads back, saves no settings and runs no note-mode exit, as `main`'s `nullifyUIs` does none of them.
* **Activity.** Every pad, button and encoder the guest sees, forwarded or not, is screensaver
  activity (`Shell::note_activity`).
* **MIDI learn.** `PlaybackHandler` offers note-on, program change, CC and pitch bend to the current UI
  (`UI::noteOnReceivedForMidiLearn` and its three siblings). The guest routes each as a `UiEvent` MIDI kind:
  the menu learns into the row, column or item under the cursor in its own learn mode (`MenuLearn::offer`),
  then `services::MidiLearn` in its learn mode, then the base answer (legacy's learn mode, `View`).

## Boot wiring this layer does not do for itself

`registry.h`'s boot-wiring record is the authority; the short version:

* **The shortcut layout** is the device's `pads/shortcutsVersion` row, which `menu::shortcuts_version` reads through
  the device port. The host keeps it in flash byte 60 (`FlashStorage::shortcutsVersion`). The settings item writes
  it.
* **The screensaver timeout** (`ShellConfig::screensaver_idle_ms`) is pushed through
  `Shell::set_screensaver_idle_ms` from `Screensaver::settingsChanged()`, which the device port's store calls for a
  write of either screensaver row.
* **The services** are installed by `gui/ui/shell_guest_services.cpp`, from `deluge_boot` and from
  `ShellHostUI::opened()`.
* **The node ids**: `deluge_boot` calls `assigned_node_ids()` explicitly, rather than relying on the
  registry's construction reaching it first.
* **The play-cursor flash** (`PadLEDs::flashCursor`, flash byte 36) is read where it is drawn: the device's
  `pads/playCursor` row, which the audio clip view, the slicer and the marker editor read each frame: Slow
  paints the playhead, Fast flashes it through the PIC (`ui::widgets::grid::flash_tick_squares`), Off shows nothing.

## Defects this port found in shipping firmware

None introduced by the port, and one fixed here: the recorder use-after-free below, as every take now ends
through `AudioEngine::setForegroundRecording` (`main` still has it). Where the port's job is to agree with legacy and the
defect has no crash, it **reproduces** the defect and asserts both sides, because a one-sided fix
would be a silent divergence; where reproducing it would mean a crash or undefined behaviour, it does
not. They are recorded here and at the port-side sites rather than in the legacy files, which this
stack cites by line hundreds of times.

| defect | severity | where it is recorded |
|---|---|---|
| **`Slicer` declares 432 bytes of stack it never touches, twice.** `renderMainPads`' REGION arm declares `RGB myImage[kDisplayHeight][kDisplayWidth + kSideBarWidth]` and renders into the caller's `image`; `buttonAction`'s BACK arm declares the same array and renders into `PadLEDs::image`. Both are uninitialised, unread and unwritten — 8 × 18 × 3 bytes each on a small UI stack — the residue of the MANUAL arm, the one place the array is used. Harmless as written; nothing to port. | cosmetic / stack pressure | this row; `slicing/slicer_scene.cpp`'s `button_action` BACK comment |
| **`Slicer::selectEncoderAction`'s slice-count wrap fires only on an exact hit.** The REGION arm is `numClips += offset; if (numClips == 257) numClips = 2; else if (numClips == 1) numClips = 256;` — equality, and `offset` is whole detents, so a fast turn steps over both ends: 256 + 2 is 258, and from the opening 16 a single −20 turn lands on **−4**, which the page shows and nothing re-checks before `doSlice`. The port reproduces it, including the `int16_t` width. | user-visible, not a crash | `slicing/slicer_scene.cpp`'s `select_encoder`; the "steps past the wrap" and "drive the REGION count negative" examples in `tests/integration/ui/scenes/slicer_scene_spec.cpp` |
| **Use-after-free in `main`'s `AudioRecorder::finishRecording`.** It frees through the member (`discardRecorder` → `~SampleRecorder` + `delugeDealloc`) and only then clears `recorder` and `recordingSource`. In that window the pointer dangles while the channel still says LEFT/STEREO, and `AudioEngine::setMonitoringMode`, reached from `renderAudio` on the priority-0 audio task — which runs on the **preemptive** executor — tests that pair and dereferences the recorder. The `recorder != nullptr` check does not help: the pointer is non-null and freed. The port clears the channel first, then the pointer, then frees, with a compiler barrier between the stores. Every take ends that way: the pair has one writer, `AudioEngine::setForegroundRecording`, and `deluge::recording`'s `finishRecording` clears through it before it frees; `main` still frees first. | **crash / memory corruption** | `AudioEngine::setForegroundRecording`'s contract; `processing/recording/foreground_recording.cpp`'s `finishRecording` |
| **`View::modButtonAction` dereferences a pointer its sibling null-checks.** `ModControllable::getModKnobMode()` returns null for a controllable without mod-knob banks; `View::getModKnobMode` tests it, while `modButtonAction` reads `*activeModControllableModelStack.modControllable->getModKnobMode()` unguarded on a press outside the clip-minder views. `CVInstrument` is the one active mod controllable that keeps the null default, so the case is a mod button pressed while a CV track's knobs are the target. Established by reading, not reproduced. Here `knobFocusFromStack` maps a CV track to no focus, and `ModKnobs::section_button` returns on that empty focus, so the press changes nothing where `main` faults. | **crash (null dereference)** | `gui/views/knob_focus_from_stack.cpp`'s `knobFocusFromStack`; `services/mod_knobs.cpp`'s `section_button` |
| **`View::setKnobIndicatorLevel` reads an uninitialised `int32_t`.** `knobPos` is assigned when the param has an `autoParam`, when `paramId == 255`, and when the descriptor is not just a param, and left unassigned on the fourth path — no `autoParam`, `paramId != 255`, `(paramId & 0x0000FF00) == 0x0000FF00` — then passed to `indicator_leds::setKnobIndicatorLevel`, which lights the gold knob's column from whatever was on the stack. The port declares nothing for that knob, which leaves the column as it was (the shell's rule for an undeclared indicator). | undefined behaviour; a wrong indicator | `services/mod_knobs.cpp`'s `indicator_level`; `services/mod_target.h`'s `ModIndicatorInfo` |
| **`ClipView::getTickSquare` compares a scroll against the zoom.** The arm that stops following an auto-extending linear recording restores the remembered scroll only `if (newXScroll != …xZoom[NAVIGATION_CLIP])`; the guard's purpose — skip a restore that moves nothing — needs `xScroll`. So the restore is skipped whenever the remembered scroll equals the current zoom (the view stays where the follow pushed it), and performed when it equals the current scroll (a redraw, and a scroll popup, for a move of zero). Reached whenever a linear recording auto-extends past the right edge and stops. Established by driving legacy's `getTickSquare` from both sides; the port reproduces it. | user-visible, not a crash | `timeline/timeline.h`'s `tick_square` warning; `timeline/timeline.cpp`'s file comment; `tests/integration/ui/scenes/timeline_spec.cpp` |
| **`AudioClipView::setClipLengthEqualToSampleLength`'s "NO SAMPLE" arm is dead, and the live arm misleads.** The guard tests `audioClip.guide.audioFileHolder`, which `AudioClip`'s constructor points permanently at its own member, so the `else` is unreachable. With no sample loaded the taken arm adjusts by `getLoopLengthAtSystemSampleRate`, which falls back to the clip's own loop length, and tells the user "CLIP LENGTH ADJUSTED" over a change of zero. `MenuAction::SetClipLengthToSampleLength` calls the same method, so the menu reaches it too. | user-visible, not a crash | this row |

## Divergences from `main`

What a user can see differ from `main` today, beyond the features below that the scenes do not have.

| divergence | where it is recorded |
|---|---|
| Encoder acceleration is legacy's `EncoderAcceleration`, on a whole-millisecond clock where legacy's is finer, which caps the acceleration term. | `InputLowering::calc_next_knob_speed`, `MenuView`'s horizontal turn |
| For about a millisecond after the `Exit` forward empties the guest's stack, the empty stack's `NullScene` answers `GridPadClaim::All`, so a pad landing then is claimed rather than given to the root view. | `ShellHostUI::timerCallback`; `tests/ui-scripts/parity/DIFFERENCES.md` D99 |
| `openMenu` clears a kit's `affectEntire` before its `openUI`, so a refused open leaves it cleared; `main` clears after a successful `SoundEditor::setup` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`). | `gui/ui/menu_entry.cpp`; `tests/ui-scripts/parity/DIFFERENCES.md` D99 |
| The shell sends an LED only when its own record says it changed, and legacy writes LEDs behind that record, so an Off the shell never saw can go unsent; no case against `main` is known. | `src/ui/shell/leds.cpp`'s `led_send_needed` |
| A RECORD press a scene takes never toggles recording on its release; `main` toggles on such a release within 500 ms of a RECORD tap the transport took. | `GlobalControls::tail_button_action`; `tests/ui-scripts/parity/DIFFERENCES.md` D127 |
| After a sample take lands on the oscillator's page, the focused file selector's own shortcut pad blinks; `main`'s blocking recorder loop re-plans the recorder's pad after the take. | `MenuScene::on_resume`; `tests/ui-scripts/parity/DIFFERENCES.md` D128 |
| A notification's 2000 ms starts when a consumer pops it, not when it was raised, and its title and value are fitted into 46 bytes value-first, where `OLED::displayNotification` measures the untruncated title. | `Notices::drain`, `notice_notification`; `tests/ui-scripts/parity/DIFFERENCES.md` D97 |
| Shift + a horizontal turn on the DX7 parameter page at the pitch envelope's first rate, every operator off: legacy hangs, the port stays on the param. | `tests/ui-scripts/parity/DIFFERENCES.md` D87 |
| The DX7 shortcut fallback tests the pad's source on this sound, where legacy tests the source the previous menu left. | `tests/ui-scripts/parity/DIFFERENCES.md` D90 |
| The marker editor draws the h/m unit letters beside their digits; legacy draws them at a fixed `x = 6`, over the digits, past a minute. | `ui/widgets/oled/sample_position.h` |
| Toolkit features with no legacy counterpart — `GestureEvent::Hold`, the widened `SecondPad`, `ui/widgets/oled/popup.h`'s progress bar, `ui/widgets/oled/misc.h`'s `vu`/`adsr` — have no caller here, so they ship no UX; each becomes a divergence the moment a scene calls it. | this row |

## Features `main` has that the scenes do not

`main` is the truth the scenes match (read with `git show origin/main:<path>`); every way they differ is a
`tests/ui-scripts/parity/DIFFERENCES.md` row, fixed or accepted. A feature-level `main` behaviour the scenes lag is
listed here, and the specs over the frozen legacy rows pin each divergence by identity
(`tests/integration/ui/scenes/support/known_differences.h`), so porting it fails that spec until the pin goes.

| `main` | gap | what porting it takes |
|---|---|---|
| an item's own `MenuItem::checkPermissionToBeginSession` (`origin/main:src/deluge/gui/menu_item/menu_item.h`) | `permission_for` is the base rule (relevance) plus the range rule; the five scene-opener items override it and skip relevance (`FileSelector` opens on any subtractive sound). `MenuScene::on_enter` asks only the range half, so nothing diverges until something asks `permission_for` about one of them | a per-item permission hook consulted ahead of `relevant` |
| the load/save browsers | the song, preset, pattern and kit-row browsers, and `SlotBrowser`'s brand-new-name machinery; they are reached from root UIs, so the menu walk cannot exercise them | `Descriptor`s or a second scene, a `SlotBrowser` equivalent, and an oracle that is not the menu walk |
| the catalogue claim over a browsing session | the leaf calls (`predictExtendedText`, `getUnusedSlot`, the save UIs' name search) do not claim | claim-on-open/release-on-close, with the load/save browsers |
| the remaining root views | `SessionScene`, the instrument clip view, `ArrangerScene`, `KeyboardScene`, `PerformanceScene`, `AutomationScene` | a scene each |
| `ClipView`'s clip shift (Y_ENC, or X_ENC held + CLIP) | `AudioClipScene` claims it, as legacy does, and does nothing: two of `Clip::shiftHorizontally`'s arguments depend on whether the automation view is current | port it with `AutomationScene` |
| `AudioClipView::renderSidebar`'s macro arm | `View::renderMacros` while holding SONG; its reads are all outside this layer | the arm, once the macro state is reachable |
| clip-row rendering | `AudioClip::renderAsSingleRow` draws from `model/` and includes the legacy waveform renderer | moving it into `SessionScene` |
| pad MIDI learn | no scene answers `learnable_at(pad)`, so `MidiLearn::thing_pressed` gets only its own TAP/PLAY/RECORD arms | the root scenes answering it |
| `Submenu::shouldForwardButtons`' `buttonAction` (`origin/main:src/deluge/gui/menu_item/submenu.h`) | `main` forwards a horizontal page's buttons to its focused value column; the menu routes `CustomItem::handle` to the focused node, which on a horizontal page is the page | forwarding `handle` to a focused custom column; `tests/ui-scripts/parity/DIFFERENCES.md` D95 |
| the timeline's scroll and zoom animations | `TimelineState` takes the arithmetic of `initiateXScroll`/`initiateXZoom`, not the `PadLEDs` animation | the zoom through `ui::widgets::grid::ZoomTransition`, as the marker editor's; a scroll transition |
