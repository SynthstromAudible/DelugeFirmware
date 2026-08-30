# UI Scenes E2 — Custom Items, Rename and Context Menu — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the placeholder menu items E1 left behind with real implementations, and build the two scenes they need (`RenameScene`, `ContextMenuScene`), so the ported menu tree behaves like the legacy one everywhere except the file/sample family.

**Architecture:** E1 shipped the declarative tree (`src/scenes/menu/`) with 59 nodes pointing at a shared `nodes::kPending` placeholder and `CustomItem::handle` never called. This plan wires input to customs, adds the three node kinds those items actually need (`Info` for read-only text, plus a `select_chain` hook for legacy's `selectButtonPress` chaining), builds two new scenes on shipped toolkit widgets (`text_keyboard` on the grid, `popup_frame`/`list`/`text_edit` on the OLED), and then implements the placeholders in groups. The legacy classes stay in place as the comparison oracle, exactly as in E1.

**Tech Stack:** C++20, CppSpec (host suite `build/tests`, sim suite `build-sim`), CMake ≥ 3.27, the `ui`/`gfx`/`shell` libraries from plans A–D, the script harness from plan G.

**Spec:** `docs/superpowers/specs/2026-08-26-ui-scenes-menus-design.md` (§4 tree and builders, §4.1 `MenuScene`, §4.4 validation, §6 overlays, §7 migration order). Read its "Amendments during implementation" section first — it records every E1 ruling this plan inherits, including the `selectButtonPress` sweep this plan implements.

## Global Constraints

- snake_case for all new code under `src/scenes/`; façades may keep camelCase.
- The scenes library never includes from `hid/`. `gui/l10n/` is the only `gui/` include allowed (`scripts/check_layer_rules.py`, row `scenes-includes-gui`).
- `src/bsp/rza1` is retired — never edit it. Never commit `src/bsp/rust/Cargo.lock`; builds dirty it, so `git checkout --` it before committing (same for `src/deluge/memory/heaps.cpp` and `tools/deluge_run.py`).
- `python3 scripts/check_layer_rules.py` and `python3 scripts/check_platform_boundary.py` must exit 0. Baselines may only shrink.
- `docs/superpowers/` is gitignored: spec edits need `git add -f`.
- Every ported item is specced against its legacy class on the same fixture, driving **each side from an independent sentinel** — never legacy-first, which passes a port hook that does nothing (E1 final review, I1).
- `./dbt build Debug` compiles all C++ and stops at a known pre-existing `ld.lld: … Invalid record` link error from a Rust/LLVM version skew. That is the expected end state, not a failure.
- Out of scope, deferred to E3 (the file/sample family): `FILE_BROWSER`, `START_POINT`/`END_POINT`, `RECORD_TO_SAMPLE` (all in `nodes/sound_osc.h`), `midi_device_definition_linked`, `audio_clip_waveform`. They keep `nodes::kPending` and `ValidateOptions::allow_placeholder_custom` stays available for exactly those.

---

## File Structure

**New in `src/scenes/`:**
- `menu/custom/` — one file per custom-item group, each holding `CustomItem` subclasses and their node definitions: `resets.{h,cpp}`, `midi_command.{h,cpp}`, `patch_cables.{h,cpp}`, `dynamic.{h,cpp}` (MIDI devices, CV/gate outputs), `song_actions.{h,cpp}`.
- `rename/rename_scene.{h,cpp}` — `RenameScene` and its target descriptor table.
- `context_menu/context_menu_scene.{h,cpp}` — `ContextMenuScene` and its descriptor table.

**New in `src/ui/oled/`:** `text_edit.{h,cpp}` — the editable text box with cursor and horizontal scroll (legacy `QwertyUI::drawTextForOLEDEditing`). It lives in the toolkit, not in `scenes`, because E3's browser needs the same widget.

**Modified:** `menu/node.h` (`Kind::Info`, `InfoFn`, `Node::select_chain`), `menu/menu_scene.{h,cpp}` (SyncLevel arm, custom input, chaining, learn entry points), `menu/validate.{h,cpp}` (cap, placeholder accounting), `menu/nodes/*.h` (placeholder nodes become real ones), `registry.{h,cpp}` (two new scenes), `menu/menu_dump_main.cpp` (the dynamic-children shape).

---

### Task 1: `Kind::SyncLevel` has no renderer

E1 shipped `Kind::SyncLevel` with a `validate.cpp` arm but **no arm in `MenuScene::render_leaf`** (`menu_scene.cpp:392-422`), so all ten sync nodes — delay sync, arp sync, LFO1/LFO2 sync, sidechain sync, reverb sidechain sync, both swing intervals, record quantize — fall to `default:` and draw a title with no value and no way to edit it. Task 11's goldens covered int/decimal/enum/toggle/submenu/shortcut/range-permission and never rendered one.

**Files:**
- Modify: `src/scenes/menu/menu_scene.cpp` (add the arm; correct the stale `default:` comment at `:419-421`, which still names `Bipolar`/`Range`/`MultiRange`/`Colour`/`Action` — all five were dropped in E1 Task 8), `src/scenes/menu/menu_scene.h` (declare `render_leaf_sync_level`)
- Test: `sim/scenes_spec/menu_scene_spec.cpp`, goldens `tests/golden/ui/menu/menu_sync_level_{48,64}.pbm`

**Interfaces:**
- Consumes: `bindings::read/write/range` (E1 Task 4), `deluge::ui::oled::text`, `syncValueToString` (`src/deluge/model/sync.h:43`).
- Produces: `void MenuScene::render_leaf_sync_level(deluge::ui::oled::OledFrame&, const Node&)`.

- [ ] **Step 1: Write the failing spec**

In `sim/scenes_spec/menu_scene_spec.cpp`:

```cpp
it("renders a sync level as legacy's centred note-length string, and edits it", [] {
    auto fixture = sound_fixture();
    MenuScene& scene = menu_scene();
    scene.on_enter({SceneKind::FirstApp, node_id(nodes::delay_sync)});

    // value 0 is legacy's OFF arm (`sync_level.cpp`'s `drawPixelsForOled`).
    bindings::write(nodes::delay_sync, fixture.ctx, 0);
    expect(render_text_of(scene, fixture)).to_equal(std::string_view{"OFF"});

    // Any non-zero value is `syncValueToString` against the song's input tick magnitude.
    bindings::write(nodes::delay_sync, fixture.ctx, 7);
    etl::string<30> want;
    syncValueToString(7, want, fixture.song->getInputTickMagnitude());
    expect(render_text_of(scene, fixture)).to_equal(std::string_view{want.data()});

    // A select-encoder turn moves the value, as legacy's `Enumeration::selectEncoderAction` does.
    turn_select(scene, fixture, +1);
    expect(bindings::read(nodes::delay_sync, fixture.ctx)).to_equal(int32_t{8});
});
```

- [ ] **Step 2: Run to see it fail**

Run: `cmake --build build-sim --target deluge_scenes_spec && ctest --test-dir build-sim -R menu_scene`
Expected: FAIL — the rendered frame carries only the title, and the turn does not move the value.

- [ ] **Step 3: Implement the arm**

In `menu_scene.cpp`, beside `render_leaf_int`:

```cpp
void MenuScene::render_leaf_sync_level(deluge::ui::oled::OledFrame& f, const Node& node) {
    TextScratch title_scratch;
    deluge::ui::oled::title(f, text_of(node.title, title_scratch));

    // Legacy is `SyncLevel::drawPixelsForOled` (`gui/menu_item/sync_level.cpp`): the literal
    // "OFF" at zero, otherwise `syncValueToString` against the song's *input* tick magnitude,
    // drawn centred in the big face. `SyncLevel` derives from `Enumeration`, so the select
    // encoder — not the gold knobs — moves the value.
    const IntRange r = bindings::range(node, ctx_);
    if (const int8_t d = f.turn(); d != 0) {
        const int32_t next = std::clamp<int32_t>(st_.edit_int + d, r.min, r.max);
        if (next != st_.edit_int) {
            st_.edit_int = next;
            st_.render_resp.changed = true;
            st_.value_changed = true;
            st_.value_node = &node;
        }
    }

    etl::string<30> buffer;
    std::string_view text = deluge::l10n::getView(deluge::l10n::String::STRING_FOR_OFF);
    if (st_.edit_int != 0) {
        syncValueToString(uint32_t(st_.edit_int), buffer, ctx_.song->getInputTickMagnitude());
        text = std::string_view{buffer.data()};
    }
    deluge::ui::oled::text(f, f.bounds(), text,
                           {.font = f.style.big_font, .align = deluge::ui::oled::Align::Centre});

    if (f.select_pressed()) {
        st_.render_resp.entered = true;
    }
}
```

Add `case Kind::SyncLevel: render_leaf_sync_level(f, *node); break;` to the switch, and rewrite the `default:` comment to say that the only kinds reaching it are ones a future plan adds, since E1's five speculative kinds are gone.

- [ ] **Step 4: Run and record goldens**

Run: `ctest --test-dir build-sim -R "menu_scene|scenes"` — PASS. Record `menu_sync_level_{48,64}.pbm`, render each with `python3 scripts/pbm2png.py`, and **look at them**: the note-length string must be centred, fully on-panel, and not clipped at the bottom (E1 hit exactly that bug twice — `render_leaf_int`'s rect and the console's line height).

- [ ] **Step 5: Commit**

```bash
git checkout -- src/bsp/rust/Cargo.lock
git add src/scenes/menu/menu_scene.cpp src/scenes/menu/menu_scene.h sim/scenes_spec/menu_scene_spec.cpp tests/golden/ui/menu
git commit -m "fix(scenes): render and edit Kind::SyncLevel, which E1 left drawing only its title"
```

---

### Task 2: Custom items receive input; `Kind::Info` for read-only text

`CustomItem::handle` is declared and never called — `menu_scene.cpp:414-417` renders a custom and stops. Nothing can implement a custom item until input reaches it. Two of the placeholders (firmware version, battery level) are pure read-only centred text and do not deserve a `CustomItem` each, so this task also adds the small kind that serves them.

**Files:**
- Modify: `src/scenes/menu/node.h` (add `Kind::Info`, `InfoScratch`, `InfoFn`, `Node::info`, and the `info_item()` builder), `src/scenes/menu/menu_scene.cpp` (call `custom->handle`; add the `Kind::Info` render arm), `src/scenes/menu/validate.cpp` (`Kind::Info` needs a non-null `info`), `src/scenes/menu/nodes/settings.h` (`firmware_version`, `battery_level` become `info_item`s)
- Test: `tests/spec/scenes/node_spec.cpp` (builder), `sim/scenes_spec/menu_scene_spec.cpp` (dispatch + render)

**Interfaces:**
- Produces:
```cpp
// node.h
struct InfoScratch { char chars[48]; };
using InfoFn = std::string_view (*)(const MenuContext&, InfoScratch&);
constexpr Node info_item(uint16_t name, InfoFn info);   // sets kind = Kind::Info
// menu_scene.h — the repaint a self-updating Info item asks for, in ms; 0 means "never".
uint16_t MenuScene::info_repaint_ms() const;
```
- Consumes: `deluge::ui::shell::Result` and `Action` (`shell/scene.h:180-215`).

- [ ] **Step 1: Write the failing spec**

```cpp
it("delivers input to a custom item and applies the actions it returns", [] {
    struct CountingCustom final : CustomItem {
        mutable int handled = 0;
        void render(OledFrame&, const MenuContext&, MenuState&) const override {}
        Result handle(const UiEvent& e, const UiContext&, const MenuContext&, MenuState&) const override {
            if (e.kind == UiEvent::Kind::Button && e.button == ButtonId::Select && e.on) {
                handled++;
                Result r{.handled = true};
                r.actions.push_back({.kind = Action::Kind::Pop});
                return r;
            }
            return Result::unhandled();
        }
    };
    static const CountingCustom kCounting;
    static constexpr Node kNode = custom(sid(String::STRING_FOR_OK), &kCounting);

    auto fixture = sound_fixture();
    MenuScene& scene = menu_scene();
    focus_on(scene, fixture, kNode);
    const Result r = press_select(scene, fixture);

    expect(kCounting.handled).to_equal(1);
    expect(r.handled).to_be_true();
    expect(r.actions.size()).to_equal(size_t{1});
});

it("renders an Info item as centred text and asks for no repaint by default", [] {
    auto fixture = settings_fixture();
    MenuScene& scene = menu_scene();
    scene.on_enter({SceneKind::FirstApp, node_id(nodes::firmware_version)});
    expect(render_text_of(scene, fixture)).to_equal(std::string_view{kFirmwareVersionString});
});
```

- [ ] **Step 2: Run to see it fail**

Run: `cmake --build build-sim --target deluge_scenes_spec && ctest --test-dir build-sim -R menu_scene`
Expected: FAIL — `handled` stays 0; `Kind::Info` does not exist.

- [ ] **Step 3: Implement**

In `node.h`, add `Info` to `Kind` (after `Custom`), the two types above, and:

```cpp
/// @brief A read-only text leaf: legacy's `firmware::Version` / `battery::Level`, whose whole
///        behaviour is "format a string and draw it centred" (`firmware/version.h:30-32`,
///        `battery/level.h:36-41`). A `CustomItem` each would be two vtables and two files for
///        one `drawStringCentredShrinkIfNecessary` apiece.
constexpr Node info_item(uint16_t name, InfoFn info) {
    Node n{};
    n.kind = Kind::Info;
    n.name = name;
    n.title = name;
    n.info = info;
    return n;
}
```

In `menu_scene.cpp`, replace the custom arm and add the info arm:

```cpp
case Kind::Custom:
    if (node->custom != nullptr) {
        node->custom->render(f, ctx_, st_);
    }
    break;
case Kind::Info: {
    TextScratch title_scratch;
    deluge::ui::oled::title(f, text_of(node->title, title_scratch));
    InfoScratch scratch{};
    deluge::ui::oled::text(f, f.bounds(), node->info(ctx_, scratch),
                           {.font = f.style.big_font, .shrink = true});
    if (f.select_pressed()) {
        st_.render_resp.entered = true;
    }
    break;
}
```

and, in `handle()`, before the scene's own button handling:

```cpp
// A custom item owns its input while it is focused. It sees the event first and, if it
// consumed it, the scene does nothing else with it -- the same precedence legacy gives
// `MenuItem::buttonAction` over `SoundEditor::buttonAction` (`sound_editor.cpp:418`).
if (const Node* focused = st_.focused(); focused != nullptr && focused->kind == Kind::Custom
                                          && focused->custom != nullptr) {
    if (Result r = focused->custom->handle(e, ui, ctx_, st_); r.handled) {
        return r;
    }
}
```

In `validate.cpp`, add the `Kind::Info` arm reporting `Issue::Kind::NullCustom` (renamed in its doc comment to cover both hooks) when `info == nullptr`.

- [ ] **Step 4: Run**

Run: `ctest --test-dir build-sim -R "menu_scene|scenes|validate"` — PASS.

- [ ] **Step 5: Commit**

```bash
git checkout -- src/bsp/rust/Cargo.lock
git add src/scenes sim/scenes_spec tests/spec/scenes
git commit -m "feat(scenes): deliver input to custom items; add Kind::Info for read-only text"
```

---

### Task 3: `select_chain` — legacy's `selectButtonPress` chaining

E1 dropped this wholesale: `menu_scene.cpp:893-897` ascends on any leaf press, so `ArpPreset::CUSTOM` no longer opens `arpOctaveModeToNoteModeMenu` (`gui/menu_item/arpeggiator/preset_mode.h:110-118`), `unison_count` no longer chains to `stereoSpreadMenu` (`unison/count.h:72`), and `arp_octave_mode` no longer chains (`octave_mode.h:90,99`). The spec's amendment carries the complete sweep, including `Submenu::shouldForwardButtons()` and `PatchCableStrength`'s arm (that one is Task 9's).

**Files:**
- Modify: `src/scenes/menu/node.h` (`SelectChainFn`, `Node::select_chain`, `with_select_chain()`), `src/scenes/menu/menu_scene.cpp` (consult it before ascending), `src/scenes/menu/nodes/sound_arp.h`, `sound_unison.h`, `sound_voice.h`, `settings.h` (the four leaves; delete the four header comments that stood in for the behaviour)
- Test: `sim/scenes_spec/tables_2_spec.cpp` (chaining vs legacy), `sim/scenes_spec/menu_scene_spec.cpp` (navigation)

**Interfaces:**
- Produces: `using SelectChainFn = const Node* (*)(const MenuContext&, const MenuState&);` and `constexpr Node with_select_chain(Node n, SelectChainFn fn);` — returning `nullptr` means "ascend", which is exactly `MenuItem::selectButtonPress`'s null return.

- [ ] **Step 1: Write the failing spec**

```cpp
it("chains a select press to the menu legacy's selectButtonPress names", [] {
    auto fixture = sound_fixture();
    MenuScene& scene = menu_scene();

    // `preset_mode.h:110-118`: only the CUSTOM preset chains; every other value ascends.
    focus_on(scene, fixture, nodes::arp_preset);
    bindings::write(nodes::arp_preset, fixture.ctx, int32_t(ArpPreset::CUSTOM));
    press_select(scene, fixture);
    expect(scene.focused()).to_equal(&nodes::arp_octave_mode_to_note_mode);

    scene.on_enter({SceneKind::FirstApp, node_id(nodes::arp_preset)});
    bindings::write(nodes::arp_preset, fixture.ctx, int32_t(ArpPreset::UP));
    press_select(scene, fixture);
    expect(scene.focused() != &nodes::arp_octave_mode_to_note_mode).to_be_true();

    // `unison/count.h:72` chains unconditionally.
    focus_on(scene, fixture, nodes::unison_count);
    press_select(scene, fixture);
    expect(scene.focused()).to_equal(&nodes::unison_stereo_spread);
});
```

- [ ] **Step 2: Run to see it fail**

Run: `ctest --test-dir build-sim -R "menu_scene|tables_2"` — FAIL: every press ascends.

- [ ] **Step 3: Implement**

`node.h` gains the hook and builder; `menu_scene.cpp`'s leaf-press path becomes:

```cpp
// Legacy's `MenuItem::selectButtonPress` returns the menu to open, or null for "go up"
// (`menu_item.h`). E1 hard-coded the null answer; the four leaves that return a menu are named
// in the spec's amendment. The hook takes the context and state because the answer is
// value-dependent for `arp_preset` (`preset_mode.h:110-118`).
if (const Node* chained = node.select_chain != nullptr ? node.select_chain(ctx_, st_) : nullptr) {
    enter_child(*chained);
    return;
}
ascend();
```

Define the four hooks beside their nodes, e.g. in `sound_arp.h`:

```cpp
/// `preset_mode.h:110-118`: the CUSTOM preset opens the octave-mode-to-note-mode page; every
/// other preset ascends.
constexpr const Node* arp_preset_chain(const MenuContext& ctx, const MenuState&) {
    return bindings::read(arp_preset, ctx) == int32_t(ArpPreset::CUSTOM) ? &arp_octave_mode_to_note_mode
                                                                        : nullptr;
}
```

- [ ] **Step 4: Run**

Run: `ctest --test-dir build-sim --output-on-failure` — all green.

- [ ] **Step 5: Commit**

```bash
git checkout -- src/bsp/rust/Cargo.lock
git add src/scenes sim/scenes_spec
git commit -m "feat(scenes): port selectButtonPress chaining as Node::select_chain"
```

---

### Task 4: `ui::oled::text_edit` — the editable text box

Legacy's `QwertyUI::drawTextForOLEDEditing` (`gui/ui/qwerty_ui.cpp`) draws the entered text with a cursor and horizontal scroll. The grid half already shipped in the toolkit (`ui/grid/keyboard.h`'s `text_keyboard`); this is the missing OLED half. It belongs in the toolkit, model-free, because E3's browser needs the same widget.

**Files:**
- Create: `src/ui/oled/text_edit.h`, `src/ui/oled/text_edit.cpp`, `tests/spec/ui/text_edit_spec.cpp`
- Modify: `src/ui/CMakeLists.txt`
- Test goldens: `tests/golden/ui/widgets/text_edit_{short,scrolled}_{48,64}.pbm`

**Interfaces:**
- Produces:
```cpp
namespace deluge::ui::oled {
/// Cursor and scroll position for an edited string; the string itself belongs to the caller.
struct TextEditState {
    int16_t cursor = 0;     ///< Insertion point, 0..len.
    int16_t scroll = 0;     ///< First visible character.
    bool cursor_lit = true; ///< Blink phase, driven by the caller's clock.
};
/// Draws @p s in @p rect with the cursor drawn as an underline under `state.cursor`, scrolling
/// horizontally so the cursor stays visible. Ports `QwertyUI::drawTextForOLEDEditing`.
void text_edit(OledFrame& f, gfx::oled::Rect rect, std::string_view s, TextEditState& state);
}
```

- [ ] **Step 1: Write the failing spec**

```cpp
it("keeps the cursor visible by scrolling, and underlines it", [] {
    TextEditState st{.cursor = 30, .scroll = 0};
    auto f = test_frame(kDelugeStyle48);
    text_edit(f, f.bounds(), "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", st);
    expect(st.scroll > 0).to_be_true();                    // scrolled to reveal the cursor
    expect(st.cursor - st.scroll < visible_chars(f)).to_be_true();
});

it("draws nothing but the cursor for an empty string", [] {
    TextEditState st{};
    auto f = test_frame(kDelugeStyle48);
    text_edit(f, f.bounds(), "", st);
    expect(f.set_pixels() > 0).to_be_true();   // the cursor is drawn
    expect(f.set_pixels() < 20).to_be_true();  // and nothing else is
});
```

- [ ] **Step 2: Run to see it fail** — `cmake --build build/tests --config Debug` fails to compile: no such header.

- [ ] **Step 3: Implement** `text_edit.cpp`, porting the arithmetic from `qwerty_ui.cpp`'s `drawTextForOLEDEditing` verbatim (scroll window, `kTextSpacingX` advance, the underline row), generalised from "the whole panel" to `rect` the way every other widget in `src/ui/oled/` was.

- [ ] **Step 4: Run and record goldens** — `ctest --test-dir build/tests -C Debug`; render both goldens with `python3 scripts/pbm2png.py` and check the cursor sits under the right character.

- [ ] **Step 5: Commit**

```bash
git add src/ui/oled/text_edit.h src/ui/oled/text_edit.cpp src/ui/CMakeLists.txt tests/spec/ui/text_edit_spec.cpp tests/golden/ui/widgets
git commit -m "feat(ui): text_edit widget -- the OLED half of legacy's qwerty text entry"
```

---

### Task 5: `RenameScene`

Legacy has five rename UIs (`gui/ui/rename/`, 341 lines) over one `QwertyUI` base. One scene replaces them, parameterised by `SceneId::arg`.

**Files:**
- Create: `src/scenes/rename/rename_scene.h`, `rename_scene.cpp`, `sim/scenes_spec/rename_scene_spec.cpp`
- Modify: `src/scenes/registry.{h,cpp}` (register at `SceneKind::FirstApp + 1`), `src/scenes/CMakeLists.txt`, `src/scenes/menu/nodes/sound_actions.h` and `nodes/roots.h` (`edit_track_name`, `edit_clip_name`, `drum_name_edit` become customs that push it)
- Test goldens: `tests/golden/ui/scenes/rename_{empty,typed}_{48,64}.pbm`

**Interfaces:**
- Produces:
```cpp
namespace deluge::scenes::rename {
/// What one rename target reads and writes. Indexed by `SceneId::arg`.
struct Target {
    uint16_t title;                                              ///< l10n string id for the title.
    std::string_view (*read)(const menu::MenuContext&);          ///< Current name.
    bool (*write)(const menu::MenuContext&, std::string_view);   ///< False rejects the name.
    bool allow_empty = true;
};
enum class TargetId : uint16_t { Clip = 0, Output = 1, Drum = 2, MidiCc = 3, Count };
const Target& target(TargetId);
class RenameScene final : public deluge::ui::shell::Scene { /* … */ };
RenameScene& rename_scene();
}
```
- Consumes: `deluge::ui::grid::text_keyboard` / `TextKeyboardState` (`src/ui/grid/keyboard.h:140`), `deluge::ui::oled::text_edit` (Task 4), `deluge::ui::oled::title`.

- [ ] **Step 1: Write the failing spec**

```cpp
it("types a name on the grid and commits it on enter", [] {
    auto fixture = kit_fixture();
    RenameScene& scene = rename_scene();
    scene.on_enter({SceneKind(uint8_t(SceneKind::FirstApp) + 1), uint16_t(TargetId::Drum)});

    type(scene, fixture, "KICK");   // presses the pads `text_keyboard_char_at` maps to K,I,C,K
    expect(scene.entered_text()).to_equal(std::string_view{"KICK"});

    press_enter_pad(scene, fixture);
    expect(std::string_view{fixture.drum->name.get()}).to_equal(std::string_view{"KICK"});
});

it("rejects a name the target refuses and stays open", [] {
    auto fixture = kit_fixture();
    add_drum_named(fixture, "SNARE");           // legacy refuses duplicates
    RenameScene& scene = rename_scene();
    scene.on_enter({SceneKind(uint8_t(SceneKind::FirstApp) + 1), uint16_t(TargetId::Drum)});
    type(scene, fixture, "SNARE");
    const Result r = press_enter_pad(scene, fixture);
    expect(r.actions.empty()).to_be_true();     // no Pop: the scene stays up
});
```

- [ ] **Step 2: Run to see it fail** — no such scene.

- [ ] **Step 3: Implement.** `render_grid` is `text_keyboard(f, kb_state_, layout_, kTextKeyboardHomeRow)`; `render_oled` is `title()` plus `text_edit()`; `handle` maps `kTextKeyboardBackspace`/`kTextKeyboardEnter` and the horizontal encoder to cursor motion, exactly as `qwerty_ui.cpp` does. `write` failure raises a `Severity::Warn` notice through the existing notify path rather than popping.

- [ ] **Step 4: Run and record goldens.** Inspect both.

- [ ] **Step 5: Commit**

```bash
git checkout -- src/bsp/rust/Cargo.lock
git add src/scenes sim/scenes_spec tests/golden/ui/scenes
git commit -m "feat(scenes): RenameScene over the toolkit's text keyboard; three name-editor items use it"
```

---

### Task 6: `ContextMenuScene`

Legacy's `gui::ContextMenu` (`gui/context_menu/context_menu.cpp`) is a centred 100×40 window: title, rule, two visible options with left-edge inversion and a side-scroller, select-encoder scroll, a configurable accept button (`SELECT_ENC`, or `SAVE`/`LOAD` for the saving/loading variants), pad-dismiss, and greyout beneath.

**Files:**
- Create: `src/scenes/context_menu/context_menu_scene.h`, `.cpp`, `sim/scenes_spec/context_menu_scene_spec.cpp`
- Modify: `src/scenes/registry.{h,cpp}` (register at `SceneKind::FirstApp + 2`), `src/scenes/CMakeLists.txt`
- Test goldens: `tests/golden/ui/scenes/context_menu_{first,scrolled}_{48,64}.pbm`

**Interfaces:**
- Produces:
```cpp
namespace deluge::scenes::context_menu {
struct Descriptor {
    uint16_t title;                                            ///< l10n id.
    std::span<const uint16_t> options;                         ///< l10n ids, in legacy's order.
    bool (*accept)(const menu::MenuContext&, uint8_t option);  ///< False closes without acting.
    deluge::ui::shell::ButtonId accept_button = deluge::ui::shell::ButtonId::SelectEnc;
};
enum class DescriptorId : uint16_t { ResetFlash = 0, ResetCommunityFeatures, ResetMidiFollow,
                                     ResetMidiDevices, ResetAll, AudioSourceSelector, Count };
const Descriptor& descriptor(DescriptorId);
class ContextMenuScene final : public deluge::ui::shell::Scene { /* … */ };
ContextMenuScene& context_menu_scene();
}
```
- Consumes: `deluge::ui::oled::popup_rect_for`/`popup_frame` (`src/ui/oled/popup.h:57,68`), `deluge::ui::oled::list` (`list.h:123`), `deluge::ui::oled::title`.

- [ ] **Step 1: Write the failing spec**

```cpp
it("shows two options at a time and scrolls with the select encoder", [] {
    ContextMenuScene& scene = context_menu_scene();
    scene.on_enter({kContextMenuKind, uint16_t(DescriptorId::ResetAll)});
    expect(visible_options(scene)).to_equal(std::vector<std::string_view>{"NO", "YES"});
});

it("acts only on the accept button, and closes either way", [] {
    auto fixture = settings_fixture();
    bool acted = false;
    ContextMenuScene& scene = context_menu_scene();
    scene.on_enter({kContextMenuKind, uint16_t(DescriptorId::ResetCommunityFeatures)});

    // A back press closes without acting -- legacy returns false from `acceptCurrentOption`
    // only on the accept path (`context_menu.cpp`).
    const Result back = press_back(scene, fixture);
    expect(pops(back)).to_be_true();
    expect(acted).to_be_false();
});

it("is transparent over the scene beneath and greys the grid", [] {
    ContextMenuScene& scene = context_menu_scene();
    expect(scene.oled_transparent(SurfaceRole::Main)).to_be_true();  // `oledShowsUIUnderneath = true`
    expect(scene.is_grid_overlay()).to_be_true();
    expect(scene.dims_grid()).to_be_true();                          // `getGreyoutColsAndRows`
});
```

- [ ] **Step 2: Run to see it fail.**

- [ ] **Step 3: Implement**, porting `renderOLED`'s arithmetic through `popup_rect_for`/`popup_frame` plus `list` over the two-row window.

- [ ] **Step 4: Run and record goldens.** Compare against the legacy renderer the way E1's table specs do: call `gui::context_menu::resetSettings.renderOLED(OLED::main)` on the same fixture and diff the canvases.

- [ ] **Step 5: Commit**

```bash
git checkout -- src/bsp/rust/Cargo.lock
git add src/scenes sim/scenes_spec tests/golden/ui/scenes
git commit -m "feat(scenes): ContextMenuScene, pixel-compared against the legacy context menu"
```

---

### Task 7: The five reset items

`reset_settings/reset.h` opens `context_menu::resetSettings` with a `ResetSettingsAction`; the context menu performs the reset on accept.

**Files:**
- Create: `src/scenes/menu/custom/resets.h`, `resets.cpp`
- Modify: `src/scenes/menu/nodes/settings.h:256-260` (the five nodes), `src/scenes/context_menu/context_menu_scene.cpp` (the five descriptors' `accept` hooks)
- Test: `sim/scenes_spec/tables_3_spec.cpp` (the five, driven port-side against legacy's own reset paths)

**Interfaces:** Consumes Task 2's custom dispatch and Task 6's `DescriptorId`. Produces `custom::kResetFlash` … `custom::kResetAll`, each a `CustomItem` whose `handle` returns `Action{Push, {kContextMenuKind, uint16_t(DescriptorId::…)}}` on a select press.

- [ ] **Step 1: Write the failing spec**

```cpp
it("opens the reset context menu rather than resetting on the spot", [] {
    auto fixture = settings_fixture();
    MenuScene& scene = menu_scene();
    focus_on(scene, fixture, nodes::reset_community_features);

    const Result r = press_select(scene, fixture);
    expect(r.actions.size()).to_equal(size_t{1});
    expect(r.actions[0].kind == Action::Kind::Push).to_be_true();
    expect(r.actions[0].id.arg).to_equal(uint16_t(DescriptorId::ResetCommunityFeatures));
});

it("resets exactly what the legacy context menu resets", [] {
    // Port side, from a sentinel: perturb a feature away from its default, then accept.
    auto port = settings_fixture();
    runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, 1);
    accept_option(context_menu_scene(), port, DescriptorId::ResetCommunityFeatures, /*option=*/1);
    const uint32_t got = runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShiftIsSticky);

    // Legacy side, from the same sentinel on a fresh fixture.
    auto legacy = settings_fixture();
    runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, 1);
    gui::context_menu::resetSettings.setAction(gui::context_menu::ResetSettingsAction::CommunityFeatures);
    gui::context_menu::resetSettings.currentOption = 1;
    gui::context_menu::resetSettings.acceptCurrentOption();
    expect(got).to_equal(runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShiftIsSticky));
});
```

- [ ] **Step 2: Run to see it fail**

Run: `cmake --build build-sim --target deluge_scenes_spec && ctest --test-dir build-sim -R "tables_3|context_menu"`
Expected: FAIL — the select press produces no actions, because these nodes still point at `kPending`.

- [ ] **Step 3: Implement** the five customs and descriptors. One `CustomItem` parameterised by `DescriptorId` covers all five; the descriptors' `accept` hooks call the same `FlashStorage`/`runtimeFeatureSettings`/`MIDIDeviceManager` paths `context_menu/reset_settings.cpp` calls.
- [ ] **Step 4: Run** — `ctest --test-dir build-sim -R "tables|context_menu"` PASS.
- [ ] **Step 5: Commit** `feat(scenes): the five reset items, over ContextMenuScene`

---

### Task 8: The twelve MIDI-command learn items

`midi::Command` (`gui/menu_item/midi/command.h`) is a `MenuItem` with learn hooks. Legacy delivers MIDI to it through the UI: `SoundEditor::noteOnReceivedForMidiLearn` → `getCurrentMenuItem()->learnNoteOn` (`sound_editor.cpp:1550-1570`). The routing from the MIDI engine belongs to the wiring plan; this task builds the item behaviour and the scene entry points, which specs drive directly.

**Files:**
- Create: `src/scenes/menu/custom/midi_command.h`, `.cpp`
- Modify: `src/scenes/menu/menu_scene.{h,cpp}` (entry points), `src/scenes/menu/nodes/settings.h:406-417`
- Test: `sim/scenes_spec/tables_3_spec.cpp`

**Interfaces:**
- Produces on `MenuScene`:
```cpp
/// Ports `SoundEditor::noteOnReceivedForMidiLearn` / `midiCCReceived` / `pcReceivedForMidiLearn`
/// (`sound_editor.cpp:1550-1570`): forwarded to the focused custom, which decides.
bool learn_note_on(MIDICable&, int32_t channel, int32_t note);
bool learn_cc(MIDICable&, int32_t channel, int32_t cc, int32_t value);
bool learn_program_change(MIDICable&, int32_t channel, int32_t program);
void unlearn();
/// Whether the focused item wants the LEARN LED blinking (`MenuItem::shouldBlinkLearnLed`).
bool wants_learn_blink() const;
```
- Produces on `CustomItem` (default implementations return false / do nothing, so no other custom changes): `virtual bool learn_note_on(...)`, `learn_cc(...)`, `learn_program_change(...)`, `unlearn(...)`, `wants_learn_blink()`.

- [ ] **Step 1: Write the failing spec**

```cpp
it("learns a CC onto the same command slot the legacy item learns it onto", [] {
    auto port = settings_fixture();
    MIDICable& cable = port.cable;
    MenuScene& scene = menu_scene();
    focus_on(scene, port, nodes::midi_command_play);
    expect(scene.learn_cc(cable, /*channel=*/3, /*cc=*/42, /*value=*/127)).to_be_true();
    const LearnedMIDI got = midiEngine.globalMIDICommands[uint8_t(GlobalMIDICommand::PLAY)];

    auto legacy = settings_fixture();
    midiEngine.globalMIDICommands[uint8_t(GlobalMIDICommand::PLAY)] = {};
    gui::menu_item::midi::Command legacy_item{l10n::String::STRING_FOR_PLAY, GlobalMIDICommand::PLAY};
    legacy_item.learnCC(legacy.cable, 3, 42, 127);
    const LearnedMIDI want = midiEngine.globalMIDICommands[uint8_t(GlobalMIDICommand::PLAY)];

    expect(got.equalsChannelOrZone(&cable, want.channelOrZone)).to_be_true();
    expect(got.noteOrCC).to_equal(want.noteOrCC);
});

it("unlearns, and asks for the learn blink only on these twelve", [] {
    auto fixture = settings_fixture();
    MenuScene& scene = menu_scene();
    focus_on(scene, fixture, nodes::midi_command_play);
    scene.learn_cc(fixture.cable, 3, 42, 127);
    scene.unlearn();
    expect(midiEngine.globalMIDICommands[uint8_t(GlobalMIDICommand::PLAY)].containsSomething()).to_be_false();
    expect(scene.wants_learn_blink()).to_be_true();

    focus_on(scene, fixture, nodes::default_pad_brightness);
    expect(scene.wants_learn_blink()).to_be_false();
});
```

- [ ] **Step 2: Run to see it fail**

Run: `ctest --test-dir build-sim -R tables_3`
Expected: FAIL to compile — `MenuScene::learn_cc` does not exist.

- [ ] **Step 3: Implement** one `CustomItem` parameterised by `GlobalMIDICommand`, twelve node definitions, and the four `MenuScene` entry points. `render` draws the learned binding the way `Command::drawPixelsForOled` does; `handle` needs no arm, since learn arrives through the entry points rather than as a `UiEvent`.
- [ ] **Step 4: Run.**
- [ ] **Step 5: Commit** `feat(scenes): the twelve MIDI command items, with MenuScene learn entry points`

---

### Task 9: Patch cables, patch-cable strength, and the multi-range picker

`patch_cables.cpp` (192), `patch_cable_strength.cpp` (390) and `multi_range.cpp` (435) are the three biggest bespoke items. E1 left `range_picker_pending`/`pending_after_range` in `MenuState` (`menu_scene.h:165-167`) precisely for this task, and `PatchCableStrength::selectButtonPress` chains to `source_selection::rangeMenu` (`patch_cable_strength.cpp:302-306`) — the arm the spec's sweep records.

**Files:**
- Create: `src/scenes/menu/custom/patch_cables.h`, `.cpp`
- Modify: `src/scenes/menu/nodes/sound_root.h:69` (`patch_cables`), `sound_master.h:89` (`vibrato`), `sound_sidechain.h:85` (`volume_ducking`), `src/scenes/menu/menu_scene.cpp` (the range-picker descent)
- Test: `sim/scenes_spec/tables_1_spec.cpp` (the range picker on osc sample items), `sim/scenes_spec/tables_2_spec.cpp` (the three cable items)

**Interfaces:** Consumes `bindings::learn_target`, `permission_for`'s `MustSelectRange` (E1 Task 3), Task 3's `select_chain`. Produces `custom::kPatchCables`, `custom::kPatchCableStrength`, `custom::kMultiRange`.

- [ ] **Step 1: Write the failing spec** — entering a range-dependent param on a two-range sound lists both ranges by name (compared against `MultiRange::getOptions` on the same fixture) and, on selecting one, lands on the node `pending_after_range` named with `Source::getOrCreateFirstRange()` applied, which E1's carry list records as owed here.
- [ ] **Step 2: Run to see it fail.**
- [ ] **Step 3: Implement** the three customs and replace the placeholder descent.
- [ ] **Step 4: Run** — `ctest --test-dir build-sim --output-on-failure`.
- [ ] **Step 5: Commit** `feat(scenes): patch cables, cable strength, and the multi-range picker`

---

### Task 10: Dynamic submenus, and the dump's dynamic shape

`midi::Devices` lists `MIDIDeviceManager::hostedMIDIDevices` at open time (`midi/devices.cpp`), and the CV/gate output submenus (`menu_item/cv/`, `gate/`) are per-output. These are the tree's first `dynamic_children` users — and `deluge_menu_dump` **fails closed** on `dynamic_children` by design (E1 Task 10 fix round 1), so this task is where its JSON shape gets designed against a real case.

**Files:**
- Create: `src/scenes/menu/custom/dynamic.h`, `.cpp`
- Modify: `src/scenes/menu/nodes/settings.h:272,274,430` (CV, gate, devices), `src/scenes/menu/menu_dump_main.cpp` (replace the fail-closed arm), `website/src/data/generated/menu-hierarchies.json` (regenerated), `website/src/data/generated/README.md`
- Test: `sim/scenes_spec/tables_3_spec.cpp`, `sim/scenes_spec/menu_dump_spec.cpp`

**Interfaces:** Consumes `Node::dynamic_children` (`node.h:234`) and `kMaxDynamicChildren` (64). Produces the dump's dynamic entry shape.

- [ ] **Step 1: Write the failing spec** — with two hosted MIDI devices in the fixture, `nodes::midi_devices`'s `dynamic_children` yields exactly the nodes legacy's `Devices` lists, in legacy's order, with DIN first (`devices.cpp`'s `lowestDeviceNum` comment); with none, it yields DIN alone.
- [ ] **Step 2: Run to see it fail.**
- [ ] **Step 3: Implement** the three dynamic submenus. Then decide the dump's shape and write it down where the fail-closed comment now sits: the entry is `"type": "submenu"` with the children it yields **under the dump's own fixture**, plus `"dynamic": true` so a consumer knows the list is context-dependent. This keeps one shape per entry, which the retired `doc_emitter.py` also had (its `"multicontext"` carried no `children` at all because the Python pre-resolved the context upstream).
- [ ] **Step 4: Run** — `ctest --test-dir build-sim -R "menu_dump|tables_3"`, then `scripts/menu_dump_check.sh` twice: byte-identical, and the committed JSON updated in this commit.
- [ ] **Step 5: Commit** `feat(scenes): dynamic submenus for MIDI devices and CV/gate outputs; the dump's dynamic shape`

---

### Task 11: The remaining value and action items

Everything left that needs no new machinery. **The six pad colours are not a new `Kind`** — legacy's `Colour` is a `Selection` with nine named options and a `renderingNeededRegardlessOfUI()` side effect (`menu_item/colour.h:24-55`), so they are ordinary `Kind::Enum` nodes with get/set hooks. E1 Task 8's ruling that a dropped kind returns only with a real consumer holds: none of these needs one.

**Files:**
- Create: `src/scenes/menu/custom/song_actions.h`, `.cpp` (`active_scales`, `configure_song_macros`, `song_midi_learn`, `perform_editor`, `start_stem_export`, `set_clip_length_equal_to_sample_length`, `audio_source_selector`)
- Modify: `src/scenes/menu/nodes/settings.h` (6 pad colours, `flash_status`, `feature_dev_sysex`, `default_active_scale`, `shortcuts_version`, default tempo/swing/key), `nodes/roots.h`
- Test: `sim/scenes_spec/tables_3_spec.cpp`

**Interfaces:** Consumes Task 2's dispatch, Task 6's `ContextMenuScene` (for `audio_source_selector`, whose legacy is `context_menu::audioInputSelector`).

- [ ] **Step 1: Write the failing spec**

```cpp
it("writes every one of this batch's nodes exactly where legacy writes it", [] {
    // The set is built from the table, not hand-listed, so a node dropped from a root fails here
    // rather than silently leaving the oracle (E1 final review, I2).
    std::set<const Node*> covered;
    for (const Pair& p : batch4_pairs()) {
        for (const int32_t v : probe_values(p)) {
            probe_write(p, v);   // drives port and legacy independently, each from `sentinel_for(v)`
            covered.insert(p.node);
        }
    }
    expect(covered).to_equal(expected_batch4_nodes());
});

it("renders a pad colour as legacy's nine-option list, and writes the same option", [] {
    auto port = settings_fixture();
    bindings::write(nodes::pad_colour_muted, port.ctx, int32_t(gui::menu_item::Colour::CYAN));

    auto legacy = settings_fixture();
    gui::menu_item::mutedColourMenu.setValue(int32_t(gui::menu_item::Colour::CYAN));
    gui::menu_item::mutedColourMenu.writeCurrentValue();

    expect(int32_t(gui::menu_item::mutedColourMenu.value))
        .to_equal(bindings::read(nodes::pad_colour_muted, port.ctx));
    expect(names_of(nodes::pad_colour_muted, port.ctx).size()).to_equal(size_t{9});
});
```

- [ ] **Step 2: Run to see it fail**

Run: `ctest --test-dir build-sim -R tables_3`
Expected: FAIL — these nodes are `Kind::Custom` placeholders with no binding, so `bindings::read` refuses and the coverage set is short.
- [ ] **Step 3: Implement.** `shortcuts_version` also selects the shortcut table version, which E1 recorded as owed: wire it to `menu::shortcuts` and delete the "selects nothing" note from `shortcuts.cpp`'s header.
- [ ] **Step 4: Run.**
- [ ] **Step 5: Commit** `feat(scenes): pad colours, defaults, song and clip action items`

---

### Task 12: Acceptance scripts, validator accounting, dump refresh

**Files:**
- Create: `tests/ui-scripts/shell/menu_sync_level.txt`, `menu_info.txt`, `menu_chain.txt`, `rename.txt`, `context_menu.txt`; goldens under `tests/golden/ui/shell/<name>/`
- Modify: `sim/scenes_spec/menu_scripts_spec.cpp`, `src/scenes/menu/validate.h` (the cap), `src/scenes/README.md`, `website/src/data/generated/menu-hierarchies.json`

- [ ] **Step 1:** Write the five scripts on Task 11's pattern (`snapshot` on entry, one navigation, one edit, one popup, `quit`), record once, then **render every recorded `.pbm` with `python3 scripts/pbm2png.py` and look at it** — Task 11 of E1 found a clipped value digit this way, and its reviewer found a clipped console line the same way one round later. A golden nobody looked at is not evidence.
- [ ] **Step 2:** Determinism — run `ctest --test-dir build-sim -R menu_scripts` twice, `got/` byte-identical via `python3 scripts/ui_snapshot_diff.py`.
- [ ] **Step 3:** Update `kMaxCustomItems` to the count that remains after this plan (the E3 set only), and change `validate_spec` to assert the exact remaining placeholder node set by identity, so E3 closing them out is a spec change and not a silent drift.
- [ ] **Step 4:** Regenerate the dump; `scripts/menu_dump_check.sh` clean.
- [ ] **Step 5: Commit** `test(scenes): level-2 scripts for the new kinds and scenes; validator and dump refresh`

---

### Task 13: Layer rules, README, spec amendments, E3 handoff

**Files:**
- Modify: `scripts/check_layer_rules.py` + baseline, `src/scenes/README.md`, `docs/superpowers/specs/2026-08-26-ui-scenes-menus-design.md`, `docs/superpowers/specs/2026-08-26-ui-migration-design.md`

- [ ] **Step 1:** Add a ratchet row `scenes-no-legacy-ui`, `"src/scenes/**"`, `include`, `r"^gui/(ui|views|context_menu)/"`, baseline 0 — the new scenes port legacy UIs and must not include them. Prove it fires with a deliberate probe, then remove the probe.
- [ ] **Step 2:** README: how to add a custom, a context-menu descriptor, and a rename target; what `Kind::Info` is for; the E3 roadmap.
- [ ] **Step 3:** Spec E amendments — `Kind::Info`; `Node::select_chain` and which sweep entries are now implemented vs still E3's; pad colours needing no `Kind::Colour`; the dump's dynamic shape; the `SyncLevel` renderer gap E1 shipped, so the next reader knows the goldens did not cover every kind.
- [ ] **Step 4:** **Correct the migration spec's deletion schedule** (`2026-08-26-ui-migration-design.md:127-137`). Its "after phase 4" row lists `gui/menu_item/**`, `gui/ui/menus.*`, `gui/ui/sound_editor.*` — but those cannot go until E3 lands the file/sample family, because `nodes/sound_osc.h`'s file-browser, sample-marker and record-to-sample items are still placeholders. State the real precondition: `validate_tree` reports zero `PlaceholderCustom` with `allow_placeholder_custom` off.
- [ ] **Step 5: Commit** (`git add -f` for the two specs) `test(scenes): layer rule for legacy UI includes; README; spec E and migration-spec amendments`

---

## Self-review notes

- **Spec coverage.** §4's `CustomItem` interface — implementations land in Tasks 7–11, which is what §4 defers to E2. §4.1's `MenuScene` gains custom input (Task 2), chaining (Task 3) and the range picker (Task 9). §4.4 validation: the cap and the placeholder set become exact in Task 12. §6's overlay list: `ContextMenuScene` is Task 6; `CountInScene`, `ParamPopupScene` and `ScreensaverScene` stay with the services plan, as E1's scope note assigned them. §7 step 1 finishes here **except** its file/sample tail, which Task 13 records against the deletion schedule.
- **Placeholders:** none in this plan. Five placeholder nodes survive deliberately in the tree (the E3 set), named in the Global Constraints and pinned by identity in Task 12.
- **Type consistency:** `InfoScratch`/`InfoFn`/`info_item` (Task 2) are used by Task 11; `SelectChainFn`/`with_select_chain` (Task 3) by Tasks 3 and 9; `TextEditState`/`text_edit` (Task 4) by Task 5 and by E3's browser; `Target`/`TargetId`/`rename_scene()` (Task 5) by Task 11's name items; `Descriptor`/`DescriptorId`/`context_menu_scene()` (Task 6) by Tasks 7 and 11; `MenuScene::learn_*` (Task 8) by the wiring plan. `kContextMenuKind` is `SceneKind(uint8_t(SceneKind::FirstApp) + 2)`, defined once in `context_menu_scene.h`.
- **Known risks:** (a) Task 8's learn items cannot be driven end-to-end until the MIDI engine routes into the scene, so their specs call `MenuScene::learn_*` directly — real coverage of the item, none of the routing, and the plan says so rather than implying more. (b) Task 10 changes the committed dump JSON; the CI diff will fail loudly if the regeneration is forgotten, which is the net working. (c) Task 6 compares against a legacy context menu that draws through `hid::display::OLED::main` — the spec may include `hid/`, the library may not, exactly as E1's table specs do.
