# Note Family Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Gate legacy entry sites 12 and 13 onto a faithful port of the note editor and note-row editor, and first give every ported menu legacy's input mapping, which the note editor is built on.

**Architecture:** Tasks 1-2 move menu value editing to the select encoder and give horizontal pages legacy's button focus, page switching and select-press behaviour. Tasks 3-5 add three optional `Binding` hooks, a note bridge (function pointers declared in `src/scenes`, installed from `gui/ui`), and the 29 note nodes. Task 6 adds the two `MenuContext::Root` enumerators and everything indexed by them; Task 7 routes the mode's buttons, encoders, pads and exit through the guest; Task 8 gates both sites and extends the level-3 A/B; Task 9 records what shipped.

**Tech Stack:** C++20, CMake, CppSpec (`deluge_scenes_spec`, `deluge_spec`), the level-1/2/3 UI harness, Python checkers under `scripts/`.

**Spec:** `docs/superpowers/specs/2026-09-09-note-family-design.md` (revised 2026-09-10). Read §0 first: it lists what the revision corrected.

**Branch:** `ui/notes-1`, cut from `ui/pads-1` at `20aca8e4f`. **Branch in place — no worktrees.**

## Global Constraints

- **Never edit `src/bsp/rza1`.** Retired dead source.
- **Never commit `src/bsp/rust/Cargo.lock`.**
- **`git add` named paths only.** Never `git add -A` / `git add .` — an untracked `release-blockers.md` sits in the working tree and must never be committed.
- **snake_case** under `src/shell`, `src/scenes` and `src/ui`; **camelCase** under `src/deluge`.
- **No new UI/UX.** Every behaviour is a port of a named legacy statement. A comment saying "legacy has none" or "spec design, not a port" is a defect to report.
- **Legacy bug fixes are Kate's call.** Do not change the behaviour of any file under `src/deluge/gui/menu_item/`, `src/deluge/gui/ui/sound_editor.cpp` or `src/deluge/gui/views/` except where a task names the exact edit (the two entry gates in Task 8, and Task 7a's inventory of legacy's note-editor reads and navigation calls). Porting a legacy predicate into port-side code is not a legacy fix.
- **`./dbt build Debug` is never evidence.** The device link fails on a known pre-existing toolchain skew.
- **Verification gates, run for every task:**
  ```bash
  cmake --build build-sim --target deluge_host deluge_scenes_spec -j8 && ctest --test-dir build-sim
  cmake --build build/tests -j8 && ctest --test-dir build/tests -C Debug
  python3 scripts/check_layer_rules.py && python3 scripts/check_layer_rules.py --audit-symbols
  python3 scripts/check_platform_boundary.py && python3 scripts/check_gfx_layer.py
  python3 scripts/check_citations.py --strict
  python3 scripts/check_citation_shift.py 23aae2ef2..HEAD
  bash scripts/menu_dump_check.sh
  ```
  Never run bare `cmake --build build-sim` with no `--target`. **`check_citation_shift.py` cannot see uncommitted work** — commit, then re-run it.
- **Every citation you write must be opened and read** (`sed -n '<n>p' <file>`), both halves: the line resolves, and it says what the sentence claims. `check_citations.py` does not scan `docs/`, and neither checker sees a bare `` (`:NNN`) `` self-citation.
- **Golden discipline.** Re-record with `GFX_GOLDEN_UPDATE=1`, then run again without the flag. **Read every changed golden** and classify each change in your report as expected by this task, with the reason. A change you cannot explain is a finding, not a re-record. Level-2 goldens are asserted by `sim/scenes_spec/menu_scripts_spec.cpp` and `scene_scripts_spec.cpp`; level-3 by the `ui_app_scripts_*` rows.
- **The level-3 A/B must stay green after every task.** Its rule is byte-identical OLED between the legacy and ported legs. If a task turns it red, the task has changed behaviour the script reaches: stop and report.

## Rulings carried into this plan

1. **Full-page number screens are fixed along with horizontal pages** (spec §3.4). One mapping, one cause; a half fix would invent a mode split.
2. **Carried, not built:** chained horizontal menus and `HorizontalMenuGroup`, SHIFT + CROSS_SCREEN/SCALE at depth 0, automation retargeting on a column focus change, menu-side acceleration, `currentUIMode` gating of select editing (spec §3.3). Task 9 records each.
3. **SHIFT + CROSS_SCREEN/SCALE at depth 0 does nothing.** Legacy switches chained menu there, and the port has no chain data; a plain page switch in its place would be a different gesture. At depth > 0, where legacy has no chain, it page-switches as unshifted.
4. **Both custom-iterance families get their own node definitions** (spec §7's first open question). Sharing would give a node id two parents; separate definitions keep every id single-parented at the cost of 9 more small nodes.
5. **The bridge's real-view spec is a sample** (spec §7's second question): one note-editor read, one offset write, one note-row read that creates a row, and the iterance label. The 25 leaves are covered against a fake bridge.

---

### Task 1: Menu value widgets edit on the select encoder

**Files:**
- Modify: `src/ui/oled/values.cpp` (`int_value` `:81`, `decimal_value` `:126`, `range_value` `:332`, `edit_delta`)
- Modify: `src/ui/oled/values.h` (the `Range` note at `:16-24`, and the three widgets' docs)
- Modify: `src/ui/core/input.h:63-64` (the `turn` / `edit` docs)
- Modify: `src/scenes/menu/menu_scene.cpp` (the `claim_turn` comments in `render_leaf_int`, `render_leaf_decimal`, `render_leaf_range`)
- Modify: `tests/ui-scripts/shell/menu_gold_turn.txt` (header rewrite)
- Test: `tests/spec/ui/values_spec.cpp` (re-pin `:192`, `:218`, `:389`)
- Goldens: `tests/golden/ui/shell/*` (re-record, read)

**Interfaces:**
- Consumes: nothing.
- Produces: `int_value`, `decimal_value`, `range_value` read `f.turn()`, never `f.edit()`.

- [ ] **Step 1: Re-pin the toolkit examples so they fail**

In `tests/spec/ui/values_spec.cpp`, rewrite the three examples to drive `turn` rather than `edit`, and add two. Copy the `InputSnapshot`/frame construction from the examples already there — do not invent a rig.

```cpp
it("applies the select turn only when focused and clamps to the range", _ {
	// Legacy `Integer::selectEncoderAction` (`gui/menu_item/integer.cpp:25`): add, then clamp.
	// <frame with input.turn = +3, focused, value 125, Range{1, 127, 1}>
	// expect value == 127 and resp.changed
	// <same, unfocused>  expect value unchanged
});

it("scales the select turn by 5 while shift is held", _ {
	// `SoundEditor::selectEncoderAction` (`gui/ui/sound_editor.cpp:1072`) multiplies a
	// non-submenu item's offset by 5 while SHIFT is held.
	// <input.turn = +1, input.shift = true, value 10>  expect value == 15
});

it("ignores the gold knobs", _ {
	// Legacy's gold knobs never reach a menu item: `SoundEditor::modEncoderAction` falls through
	// to `View::modEncoderAction` (`gui/views/view.cpp:835`), which edits a param.
	// <input.edit = {+4, +4}, turn = 0, value 10>  expect value == 10, !resp.changed
});

it("moves a decimal by 10^edit_pos per select detent", _ {
	// Legacy `Decimal::selectEncoderAction` (`gui/menu_item/decimal.cpp:57`).
	// <decimal, edit_pos = 1, input.turn = +2, value 100>  expect value == 120
});

it("moves the selected range edge on the select turn", _ {
	// Legacy `IntegerRange::selectEncoderAction` (`gui/menu_item/integer_range.cpp:33`).
	// <range, edge Left, lower 3, upper 9, input.turn = +2>  expect lower == 5
});
```

Delete the old coarse/fine example at `:218` rather than adapting it: the fine gold knob has no legacy counterpart.

- [ ] **Step 2: Run and watch them fail**

```bash
cmake --build build/tests -j8 && ctest --test-dir build/tests -C Debug -R values
```
Expected: the new examples FAIL — the widgets still read `edit`.

- [ ] **Step 3: Change the widgets**

In `values.cpp`, replace `edit_delta` with:

```cpp
/// The select encoder's detents for a value edit, as `SoundEditor::selectEncoderAction`
/// (`gui/ui/sound_editor.cpp:1072`) hands them to a non-submenu item: multiplied by 5 while SHIFT
/// is held. Read through the frame, so an unfocused frame contributes nothing.
int64_t select_delta(const OledFrame& f) {
	const int64_t detents = f.turn();
	return f.shift() ? detents * 5 : detents;
}
```

`int_value`: `f.claim_turn();` in place of the two `claim_edit` calls; `const int64_t delta = select_delta(f) * coarse_step(range);`. Keep the comment at `:86` — it is now true.

`decimal_value`: keep the horizontal-encoder cursor block exactly; the value delta becomes `select_delta(f) * pow10(edit_pos)`, claiming the turn, not `edit`.

`range_value`: keep the horizontal edge block exactly; the value delta becomes `select_delta(f) * coarse_step(range)`, claiming the turn.

Rewrite the docs this makes false: `values.h`'s `Range::step` note (`step` is now "the amount one select detent moves an integer or a range", and there is no fine knob), each widget's encoder description, and `input.h:63-64`: `turn` becomes "Select encoder detents: move a list cursor, or edit the focused value"; `edit[2]` becomes "GoldUpper, GoldLower detents: not read by any menu widget — they reach `services::ModKnobs`".

- [ ] **Step 4: Run the toolkit specs**

```bash
cmake --build build/tests -j8 && ctest --test-dir build/tests -C Debug
```
Expected: PASS.

- [ ] **Step 5: Fix the three leaf comments, re-record, read**

`render_leaf_int`/`render_leaf_decimal`/`render_leaf_range` each end in `f.claim_turn()`; their comments may say the screen "owns the select encoder" — now true. Make sure none still describes the gold knobs as the editor.

```bash
cmake --build build-sim --target deluge_host deluge_scenes_spec -j8
GFX_GOLDEN_UPDATE=1 ctest --test-dir build-sim -R 'menu_scripts_spec|scene_scripts_spec'
ctest --test-dir build-sim
```

Expected classes of change, each to be checked against the pictures: a **gold turn on a number screen no longer moves the value**; a **select turn on a number screen now does**. `menu_gold_turn`'s gold turn now reaches `services::ModKnobs` — read its golden for a param change and, unless the menu shows that param (`ModKnobs::popup_suppressed`, `scenes/services/mod_knobs.cpp:342`), a popup. Rewrite that script's header to say what it now pins and why, citing `View::modEncoderAction` (`gui/views/view.cpp:835`) and the same-param test (`:893-900`). Its old argument — that the menu claims the gold turn so `ModKnobs` never sees it — is the defect being fixed.

**The level-3 A/B must still be green.** Its gold turn (`tests/ui-scripts/app/menu.txt`, `t=1000`) is on a list page, which never claimed it.

- [ ] **Step 6: Whole gate block, then commit**

```bash
git add src/ui/oled/values.cpp src/ui/oled/values.h src/ui/core/input.h src/scenes/menu/menu_scene.cpp \
        tests/spec/ui/values_spec.cpp tests/ui-scripts/shell/menu_gold_turn.txt tests/golden/ui/shell
git commit -m "fix(ui): menu value widgets edit on the select encoder, as legacy's items do"
```

---

### Task 2: Horizontal pages get legacy's controls

**Files:**
- Modify: `src/ui/oled/columns.h`, `src/ui/oled/columns.cpp` (`columns()` `:392`, the turn walk `:401-404`; new helpers)
- Modify: `src/scenes/menu/menu_scene.h` (`MenuState`; the `column_press` comment at `:218-221`; a focused-value accessor)
- Modify: `src/scenes/menu/menu_scene.cpp` (`render_horizontal` `:763`; the notification block `:1437-1467`; `after_render`'s entered arm; `handle`; `leds` `:1113`)
- Modify: `src/scenes/services/mod_knobs.cpp` (`popup_suppressed` `:342`)
- Test: `tests/spec/ui/columns_spec.cpp` (re-pin `:483`, `:575`), `sim/scenes_spec/menu_horizontal_controls_spec.cpp` (create), `sim/scenes_spec/mod_knobs_spec.cpp` (extend)
- Goldens: `tests/golden/ui/shell/*` (re-record, read)

**Interfaces:**
- Consumes: Task 1's select-encoder widgets.
- Produces:
  - `int32_t deluge::ui::oled::column_under_button(std::span<const Column> cols, int32_t focused, int32_t button)` — the column covering instrument button `button` (0-3) on `focused`'s page, or -1.
  - `int32_t deluge::ui::oled::page_switch(std::span<const Column> cols, int32_t focused, int32_t direction)` — the column to focus after a page switch, or `focused` when there is one page.
  - `const menu::Node* deluge::scenes::menu::MenuScene::focused_value() const` — on a horizontal page, the focused column if it is not a submenu; on a leaf, the leaf; otherwise null.

**Read spec §3.1-3.2 first.** Legacy's controls on a horizontal page: select turn edits the focused column (`gui/menu_item/horizontal_menu.cpp:309-326`); SYNTH/KIT/MIDI/CV focus a column on the visible page (`:96-97`, `handleInstrumentButtonPress`); CROSS_SCREEN/SCALE switch page (`:85-94`, `switchVisiblePage` `:345-366`); a select press follows the focused value column (`submenu.cpp:187-194`).

- [ ] **Step 1: Toolkit examples first**

In `columns_spec.cpp`: re-pin `:483` to show a turn **no longer** moves focus; replace `:575`'s page crossing with examples for the two new helpers:

```cpp
it("does not move focus on a turn", _ { /* <turn = +1>  expect state.focused unchanged */ });

it("finds the column under an instrument button on the focused page", _ {
	// `HorizontalMenu::handleInstrumentButtonPress`: walk the visible page's items, accumulating
	// occupied slots, and take the one whose [start, start + slots) covers the button.
	// <page of {1 slot, 2 slots, 1 slot}>  button 0 -> col 0; 1 and 2 -> col 1; 3 -> col 2
});

it("finds nothing under a button past the page's slots", _ { /* 3 one-slot columns, button 3 -> -1 */ });

it("switches page with wrap, keeping the position on the page", _ {
	// `switchVisiblePage` (`horizontal_menu.cpp:345-366`) wraps; `selectMenuItem` keeps
	// `selectedItemPositionOnPage`, falling back to the closest relevant item.
	// <two pages of four, focused = 2>  +1 -> 6; from 6, +1 -> 2; from 1, -1 -> 5
});

it("does not switch when there is one page", _ { /* expect focused unchanged */ });
```

Relevance follows whatever `window_of` already does with `Column::relevant`; read it (`columns.cpp:186`) and match it rather than inventing a second rule.

- [ ] **Step 2: Implement the helpers and remove the turn walk**

In `columns()`, delete `f.claim_turn()` and the `move_focus` call; the scene now owns the turn. Implement `column_under_button` and `page_switch` beside `window_of`, reusing its page computation. Delete `move_focus` if nothing else calls it.

Run `ctest --test-dir build/tests -C Debug -R columns` — PASS.

- [ ] **Step 3: Scene-level failing examples**

Create `sim/scenes_spec/menu_horizontal_controls_spec.cpp`. Copy the rig from `menu_scene_spec.cpp` (a real `MenuScene` opened on a horizontal node; `nodes::lpf` is what `tests/ui-scripts/shell/menu_submenu_horizontal.txt` uses). Examples, each citing its legacy line:

- a select turn edits the focused column and leaves focus alone (`horizontal_menu.cpp:326`);
- a select turn on a focused **submenu** column does nothing (`:315-318`);
- KIT focuses column 1; pressing KIT again runs the column's press — a toggle flips — and raises the notification (`handleItemAction`, `:663-678`);
- two navigation presses inside 140 ms: the second is ignored (`:77-82`);
- CROSS_SCREEN / SCALE page forward and back with wrap;
- SHIFT + CROSS_SCREEN at depth 0 does nothing (ruling 3);
- a select press on a value column whose node has a `select_chain` enters the chain target; with none, the page ascends; **no notification** on a select press;
- button focus onto a `range_dependent` column runs the `MustSelectRange` check (`handleInstrumentButtonPress`'s `initializeItem` arm);
- LEDs: SYNTH/KIT/MIDI/CV lit over the focused column's slots, off when the page is a single four-slot value item (`updateSelectedMenuItemLED`, `:546-572`); SCALE and CROSS_SCREEN lit only with more than one page (`renderOLED`, `:118-119`); all six off on a vertical page.

Run and watch them fail.

- [ ] **Step 4: `render_horizontal` edits on the turn**

Replace `:772-773`'s gold reads:

```cpp
	// `HorizontalMenu::selectEncoderAction` (`gui/menu_item/horizontal_menu.cpp:309-326`): the select
	// turn edits the focused column and never moves focus. A horizontal menu is itself a submenu, so
	// `SoundEditor::selectEncoderAction` hands it the raw offset (`gui/ui/sound_editor.cpp:1072`) --
	// no SHIFT multiplier here -- and a submenu column is left alone (`:315-318`). Menu-side
	// acceleration (`calcNextKnobSpeed`, `:630`) is carried, not ported.
	const int8_t detents = f.turn();
	f.claim_turn();
	if (n != 0 && detents != 0 && kids[focus]->kind != Kind::Submenu) {
		const Node& target = *kids[focus];
		const IntRange range = bindings::range(target, ctx_);
		const int32_t step = range.step != 0 ? range.step : 1;
		const int32_t moved = clamp_to(bindings::read(target, ctx_) + detents * step, range.min, range.max);
		// ... the existing edit-storage switch, unchanged ...
	}
```

- [ ] **Step 5: The select press, the buttons, the LEDs**

**Select press.** Where `render_horizontal` sets `st_.column_press` (`:874`) and `after_render` answers it, make a select press on a non-submenu column do what `SoundEditor`'s SELECT arm does through `Submenu::selectButtonPress`: enter the column's `select_chain` target if it returns one, otherwise `ascend()` — the same two arms `after_render` already uses for a leaf (`menu_scene.cpp`, the `render_resp.entered` block). Remove `column_press` from the notification trigger at `:1437`. Correct the comment at `menu_scene.h:218-221`, which attributes this to `handleItemAction`.

**Buttons**, in `MenuScene::handle`, after the `Custom` arm and before the shortcut pad arm, only when `focused()` is a `Layout::Horizontal` submenu and `event.on`:

```cpp
	// `HorizontalMenu::buttonAction` (`gui/menu_item/horizontal_menu.cpp:70-107`).
	if (event.kind == UiEvent::Kind::Button && event.on && on_horizontal_page()) {
		const int32_t button = instrument_button_index(event.button); // Synth 0, Kit 1, Midi 2, Cv 3, else -1
		const int32_t page_dir = event.button == ButtonId::CrossScreen ? 1 : event.button == ButtonId::Scale ? -1 : 0;
		if (button >= 0 || page_dir != 0) {
			// `:77-82`: one 140 ms debounce shared by all six buttons.
			if (ctx.now_ms - st_.nav_press_ms <= kNavButtonDebounceMs) {
				return Result::handled_nothing(); // spell as this file spells a claimed no-op
			}
			st_.nav_press_ms = ctx.now_ms;
			// ... button: column_under_button; if already focused -> the column's press + notification;
			//     else focus it (MustSelectRange check), request repaint, raise the notification.
			// ... page_dir: at depth 0 with shift held, nothing (ruling 3); else page_switch.
			// return a handled Result with a repaint.
		}
	}
```

Build the `Column` span the same way `render_horizontal` does — factor a helper so both read one list rather than two copies of the relevance and slot rules. Use whatever handled-no-op spelling `handle` already uses; do not invent `handled_nothing` if it does not exist.

**LEDs**, in `MenuScene::leds`: replace the forced-off list for the six buttons with legacy's states on a horizontal page, and off otherwise. `ButtonId::Save` stays off as it is today.

**Popup rule**, in `ModKnobs::popup_suppressed`: compare against `menu_scene().focused_value()` rather than `focused()`. Legacy's same-param test forwards through a horizontal page to the focused value column (`Submenu::shouldForwardButtons`, `gui/menu_item/submenu.cpp:181-185`). Add a `mod_knobs_spec.cpp` example: a gold turn on the param of a focused horizontal column shows no popup.

- [ ] **Step 6: Run, re-record, read**

```bash
cmake --build build-sim --target deluge_host deluge_scenes_spec -j8 && ctest --test-dir build-sim -R 'menu_horizontal|mod_knobs'
GFX_GOLDEN_UPDATE=1 ctest --test-dir build-sim -R 'menu_scripts_spec|scene_scripts_spec'
ctest --test-dir build-sim
```

Expected classes: on horizontal pages a select turn now edits instead of moving focus; a select press on a value column now ascends instead of notifying; the six LEDs change. **Level-3 A/B must stay green.**

- [ ] **Step 7: Whole gate block, then commit**

```bash
git add src/ui/oled/columns.h src/ui/oled/columns.cpp src/scenes/menu/menu_scene.h src/scenes/menu/menu_scene.cpp \
        src/scenes/services/mod_knobs.cpp tests/spec/ui/columns_spec.cpp \
        sim/scenes_spec/menu_horizontal_controls_spec.cpp sim/scenes_spec/mod_knobs_spec.cpp tests/golden/ui/shell
git commit -m "fix(menu): horizontal pages edit on select and focus by instrument button, as legacy's do"
```

---

### Task 3: Three optional `Binding` hooks — `adjust`, `value_text`, `permitted`

**Files:**
- Modify: `src/scenes/menu/node.h` (`Binding` `:216`; new function types; builder helpers)
- Modify: `src/scenes/menu/bindings.h`, `bindings.cpp`
- Modify: `src/scenes/menu/menu_scene.h` (`MenuState`), `menu_scene.cpp` (`render_horizontal`, the notification block, `commit_value` `:1208`)
- Modify: `src/ui/oled/columns.h`, `columns.cpp` (`TextColumn::strikethrough`)
- Modify: `src/scenes/menu/validate.h`, `validate.cpp`
- Test: `sim/scenes_spec/menu_binding_hooks_spec.cpp` (create), `tests/spec/scenes/validate_spec.cpp` (extend), `tests/spec/ui/columns_spec.cpp` (extend)

**Interfaces:**
- Consumes: Task 2's `render_horizontal` edit block and `focused_value()`.
- Produces (in `deluge::scenes::menu`):
  ```cpp
  using AdjustFn = void (*)(const MenuContext& ctx, int32_t offset);
  enum class ValueTextUse : uint8_t { Column, Notification };
  struct ValueText { size_t length = 0; bool strikethrough = false; };
  using ValueTextFn = ValueText (*)(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
  using PermittedFn = bool (*)(const MenuContext& ctx);
  // Binding gains: AdjustFn adjust = nullptr; ValueTextFn value_text = nullptr; PermittedFn permitted = nullptr;
  constexpr Node with_adjust(Node n, AdjustFn fn);
  constexpr Node with_value_text(Node n, ValueTextFn fn);
  constexpr Node with_permitted(Node n, PermittedFn fn);
  void bindings::adjust(const Node& node, const MenuContext& ctx, int32_t offset);
  ```
  and `deluge::ui::oled::TextColumn::strikethrough` (bool, default false).

**Why each exists** (spec §5.2-5.3): eight note leaves write by offset — a value `set` would flatten several notes to one velocity; their columns and notifications show legacy's strings, not a number; and their guards refuse an edit with a popup. `Kind::Custom` cannot carry them: 55 custom nodes against `kMaxCustomNodes = 56` (`scenes/menu/validate.h:156`).

- [ ] **Step 1: Failing examples**

In `menu_binding_hooks_spec.cpp`, on a small test tree built in the file (the pattern `tests/spec/scenes/validate_spec.cpp` uses for `kBadRoots`), opened in a real `MenuScene` as a horizontal page:

- a node with `adjust`: a select turn of +3 calls `adjust` **once**, with +3, at commit — and `set` is never called;
- a node with `value_text`: the column draws its `Column` text, and the post-edit notification line is `name: ` + its `Notification` text;
- `strikethrough = true` reaches the column (`TextColumn` example in `columns_spec.cpp` checks the pixels against `note/fill.h`'s strike: rows `center_y ± 1` cleared, a line at `center_y`, from `start_x + 2` to `start_x + width - 4`);
- a node with `permitted` returning false: the turn changes nothing and `adjust`/`set` are not called.

In `validate_spec.cpp`: a value node with `adjust` and no `get` reports a new `Issue::Kind::AdjustWithoutGet`.

- [ ] **Step 2: Run, watch fail. Step 3: Implement**

- `Binding` fields and builders in `node.h`, each with a docblock citing the spec section and the legacy it serves.
- `MenuState::edit_offset` (`int32_t`). In `render_horizontal`'s edit block: if `target.bind.permitted` exists and returns false, stop; if `target.bind.adjust` exists, accumulate `detents` into `st_.edit_offset` and mark `value_changed` / `value_node` without computing `moved`.
- `commit_value`: for a node with `adjust`, call `bindings::adjust(node, ctx_, st_.edit_offset)` and clear it; otherwise unchanged.
- The column: when `kid.bind.value_text` is set, build a `TextColumn` from its `Column` text and `strikethrough` instead of the kind's default renderer.
- The notification block: when the column's `value_text` is set, use its `Notification` text instead of `format_int`.
- `text_column` draws the strike when `strikethrough` is set, transcribing `note/fill.h`'s pixel loop.
- `validate.cpp`: the `AdjustWithoutGet` rule beside the existing `get`/`param` rule (`:292`).

- [ ] **Step 4: Run, whole gate block, commit**

No golden should change: no shipped node sets a hook yet.

```bash
git add src/scenes/menu/node.h src/scenes/menu/bindings.h src/scenes/menu/bindings.cpp \
        src/scenes/menu/menu_scene.h src/scenes/menu/menu_scene.cpp src/ui/oled/columns.h src/ui/oled/columns.cpp \
        src/scenes/menu/validate.h src/scenes/menu/validate.cpp \
        sim/scenes_spec/menu_binding_hooks_spec.cpp tests/spec/scenes/validate_spec.cpp tests/spec/ui/columns_spec.cpp
git commit -m "feat(menu): Binding::adjust, value_text and permitted -- offset writes, legacy's strings, legacy's guards"
```

---

### Task 4: The note bridge

**Files:**
- Create: `src/scenes/menu/note_bridge.h`, `src/scenes/menu/note_bridge.cpp`
- Create: `src/deluge/gui/ui/shell_note_bridge.h`, `src/deluge/gui/ui/shell_note_bridge.cpp`
- Modify: `src/deluge/deluge.cpp` (beside `wireShellGuestServices()` at `:804`)
- Modify: `scripts/check_layer_rules.py` (`LEGACY_PORT_SEAMS`)
- Create: `sim/scenes_spec/support/fake_note_bridge.h`
- Test: `sim/scenes_spec/note_bridge_spec.cpp` (create)

**Interfaces:**
- Consumes: nothing.
- Produces:
  ```cpp
  namespace deluge::scenes::menu {
  struct NoteBridge {
  	// Note editor -- `gui/menu_item/note/*.h`.
  	bool (*note_selected)();                         // `SelectedNote::shouldEnterSubmenu`'s test, no popup
  	int32_t (*note_velocity)();                      // `gridSquareInfo[y][x].averageVelocity`
  	void (*adjust_note_velocity)(int32_t offset);    // `adjustVelocity`
  	int32_t (*note_probability)();                   // leftmost pressed note's `getProbability()`
  	void (*adjust_note_probability)(int32_t offset); // `adjustNoteProbabilityWithOffset`
  	int32_t (*note_iterance)();                      // leftmost pressed note's `getIterance().toInt()`
  	void (*adjust_note_iterance)(int32_t offset);    // `adjustNoteIteranceWithOffset`
  	void (*set_note_iterance)(int32_t iterance);     // `adjustNoteIteranceWithFinalValue(Iterance::fromInt(...))`
  	int32_t (*note_fill)();                          // leftmost pressed note's `getFill()`
  	void (*adjust_note_fill)(int32_t offset);        // `adjustNoteFillWithOffset`
  	// Note-row editor -- `gui/menu_item/note_row/*.h`. Reads create the row on a non-kit clip.
  	bool (*row_auditioning)();                       // `isUIModeActive(UI_MODE_AUDITIONING)`
  	int32_t (*row_probability)();
  	void (*adjust_row_probability)(int32_t offset);  // `setNoteRowProbabilityWithOffset`
  	int32_t (*row_iterance)();
  	void (*adjust_row_iterance)(int32_t offset);     // `setNoteRowIteranceWithOffset`
  	void (*set_row_iterance)(int32_t iterance);      // `setNoteRowIteranceWithFinalValue`
  	int32_t (*row_fill)();
  	void (*adjust_row_fill)(int32_t offset);         // `setNoteRowFillWithOffset`, plus its fill-mode repaint
  	bool (*row_sequence_direction)(int32_t* out);    // false when there is no row
  	void (*set_row_sequence_direction)(int32_t value);
  	// Shared.
  	size_t (*iterance_preset_label)(int32_t preset, bool compact, std::span<char> out); // `iterancePresets`
  };
  void set_note_bridge(const NoteBridge* bridge);   // nullptr clears
  const NoteBridge* note_bridge();                  // nullptr until installed
  }
  namespace deluge::gui::ui { void wireShellNoteBridge(); }
  ```

**Why a bridge, and why synchronous:** spec §5.4. Every entry transcribes the legacy statement named in its comment; a read that returns nothing in legacy (no pressed note) returns 0 here, and the guard in Task 5 keeps an edit from reaching it.

- [ ] **Step 1: The declaration and a failing spec**

`note_bridge.h`/`.cpp`: the struct, `set_note_bridge`, `note_bridge`. No legacy includes. Iterance crosses as `int32_t` via `Iterance::toInt`/`fromInt` (`model/iterance/iterance.h`), which `src/scenes` may include.

`fake_note_bridge.h`: a header-only recording fake — every entry records its call and arguments into a `FakeNoteBridge` struct and returns a settable value — plus an RAII guard that installs it and clears it in its destructor (the shape of `sim/scenes_spec/bindings_spec.cpp`'s `FeedbackHookGuard`, `:113-122`).

`note_bridge_spec.cpp`, against the **real** installation, a stated sample (ruling 5): build an instrument clip with one note and select it the way `InstrumentClipView` does; read `note_velocity`; `adjust_note_velocity(+1)` and read again; on a non-kit clip with no row at an auditioned y, `row_probability` **creates** the row; `iterance_preset_label` for preset 1 matches legacy's `"%d:%d"`. **Read `sim/scenes_spec/support/song_fixture.h` first; if selecting a note cannot be done without booting the app, pin the label and the row creation and report the rest as uncovered.**

- [ ] **Step 2: The installation**

`shell_note_bridge.cpp`: one static function per entry, each a transcription of its legacy statement with a citation, and `wireShellNoteBridge()` installing a static `NoteBridge`. The note-row reads use `getIndividualNoteRow`'s body (`gui/menu_item/note_row/selected_note_row.h:45-61`). `adjust_row_fill` includes the `currentSong->isFillModeActive()` repaint (`note_row/fill.h`). `iterance_preset_label` transcribes `getIteranceDisplayValue` (`gui/menu_item/note/iterance_preset.h`) with `compact` choosing `"%d:%d"` over `"%d of %d"`, and answers only presets 1..`kNumIterancePresets`.

Call `wireShellNoteBridge()` beside `deluge.cpp:804`. Add `LEGACY_PORT_SEAMS` entry for `shell_note_bridge.cpp` with an argued comment in the register of its neighbours; its header names no `scenes/` type, so it is not listed.

- [ ] **Step 3: Run, whole gate block, commit**

```bash
git add src/scenes/menu/note_bridge.h src/scenes/menu/note_bridge.cpp \
        src/deluge/gui/ui/shell_note_bridge.h src/deluge/gui/ui/shell_note_bridge.cpp src/deluge/deluge.cpp \
        scripts/check_layer_rules.py sim/scenes_spec/support/fake_note_bridge.h sim/scenes_spec/note_bridge_spec.cpp
git commit -m "feat(menu): the note bridge -- instrumentClipView's note state, reached without an include"
```

---

### Task 5: The 29 note nodes

**Files:**
- Create: `src/scenes/menu/nodes/note.h`, `src/scenes/menu/nodes/note.cpp`
- Modify: `src/scenes/menu/nodes/nodes.h` (`kAllRootArray`, `:144-147`)
- Modify: `website/src/data/generated/menu-hierarchies.json` (regenerated, reviewed)
- Test: `sim/scenes_spec/note_nodes_spec.cpp` (create)

**Interfaces:**
- Consumes: Task 3's hooks; Task 4's `NoteBridge` and `fake_note_bridge.h`.
- Produces: `nodes::note_editor_root`, `nodes::note_row_editor_root`, `nodes::note_custom_iterance_root`, `nodes::note_row_custom_iterance_root`, and the 25 leaves named below.

**The shape** (spec §5.5), each row citing its legacy object in `gui/ui/menus.cpp`:

| node | legacy | binding |
|---|---|---|
| `note_editor_root` | `noteEditorRootMenu` `:1880` | `submenu(STRING_FOR_NOTE_EDITOR, …, Layout::Horizontal)` |
| `note_velocity` | `noteVelocityMenu` | `int_item` {1,127,1}, `Hint::None`; `get` = `note_velocity`; `adjust`; `permitted` = note |
| `note_probability` | `noteProbabilityMenu` | {1, `kNumProbabilityValues \| 127`}; `adjust`; `value_text`; `permitted` = note |
| `note_iterance` | `noteIteranceMenu` | {0, `kNumIterancePresets + 1`}; `get` = preset index; `adjust`; `value_text`; `select_chain` → custom root on `kCustomIterancePreset`; `permitted` = note |
| `note_fill` | `noteFillMenu` | {`FillMode::OFF`, `FillMode::FILL`}; `adjust`; `value_text` (column strike on `NOT_FILL`); `permitted` = note |
| `note_custom_iterance_root` | `noteCustomIteranceRootMenu` `:1859` | vertical `submenu(STRING_FOR_CUSTOM, …)` |
| `note_iterance_divisor` | `noteCustomIteranceDivisor` | `int_item` {1,8}; value `set` (mask, `set_note_iterance`); `permitted` = note |
| `note_iterance_step_1..8` | `noteCustomIteranceStep1..8` | `toggle_item`; value `set`; `relevant` = divisor > index, or index 0 with iterance off |
| `note_row_editor_root` | `noteRowEditorRootMenu` `:1927` | horizontal: `&sequence_direction`, then the three below |
| `note_row_probability` / `_iterance` / `_fill` | `noteRow*Menu` | as the note versions, through the row bridge; `permitted` = row |
| `note_row_custom_iterance_root` + divisor + 8 steps | `:1907` | as the note versions, through the row bridge |

Guards: `permitted` = note calls `note_selected()` and, when false, raises `deluge::notify::notice_text("Select Note")`; row calls `row_auditioning()` and raises `"Select Row"`. Both literals are legacy's (`note/selected_note.h:37`, `note_row/selected_note_row.h:36`). **Step toggles have no guard** — `IteranceStepToggle` derives from `Toggle`, not `SelectedNote`.

`value_text` strings, transcribed: probability — `p > kNumProbabilityValues` means latching, `p &= 127`, shown ×5; column `"%d%"`/`"%dL"`, notification `"%d%"` + `" ltch"` (`note/probability.h`). Iterance — 0 `"OFF"`, `kCustomIterancePreset` `"CUSTOM"`, otherwise `iterance_preset_label` (compact for the column). Fill — column `"OFF"` for OFF and `"FILL"` otherwise, strike on `NOT_FILL`; notification `"OFF"`/`"NOT FILL"`/`"FILL"` (`InstrumentClipView::getFillString`, `gui/views/instrument_clip_view.cpp`). Divisor and step behaviour: transcribe `note/iterance_divisor.h` and `note/iterance_step_toggle.h`, including the "OFF becomes 1of1" conversion.

**Append both editor roots to the end of `kAllRootArray`** — after `multi_range` — so no existing node id changes (`node_id.h:14-18`). The custom roots are reached through `chain_targets`.

- [ ] **Step 1: Failing spec against the fake bridge**

In `note_nodes_spec.cpp`, with `fake_note_bridge.h` installed: for every leaf, `bindings::read` calls its bridge entry; every `adjust` passes its offset through unchanged; every value `set` computes legacy's mask; each guard refuses and raises its literal; each step toggle's relevance follows the divisor; iterance's chain resolves only on `kCustomIterancePreset`; the three `value_text`s produce the strings above, including latching and the strike. And the shape, against the live legacy objects rather than a copied list (the way `tables_3_spec.cpp` does it for `settingsRootMenu`): each root and custom root has legacy's child count. `validate_tree` over `kAllRoots` reports nothing.

- [ ] **Step 2: Implement `note.h`/`note.cpp`. Step 3: Run**

- [ ] **Step 4: Regenerate the dump and read it**

```bash
bash scripts/menu_dump_check.sh || true
git diff --stat website/src/data/generated/menu-hierarchies.json
```
Expected: two new root entries appended, and **no existing node id changed**. Confirm the second by diffing only the pre-existing part. Then run `bash scripts/menu_dump_check.sh` again — it must pass.

- [ ] **Step 5: Whole gate block, then commit**

```bash
git add src/scenes/menu/nodes/note.h src/scenes/menu/nodes/note.cpp src/scenes/menu/nodes/nodes.h \
        website/src/data/generated/menu-hierarchies.json sim/scenes_spec/note_nodes_spec.cpp
git commit -m "feat(menu): the note and note-row editor trees, 29 nodes through the bridge"
```

---

### Task 6: The two roots, and everything indexed by `MenuContext::Root`

**Files:**
- Modify: `src/scenes/menu/context.h` (`Root` `:68-80` and its doc note)
- Modify: `src/scenes/menu/menu_scene.h`, `menu_scene.cpp` (`roots()` `kRoots`; `grid_pad_claim`; `greyout_mask`)
- Modify: `src/scenes/menu/shortcuts.cpp` (`kByRootV3` `:336`, `kByRootV1` `:353`)
- Modify: `src/scenes/menu/bindings.h`, `bindings.cpp` (`read_sequence_direction` / `write_sequence_direction` and its names)
- Modify: `src/deluge/util/shell_menu_entry.h` (`:178`), `src/deluge/gui/ui/shell_menu_entry.cpp` (`openShellMenu`)
- Test: `sim/scenes_spec/menu_grid_claim_spec.cpp` (extend), `sim/scenes_spec/note_roots_spec.cpp` (create)

**Interfaces:**
- Consumes: Task 5's two editor roots; Task 4's bridge.
- Produces:
  - `MenuContext::Root::NoteEditor`, `MenuContext::Root::NoteRowEditor`, appended after `Settings`.
  - `enum class deluge::gui::ui::NoteMode : uint8_t { None, NoteEditor, NoteRowEditor };`
  - `bool openShellMenu(Clip*, int32_t, bool, bool, bool, bool, ShellMenuNode node = ShellMenuNode::ContextRoot, NoteMode noteMode = NoteMode::None);`

- [ ] **Step 1: Failing examples**

In `note_roots_spec.cpp`:
- `MenuScene::roots()[NoteEditor] == &nodes::note_editor_root`, likewise the row root;
- both roots map to `kNoCells` in both shortcut versions, and a menu opened on either lights no pad (spec §4's site 5);
- `greyout_mask()` is `{0x03, 0}` on the note editor and empty on the note-row editor (`gui/ui/sound_editor.cpp:274-281`);
- `openShellMenu(clip, 0, false, false, false, false, ContextRoot, NoteMode::NoteEditor)` opens with `menu_context().root == NoteEditor`;
- on a kit with `affectEntire` true, that call leaves `affectEntire` false **and** resolves the context before the toggle (`instrument_clip_view.cpp:3590-3598`);
- under `NoteRowEditor` on a non-kit clip, sequence direction reads and writes the auditioned row through the bridge and offers `NONE` when a row exists (`gui/menu_item/sequence/direction.h:50-60`, `:103-105`); under any other root it is unchanged.

In `menu_grid_claim_spec.cpp`: both note roots answer `GridPadClaim::None` — legacy skips the shortcut arm for both (`sound_editor.cpp:1467`).

- [ ] **Step 2: Implement**

- The enumerators, and a rewritten doc note on `Root` in place of the one saying these roots "stay with the caller".
- `kRoots` gains both nodes, in enumerator order.
- `kByRootV3` and `kByRootV1` each gain `{&kNoCells, &kNoCells}` twice. The arrays are aggregate-initialised to `Root::Count`, so a missing entry would compile as a null `Cells*`.
- `MenuScene::grid_pad_claim()`: `None` for `Settings`, `NoteEditor`, `NoteRowEditor`.
- `greyout_mask()`: the two note arms, ahead of the `INSTRUMENT_CLIP` arm; update the file's own note that these arms were "left with the scene that owns those views".
- Sequence direction: when `ctx.root == Root::NoteRowEditor` and the output is not a kit, go through `row_sequence_direction` / `set_row_sequence_direction`; correct `bindings.h`'s note calling that arm unported. The equivalence argument goes in the comment: every gated site opens with `UI_MODE_NONE`, so `selectedNoteRow` is only ever latched true on this root.
- `openShellMenu`: the `NoteMode` parameter, defaulted. After `context_for` and the `resolved` test, when `noteMode != None`: set `ctx.root`; then, on a kit clip with `affectEntire`, clear it and call `view.setActiveModControllableTimelineCounter(clip)` — **before** `menu_scene().open(...)` and `openUI`, which is legacy's order.

- [ ] **Step 3: Run — `shortcuts_spec`, `tables_3_spec` and `validate_spec` enumerate roots and must pass unchanged. Whole gate block, then commit**

```bash
git add src/scenes/menu/context.h src/scenes/menu/menu_scene.h src/scenes/menu/menu_scene.cpp \
        src/scenes/menu/shortcuts.cpp src/scenes/menu/bindings.h src/scenes/menu/bindings.cpp \
        src/deluge/util/shell_menu_entry.h src/deluge/gui/ui/shell_menu_entry.cpp \
        sim/scenes_spec/menu_grid_claim_spec.cpp sim/scenes_spec/note_roots_spec.cpp
git commit -m "feat(menu): MenuContext::Root::NoteEditor and NoteRowEditor, and openShellMenu's NoteMode"
```

---

### Task 7: The mode's routing through the guest

**Files:**
- Create: `src/deluge/gui/ui/shell_note_mode.h`, `src/deluge/gui/ui/shell_note_mode.cpp`
- Modify: `src/deluge/gui/ui/shell_host_ui.cpp` (`buttonAction`, `horizontalEncoderAction`, `verticalEncoderAction`, `closeGuest`)
- Modify: `src/deluge/gui/ui/shell_pad_forward.h`, `shell_pad_forward.cpp`
- Modify: `tests/spec/gui/support/fake_legacy_ui.h`, `fake_legacy_ui.cpp`
- Modify: `scripts/check_layer_rules.py` (`LEGACY_PORT_SEAMS`)
- Test: `sim/scenes_spec/note_mode_spec.cpp` (create), `sim/scenes_spec/pad_forward_spec.cpp` (extend), `tests/spec/gui/shell_host_ui_spec.cpp` (extend)

**Interfaces:**
- Consumes: Task 6's roots.
- Produces:
  ```cpp
  namespace deluge::gui::ui {
  enum class NoteModeKind : uint8_t { None, NoteEditor, NoteRowEditor };
  NoteModeKind activeNoteMode();
  std::optional<ActionResult> forwardNoteButton(deluge::hid::Button b, bool on, bool inCardRoutine);
  std::optional<ActionResult> forwardNoteHorizontalEncoder(int32_t offset);
  std::optional<ActionResult> forwardNoteVerticalEncoder(int32_t offset, bool inCardRoutine);
  void exitNoteModeBeforeClose();
  }
  ```
  and two `PadForwardTarget` values, `NoteEditorEdit` and `NoteRowEditor`.

**`std::nullopt` means "not the note mode's": feed the shell exactly as today.** A value means the guest forwarded, and that result is what its hook returns.

**Read spec §4 first.** Its table and its paragraphs on which buttons reach site 2 are this task's requirements.

- [ ] **Step 1: Failing examples**

`note_mode_spec.cpp`, with a recording seam around the legacy calls (the shape of `ui/pads-1`'s `setPadForwardPerform`):

- `activeNoteMode()` answers the menu root, and `None` once the ported menu is off the guest's stack;
- SELECT, BACK, SAVE, LEARN, KEYBOARD, SESSION_VIEW → `nullopt` (legacy handles each before the note arm, `sound_editor.cpp:385`, `:456`, `:488`, `:505`, `:564`, `:605`);
- SYNTH/KIT/MIDI/CV → `nullopt` — they are Task 2's column focus, and legacy's route to them reads `soundEditor.getCurrentMenuItem()`, which is null under the guest;
- PLAY, SHIFT, MOD_ENCODER_0/1 → `nullopt` — legacy returns `NOT_DEALT_WITH`, which is what the guest already returns;
- X_ENC and SYNC_SCALING → forwarded to `instrumentClipView.buttonAction`, its result returned;
- AFFECT_ENTIRE in the note editor → `DEALT_WITH`, no effect;
- **AFFECT_ENTIRE in the note-row editor on a kit → `clip->affectEntire` toggles** and `DEALT_WITH` (`instrument_clip_view.cpp`, `handleNoteRowEditorButtonAction`). Prove the effect, not only the result;
- any other button in either mode → `DEALT_WITH`, not fed;
- both encoders forward to their `handleNote*` handlers and return their result — including `REMIND_ME_OUTSIDE_CARD_ROUTINE`;
- `exitNoteModeBeforeClose` calls `exitNoteEditor()` / `exitNoteRowEditor()` per root, and nothing on any other root.

`pad_forward_spec.cpp`: under `NoteEditor` over `instrumentClipView`, a main-grid pad → `NoteEditorEdit`, a sidebar pad → `Exit`; under `NoteRowEditor`, every pad → `NoteRowEditor`, and the real performer closes the menu when the handler returns false (`sound_editor.cpp:1481-1498`). Both arms come **before** the audition-column arm, as legacy's do.

`shell_host_ui_spec.cpp`, against fakes for the four new functions: each hook consults its function and uses the result when there is one, feeds the shell when there is not, and arms the tick only while the guest is current; `closeGuest` calls `exitNoteModeBeforeClose` before `close()`.

- [ ] **Step 2: Implement**

`shell_note_mode.cpp` holds the classification above as one function per hook, each arm citing its legacy line. `activeNoteMode` reads `deluge::scenes::menu_scene().menu_context().root` and requires the ported menu to be on the guest's stack — use the lookup `menuIsFrontmost` already uses (`gui/ui/shell_guest_services.cpp`). Add its `LEGACY_PORT_SEAMS` entry, argued.

`shell_host_ui.cpp`: in `buttonAction`, after the MOD_ENCODER early return, `if (auto r = forwardNoteButton(b, on, inCardRoutine)) { if (getCurrentUI() == this) armTickSoon(); return *r; }`; the two encoder hooks the same way; `closeGuest()` calls `exitNoteModeBeforeClose()` immediately before `close()`, below the existing indicator clear. Keep every existing comment. The re-entrancy rule is spec §4's last paragraph.

`shell_pad_forward.cpp`: the two targets and their arms; `performForward` for `NoteRowEditor` calls `closePortedMenu()` when the handler returns false.

Fakes in `fake_legacy_ui.{h,cpp}` for the four new functions, each recording and returning a settable value, reset in `reset()`.

- [ ] **Step 3: Run, whole gate block, commit**

```bash
git add src/deluge/gui/ui/shell_note_mode.h src/deluge/gui/ui/shell_note_mode.cpp \
        src/deluge/gui/ui/shell_host_ui.cpp src/deluge/gui/ui/shell_pad_forward.h src/deluge/gui/ui/shell_pad_forward.cpp \
        tests/spec/gui/support/fake_legacy_ui.h tests/spec/gui/support/fake_legacy_ui.cpp scripts/check_layer_rules.py \
        sim/scenes_spec/note_mode_spec.cpp sim/scenes_spec/pad_forward_spec.cpp tests/spec/gui/shell_host_ui_spec.cpp
git commit -m "feat(guest): route the note modes' buttons, encoders, pads and exit as SoundEditor does"
```

---

### Task 7a: The note mode at legacy's own reads and navigation calls

*Inserted by controller ruling during execution (ledger, "spec gap"), after Task 7 and before Task 8.
Task 7 provides `activeNoteMode()`; Task 8 opens the ported note editors on device, which is the
first commit where any of the paths below is reachable. This task must land before Task 8.*

**Why.** Legacy decides "a note editor is open" with
`getCurrentUI() == &soundEditor && soundEditor.inNoteEditor()` (or `inNoteRowEditor()`), where
`inNoteEditor()` reads `menuItemNavigationRecord[0]` (`gui/ui/sound_editor.cpp:1916-1922`). Under the
guest, the current UI is `ShellHostUI` and the navigation record is never written, so **both halves
are false** everywhere, and `soundEditor.getCurrentMenuItem()` (`sound_editor.cpp:1908-1910`,
`menuItemNavigationRecord[navigationDepth]`) is **null**. Spec §4 counts only SoundEditor's own eight
sites. The legacy functions the note mode forwards into (Task 7) and writes through (Task 4's bridge)
contain many more such reads, and several dereference the navigation record directly. Measured by
Task 4's review: with no popup, `adjustNoteParameterValue` swallows the first turn and raises a legacy
PROBABILITY popup (`gui/views/instrument_clip_view.cpp:3066`, `:3117`); `adjustVelocity`
(`:2852-2858`) and `setNoteRowParameterValue` (`:4063-4066`) are gated the same way.

**Files:**
- Modify: `src/deluge/gui/ui/shell_note_mode.h`, `src/deluge/gui/ui/shell_note_mode.cpp`
- Modify (named, predicate-only or gated-pair edits — this brief is the naming): `src/deluge/gui/views/instrument_clip_view.cpp`, `src/deluge/gui/views/view.cpp`, `src/deluge/gui/views/timeline_view.cpp`, `src/deluge/model/action/action_logger.cpp`, `src/deluge/model/note/note_row.cpp`, `src/deluge/gui/ui/notify_drain.cpp`
- Modify: `src/scenes/menu/menu_scene.h`, `src/scenes/menu/menu_scene.cpp` (public navigation entry points, only if no existing public one fits)
- Modify: `scripts/check_layer_rules.py` (only if a seam entry changes)
- Test: `sim/scenes_spec/note_mode_reads_spec.cpp` (create); extend `sim/scenes_spec/note_bridge_spec.cpp` (the note-mode arm of the offset gate)
- NOT modified: `src/deluge/gui/menu_item/**`, `src/deluge/gui/ui/sound_editor.cpp`.

**Interfaces:**
- Consumes (Task 7): `deluge::gui::ui::NoteModeKind activeNoteMode();` — `None` unless the ported menu is on the guest's stack with a note root. Consumes (Task 4): `NoteBridge` entries and `note_bridge_spec.cpp`'s real-view setup (spec-local `currentPlaybackMode`/`display` seeds, `editPadAction` note selection).
- Produces, in `deluge::gui::ui` (camelCase, `src/deluge`):
  ```cpp
  bool noteEditorOpen();    // (getCurrentUI() == &soundEditor && soundEditor.inNoteEditor())    || activeNoteMode() == NoteModeKind::NoteEditor
  bool noteRowEditorOpen(); // (getCurrentUI() == &soundEditor && soundEditor.inNoteRowEditor()) || activeNoteMode() == NoteModeKind::NoteRowEditor
  ```
  With the ported menu closed, each is exactly legacy's expression — **the legacy leg must not change**,
  and the level-3 A/B (`ui_app_scripts_menu_ab`) must stay green.

**The inventory — re-derive it first, and report any difference from this list:**
```bash
grep -rnE "soundEditor\.(inNoteEditor|inNoteRowEditor)\(\)|soundEditor\.selectedNoteRow|soundEditor\.getCurrentMenuItem\(\)|soundEditor\.goUpOneLevel\(\)|soundEditor\.toggleNoteEditorParamMenu" src/deluge | grep -v "^src/deluge/gui/ui/shell_"
```
A. **Predicate reads** (controller's grep at 8933bd9e1, 21 lines in 7 files):
`instrument_clip_view.cpp` :2379 (editPadAction), :2852+:2930 (adjustVelocity), :2982 (displayVelocity),
:3066 and :3249 (adjustNoteParameterValue), :4063 (setNoteRowParameterValue), :4202 (mutePadPress),
:4347 (scrollVertical), :5040 (setSelectedDrum), :5479 (potentiallyRefreshNoteRowMenu — `selectedNoteRow`),
:6072 (drawAuditionSquare), :6190 (verticalEncoderAction), :6827 (nudgeNotes); `view.cpp:340`;
`timeline_view.cpp:158`; `model/action/action_logger.cpp:99`; `model/note/note_row.cpp:1997`;
`gui/ui/notify_drain.cpp:48`; `gui/menu_item/sequence/direction.h` :35, :51, :118.
B. **Navigation-record dereferences on paths the note mode reaches** (each null under the guest):
`instrument_clip_view.cpp` :3655 `getCurrentMenuItem()->readValueAgain()` (handleNoteEditorEditPadAction);
:3690-3691 `getCurrentMenuItem() != &noteEditorRootMenu` → `goUpOneLevel()` (deselectNoteAndGoUpOneLevel,
called by both note-editor encoder handlers :3695, :3705); :3836 `toggleNoteEditorParamMenu(on)` and
:3842 `readValueAgain()` (handleNoteRowEditorMainPadAction); :3878 `readValueAgain()`
(handleNoteRowEditorAuditionPadAction); :3905-3907 (handleNoteRowEditorVerticalEncoderAction); and
whatever the bodies behind A's :3249, :5479-5480 and :6955-6963 (nudgeNotes' `goUpOneLevel`) do.
:3716 and :3956 (SYNTH/KIT/MIDI/CV) are unreachable — Task 7 returns `nullopt` for those buttons; say so.
`enterNoteEditor`/`enterNoteRowEditor` (:3590-3598, :3758-3766) are Task 8's gates — not yours.

**Classify every row** in a table in your report: *widen* (replace only the predicate expression with
`noteEditorOpen()`/`noteRowEditorOpen()`), *gated pair* (the site then touches soundEditor navigation:
`if (activeNoteMode() != NoteModeKind::None) { <port equivalent> } else { <legacy statements unchanged> }`,
spelled like site 6, `model/clip/instrument_clip_minder.cpp:399-424`), or *leave* (with the reason — e.g.
`direction.h` is a legacy menu item the ported `sequence_direction` node replaces (its names,
`sequence_direction_names`, `scenes/menu/nodes/sound_actions.cpp:38`); prove it unreachable under the guest). Every row cites its line,
and the port equivalent cites the legacy statement it ports. A *widen* whose body then dereferences the
navigation record is a crash — it must be a gated pair.

**Port equivalents** (confirm each against legacy before using it):
- `getCurrentMenuItem()->readValueAgain()` → `deluge::scenes::menu_scene().read_value_again()` (public,
  `scenes/menu/menu_scene.h:569`) — reached through a `gui/ui` seam, not an include from `gui/views`.
- `goUpOneLevel()` → one level up in the ported menu; at depth 0 legacy exits completely
  (`sound_editor.cpp:710-718`) → `closePortedMenu()` (`gui/ui/shell_action_dispatch.h:153`).
  `MenuScene::ascend()` is private (`menu_scene.h:906`); add the smallest public entry that ports
  `goUpOneLevel`, documented against `:710-728`.
- `getCurrentMenuItem() != &noteEditorRootMenu` → "the ported menu is not at its root level".
- `toggleNoteEditorParamMenu(on)` (`sound_editor.cpp:1925-1975`) → its port: on press, row
  probability/iterance/fill → the note versions, and the row root → the note root when horizontal menus
  are on; on release the reverse; only when the target's `shouldEnterSubmenu()` passes (Task 5's
  `permitted` guard, which raises "Select Note"); enter = descend, release = `goUpOneLevel`. Legacy's
  `inNoteRowEditor()` stays true throughout the hold (`record[0]` is unchanged), so **`activeNoteMode()`
  must keep answering `NoteRowEditor` while the note menu is entered from the row editor** — pin it. Task 7 keys `activeNoteMode()` on `menu_scene().menu_context().root`, and `ctx_` is replaced only by `MenuScene::open` (`scenes/menu/menu_scene.cpp:332-335`) and a song swap (`:572-576`), so the port must move between the row and note menus **without calling `MenuScene::open()`** — that would reassign `ctx_.root` and flip the mode to `NoteEditor` mid-hold.
  Read `runtimeFeatureSettings` `HorizontalMenus` exactly as legacy does.

No new UI/UX: every port equivalent is a legacy statement's port, cited. If a row has no faithful port
equivalent, do not invent one — classify it *carried*, state the divergence, and report it as
DONE_WITH_CONCERNS.

- [ ] **Step 1: Failing examples** (`note_mode_reads_spec.cpp`, real `instrumentClipView` over the song
  fixture, ported menu opened on the guest's stack the way Task 7's `note_mode_spec.cpp` does):
  - `noteEditorOpen()`/`noteRowEditorOpen()` false with the ported menu closed, true per root when open;
  - the offset gate's note-mode arm: with the note editor open and **no popup**, one
    `adjust_note_probability` turn through the bridge lands and raises no legacy popup; the same for
    velocity (`adjustVelocity`) and one row entry (`adjust_row_probability`) under the row editor —
    extending Task 4's legacy-gate example in `note_bridge_spec.cpp`, which must stay green;
  - each gated pair: its port equivalent observed (menu level after `goUpOneLevel`'s port; menu closed
    at depth 0; note menu entered/left on `toggleNoteEditorParamMenu`'s press/release, with
    `activeNoteMode()` still `NoteRowEditor`), and no crash — run the forwarded handler for real;
  - one legacy-leg example per edited file where a spec can reach it: ported menu closed → legacy's
    arm, unchanged.
- [ ] **Step 2: Implement.** Predicate-only edits; gated pairs keep legacy's statements byte-identical in
  the `else`. Keep every existing comment.
- [ ] **Step 3: Whole gate block** (the plan's Global Constraints), including the level-3 A/B, which must
  be green with **no golden changes** — this task changes nothing the legacy leg or today's script reaches.
- [ ] **Step 4: Commit** named paths only:
```bash
git add src/deluge/gui/ui/shell_note_mode.h src/deluge/gui/ui/shell_note_mode.cpp \
        src/deluge/gui/views/instrument_clip_view.cpp src/deluge/gui/views/view.cpp src/deluge/gui/views/timeline_view.cpp \
        src/deluge/model/action/action_logger.cpp src/deluge/model/note/note_row.cpp src/deluge/gui/ui/notify_drain.cpp \
        sim/scenes_spec/note_mode_reads_spec.cpp sim/scenes_spec/note_bridge_spec.cpp
# plus src/scenes/menu/menu_scene.{h,cpp} and scripts/check_layer_rules.py only if touched
git commit -m "feat(guest): legacy's note-editor reads and menu calls see the ported note mode"
```

---

### Task 8: Gate sites 12 and 13, and let the A/B see both modes

**Files:**
- Modify: `src/deluge/gui/views/instrument_clip_view.cpp` (`enterNoteEditor` `:3581`, `enterNoteRowEditor` `:3735`)
- Modify: `src/deluge/gui/ui/shell_menu_entry.cpp` (the table's rows 12 and 13)
- Modify: `tests/ui-scripts/app/menu.txt`
- Goldens: `tests/golden/ui/app/menu/`, `tests/golden/ui/app/menu_shell/` (recorded, read)

**Interfaces:**
- Consumes: everything above.

- [ ] **Step 1: The gates**

In each function, replace only `soundEditor.setup(clip, &noteEditorRootMenu)` + the kit toggle + `openUI(&soundEditor)` with the gated pair — `if (shellMenuEnabled()) { ok = openShellMenu(clip, 0, false, false, false, false, ShellMenuNode::ContextRoot, NoteMode::NoteEditor); } else { <the legacy statements unchanged> }` — and keep every statement after the open on both legs: `blinkSelectedNote()` and the repaint for site 12; the silent re-audition and `blinkSelectedNoteRow()` for site 13. Follow site 6's spelling (`model/clip/instrument_clip_minder.cpp:399-424`).

Repoint the stale citations: both sites cite `sound_editor.cpp:1757-1759` / `:1760-1762`, which are `:1774-1779`; so do `gui/views/automation_view.cpp`'s site-7 comment and `scenes/menu/context.cpp`'s note on the note-editor roots. Update rows 12 and 13 of `shell_menu_entry.cpp`'s table: gated, and to what.

- [ ] **Step 2: Append the gestures**

At the end of `menu.txt`, before `quit` (move `quit` later), each block commented with its legacy statement:

1. Press and hold an empty main-grid pad — it creates the note — then SELECT: the note editor (`instrument_clip_view.cpp:853`). Snapshot.
2. KIT: focus moves to probability. Snapshot.
3. Select turn +1: probability edits, the notification shows legacy's string. Snapshot.
4. Gold turn +1: it reaches `ModKnobs`, not the menu. Snapshot.
5. BACK: the note editor closes and `exitNoteEditor` unhighlights. Release the pad. Snapshot.
6. Press and hold the audition pad (`pad 17 4`), then SELECT: the note-row editor (`:858`). Snapshot.
7. BACK, release. Snapshot.

**Verify what the root and state are before each gesture from the snapshots, not from this list.** If a gesture lands somewhere else on the legacy leg, the script is wrong, not the port.

- [ ] **Step 3: Record, verify, determinism**

```bash
cmake --build build-sim --target deluge_host -j8
GFX_GOLDEN_UPDATE=1 ctest --test-dir build-sim -R ui_app_scripts
ctest --test-dir build-sim -R ui_app_scripts && ctest --test-dir build-sim -R ui_app_scripts
git status --short tests/fixtures/
```
Expected: green, identical across runs, fixtures untouched. Read every new panel on both legs and describe them in the report.

- [ ] **Step 4: Two mutations — prove the panels are compared**

Revert one at a time, rebuild, run `ctest --test-dir build-sim -R ui_app_scripts_menu_ab`, restore:
- (a) make KIT do nothing in `MenuScene::handle` → must fail naming the snapshot of step 2;
- (b) make `note_editor_probability`'s column `value_text` return the bare number → must fail naming the snapshot of step 3.

Record each checker's exact output. Check `git status` before each restore.

**Coverage, stated:** entry, first page, button focus, a select edit, a gold turn and exit on the note editor; entry and exit on the note-row editor. **Not covered at level 3:** both encoder routes into `instrumentClipView` and the pad lights.

- [ ] **Step 5: Whole gate block, then commit**

```bash
git add src/deluge/gui/views/instrument_clip_view.cpp src/deluge/gui/ui/shell_menu_entry.cpp \
        src/deluge/gui/views/automation_view.cpp src/scenes/menu/context.cpp \
        tests/ui-scripts/app/menu.txt tests/golden/ui/app
git commit -m "feat(guest): gate sites 12 and 13 -- the note editors open ported, and the A/B drives them"
```

---

### Task 9: Record what shipped, in the tree

**Files:**
- Modify: `src/scenes/README.md` (the carry table)
- Modify: `docs/superpowers/specs/2026-08-26-ui-migration-design.md` (§10 and §10.2 item 2)
- Modify: `docs/superpowers/specs/2026-08-26-ui-scenes-menus-design.md` (the horizontal-page passage)
- Modify: `docs/superpowers/specs/2026-08-26-ui-toolkit-design.md` (the `values` and `columns` contracts)

**Read the branch's commits and task reports first** — record what happened, not this plan's intentions.

- [ ] **Step 1: Carry table.** Strike the row saying `greyout_mask`'s note arms are left to another scene. Add rows, each with legacy citation and what closing it needs: chained horizontal menus and `HorizontalMenuGroup`; SHIFT + CROSS_SCREEN/SCALE at depth 0; automation retargeting on a column focus change (`handlePotentialParamMenuChange`); `currentUIMode` gating of select editing; a select press acted on at press where legacy acts at release; `Submenu::shouldForwardButtons` also forwarding `buttonAction`/`selectButtonPress` to a focused `Custom` column; `bindings::FeedbackHook` never installed; the note bridge's sampled real-view coverage. Update the menu-side acceleration row to say it now applies to select editing on horizontal pages.

- [ ] **Step 2: Migration spec.** §10: the menu input-mapping divergence — what it was, that it was on every horizontal page and number screen by default, fixed here, and why it survived (no A/B ever turned a knob on a value screen or pressed an instrument button in a menu). §10.2 item 2: sites 12 and 13 gated — **count the table yourself** for the new figure; do not take one from this plan.

- [ ] **Step 3: Two design specs.** In the scenes-menus spec, correct the passage on horizontal pages and `handleItemAction`. In the toolkit spec, rewrite the `values` fine/coarse contract and `columns`' turn ownership.

- [ ] **Step 4: Verify every citation you wrote with `sed -n`, then the gate block and commit.**

```bash
git add src/scenes/README.md docs/superpowers/specs/2026-08-26-ui-migration-design.md \
        docs/superpowers/specs/2026-08-26-ui-scenes-menus-design.md docs/superpowers/specs/2026-08-26-ui-toolkit-design.md
git commit -m "docs: what the note family and the menu input mapping carry, defer and fixed"
```

---

## Self-review

**Spec coverage.** §3.2's widgets → Task 1; its horizontal controls, select press, LEDs and popup rule → Task 2; §3.3's carries → Task 9. §4's eight sites: 1 greyout and 5 pad lights → Task 6; 2, 3, 6, 7 → Task 7; 4 exit → Task 7; 8 pads → Task 7. §5.1 → Task 6; §5.2-5.3 → Task 3; §5.4 → Task 4; §5.5 → Task 5, with sequence direction's row arm in Task 6. §6's testing is distributed with each task, and the level-3 coverage is stated in Task 8. §7's two open questions are rulings 4 and 5. §8's three findings are Task 9 rows.

**Ordering.** Nodes (Task 5) exist before `kRoots` names them (Task 6); `Root` enumerators land in the same task as every array indexed by them, because an aggregate-initialised array silently value-initialises a missing entry. Nothing is reachable from a legacy gesture until Task 8.

**Type consistency.** `AdjustFn`, `ValueTextFn`, `ValueText`, `ValueTextUse`, `PermittedFn`, `NoteBridge`, `MenuContext::Root::{NoteEditor,NoteRowEditor}` are `deluge::scenes::menu`'s; `NoteMode`, `NoteModeKind`, `forwardNote*`, `activeNoteMode`, `exitNoteModeBeforeClose`, `wireShellNoteBridge` are `deluge::gui::ui`'s. `NoteMode` is the argument a caller passes; `NoteModeKind` is what the guest reads back; they are separate because the first belongs to `openShellMenu`'s header and the second to a translation unit that header must not include.

**Placeholders.** The `<...>` markers in Task 1's and Task 2's example bodies stand for frame and rig construction copied from the named neighbour specs; each sits beside the assertion it must make possible.
