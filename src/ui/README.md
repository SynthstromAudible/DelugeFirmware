# deluge_ui_core / deluge_ui_oled / deluge_ui_grid

The immediate-mode toolkit for the Deluge UI stack (spec: `docs/superpowers/specs/2026-08-26-ui-toolkit-design.md`,
spec B of the UI stack redesign, §2).

Rules (CMake deps + the layer checker from the gfx plan, extended):
1. `ui-oled` and `ui-grid` never include each other.
2. None of the three includes `notify`, `model`, `libdeluge`, `l10n`, `gui/`, `hid/`.
3. No mutable globals; no heap in any widget; widgets are not templates (only `region`
   bodies are, plus the two named legacy-bridge exceptions in `keyboard.h` below).
4. Every widget takes its state by reference from the caller and reads input only through the
   frame's accessors (`f.turn()`, `f.edit(0)`, `f.select_pressed()`, ...) -- never through the
   snapshot behind it. See "The input-consumption contract" below.

Rules 1 and 2 are enforced mechanically, not just by convention (see "Layer rules" below).

## Layout

```
deluge-ui-core   InputSnapshot, Response, RepaintGate, POD state structs, Style, LedFrame
                 deps: gfx-oled (Rect, FontId) — types only; NOT gfx-grid (nothing under core/ includes it)
deluge-ui-oled   OledFrame + widgets (namespace deluge::ui::oled)      deps: ui-core, gfx-oled
deluge-ui-grid   GridFrame + components (namespace deluge::ui::grid)   deps: ui-core, gfx-grid
```

All three targets are `STATIC` libraries (core since Task 2, oled since Task 3, grid since
Task 10 of the toolkit plan).

## Layer rules

Enforced by `scripts/check_layer_rules.py` (ctest `layer_rules`, pre-commit hook
`check-layer-rules`) and, for the sibling-relative-include case its slash-based regex cannot
see, `scripts/check_gfx_layer.py` (ctest `gfx_layer_check`, `--root src/gfx` only — it does not
run over `src/ui`).

Four ratchet rows cover `src/ui` (baseline `scripts/layer_rules_baseline.txt`, all four
currently at count 0 — any occurrence is a regression):

| rule | applies to | forbids |
|---|---|---|
| `ui-includes-outside` | `src/ui/**` | any `#include` outside `ui/`/`gfx/` whose path contains a slash, **plus** the root headers the plan's Global Constraints name (`definitions*`, `board_config.h`, `board_layout.hpp`), which are slash-free at their include sites (a std header is neither, so it still never matches) |
| `ui-oled-no-grid` | `src/ui/oled/**` | `#include "ui/grid/..."` |
| `ui-grid-no-oled` | `src/ui/grid/**` | `#include "ui/oled/..."` |
| `ui-widgets-read-input-directly` | `src/ui/{oled,grid}/**` except `frame.{h,cpp}` | `input.turn` / `input.edit` / `input.horizontal` / `input.vertical` / `input.select_pressed` / `input.back_pressed` / `input.shift` / `input.now_ms` — the fields a widget must read through its frame, so that focus masking and claims cannot be forgotten. The two frame files are the exception because they *are* the accessors |

Probe (also exercised by `ctest -R layer_rules` and the pre-commit hook, whose `files:`
pattern covers `src/ui/`): add `#include "ui/grid/frame.h"` to `src/ui/oled/text.h` →
`scripts/check_layer_rules.py` exits 1 naming `ui-oled-no-grid`; remove it → exits 0. The
named-root-header arm has its own probe: add `#include "definitions_cxx.hpp"` to any header
under `src/ui/` → exits 1 naming `ui-includes-outside`; remove it → exits 0.

## Core types (`src/ui/core`)

| type | file |
|---|---|
| `EncoderRole`, `PadEvent`, `PadMask`, `InputSnapshot` | `core/input.h` |
| `InputMask`, `InputClaims`, `FrameContext` (+ `begin_tick`), `FrameInput` (the accessors/claims both frames inherit, and the input-consumption contract) | `core/frame_context.h` |
| `LedId`, `RingId`, `LedState`, `KnobRing`, `LedFrame` | `core/led_frame.h` |
| text/geometry constants (`kMenuBaseY48/64`, `kHorizontalMenuBaseY`, `kScrollTime`, …), `text_height_of` | `core/metrics.h` |
| `RepaintGate` | `core/repaint_gate.h` |
| `Response` | `core/response.h` |
| `ListState`, `ColumnsState`, `ScrollText`, `Blink`, `Hold`, `Timer`, `WaveformState` | `core/state.h` |
| `Style` (every field read by a widget; `menu_base_y` is the panel-height value), `kDelugeStyle48`, `kDelugeStyle64` | `core/style.h` |
| `WrappedLines`, `wrap_lines` | `core/text_wrap.h` |

## OLED catalogue (`src/ui/oled`, spec B §6)

| widget / helper | file |
|---|---|
| `OledFrame` (`pixel`, `hline`, `vline`, `line`, `rect`, `rect_rounded`, `invert`, `invert_rounded`, `clear`, `glyphs`, `glyph`, `graphic`, `icon`, `icon_centered`, `region`, `bounds`, `visible`, the input accessors and `claim_*`, `gate`), `root_frame(canvas, style, ctx)` | `oled/frame.h` |
| `Align`, `TextStyle`, `text_height_for`, `spacing_x_for`, `measure`, `text`, `title` | `oled/text.h` |
| `scroll_text` | `oled/scroll_text.h` |
| `ListRow`, `menu_rect`, `list_visible_rows`, `list_window_start`, `list`, `scrollbar` | `oled/list.h` |
| `Range`, `ValueStyle`, `int_value`, `decimal_value`, `enum_value`, `toggle` | `oled/values.h` |
| `FilterKind`, `StageKind`, `Hint`, `percent`, `knob_arc`, `unipolar_bar`, `pan`, `slider`, `length_slider`, `filter_curve`, `envelope_stage`, `sidechain_ducking`, `meter` | `oled/meters.h` |
| `Column`, `Paging`, `paging_of`, `columns`, `title_paged`, `MeterColumn`/`meter_column`, `TextColumn`/`text_column`, `submenu_column`, `ToggleColumn`/`toggle_column` | `oled/columns.h` |
| `PopupStyle`, `popup_rect_for`, `popup_frame`, `popup_text`, `working`, `progress` | `oled/popup.h` |
| `OutputNameData`, `output_name`, `segment_digits`, `vu`, `envelope`, `graphic`, `icon` | `oled/misc.h` |
| `detail::int_to_string` | `oled/detail/int_to_string.h` (internal helper, not a widget — used by `values.h`/`meters.h`/`misc.h`'s number rendering; not listed as a catalogue entry) |

`waveform` on OLED is deferred to spec E's sample-view scene: its data shape depends on
`WaveformRenderData`, which is model-side, so it is out of scope for this leaf toolkit.
`WaveformState` (`core/state.h`) is retained for it. Recorded in the spec's Amendments.

## Grid catalogue (`src/ui/grid`, spec B §7)

| component / helper | file |
|---|---|
| `GridFrame` (`pad`, `peek`, `local_events`, `first_press_in`, `interact`, `held_pads`, `region`, `bounds`, `visible`, the input accessors and `claim_*`, `gate`), `root_grid_frame(grid, ctx)` | `grid/frame.h` |
| `pad` — see `GridFrame::pad` above (the primitive every component is built from) | `grid/frame.h` |
| `NoteFill`, `NoteCell`, `ClipRowData`, `row`, `clip_row` | `grid/rows.h` |
| `LaunchStyle`, `StatusColours`, `StatusPad`, `AuditionPad`, `status_colour`, `status_pad`, `audition_colour`, `audition_pad`, `sidebar_columns` | `grid/sidebar.h` |
| `MacroKind`, `MacroCell`, `macro_colour`, `macro_wants_arm_flash`, `macros` | `grid/columns.h` |
| `NoteSquare`, `VelocityData` (uses `NoteSquare`), `velocity_colour`, `velocity_column` | `grid/columns.h` |
| `AutomationData`, `automation_colour`, `automation_column` | `grid/columns.h` |
| `vu_max_row`, `vu_row_colour`, `vu_meter` | `grid/columns.h` |
| `TickColour`, `TickSquare`, `tick_colour`, `tick_squares` | `grid/columns.h` |
| `PeakCol`, `waveform_col_colour`, `waveform_row`, `waveform_full` | `grid/waveform.h` |
| `KeyboardLayoutKind`, `TextKeyboardState`, `text_keyboard_char_at`, `text_keyboard_key_colour`, `text_keyboard_is_shift_pad`, `text_keyboard` | `grid/keyboard.h` |
| `LegacyImage`, `copy_legacy_image`, `copy_legacy_column`, `keyboard<Layout>`, `control_column<Column>` (legacy-bridge templates, removed in phase 6) | `grid/keyboard.h` |
| `GreyoutMask`, `greyout` | `grid/effects.h` |
| `GestureEvent`, `HoldPolicy`, `gesture` | `grid/effects.h` |

### Legacy-bridge templates (`grid/keyboard.h`)

`keyboard(GridFrame&, Layout&)` and `control_column(GridFrame&, int32_t, Column&, void*)` are
templates purely so `deluge-scenes` can pass the existing `gui/ui/keyboard/` `KeyboardLayout`
and `ControlColumn` classes through without `ui/grid` including `gui/` (rule 2 above) —
templating on the caller-supplied concrete type keeps the real name out of this leaf. Together
with `GridFrame::region`/`OledFrame::region`, these are the only templates in the toolkit
(rule 3). Both adapters, and the `RGB[8][18]` scratch-image shape they copy through
(`LegacyImage`, `copy_legacy_image`, `copy_legacy_column`), are deleted in phase 6 of the UI
stack redesign, once the keyboard layouts are rewritten as components that paint through the
frame directly.

## The input-consumption contract

One `FrameContext` (`core/frame_context.h`) is built per tick per surface set — by the shell in
the firmware, by the rigs in `tests/spec/ui/support/frames.h` — and every frame in the tree holds
a reference to it:

```cpp
FrameContext ctx{input, gate};                 // + claims{}, tick, tick_ms
ctx.begin_tick(now_ms);                        // exactly once per tick, before any frame
OledFrame f = root_frame(canvas, style, ctx);  // GridFrame: root_grid_frame(grid, ctx)

f.region(pane, [](OledFrame& p) { … });                        // inherit every input
f.region(pane, false, [](OledFrame& p) { … });                 // draw only
f.region(pane, InputMask{.turn = false}, [](OledFrame& p) { … }); // all but the turn
```

`begin_tick()` clears `claims` and bumps `tick`/`tick_ms`. Claims are per-tick by definition: a
context reused without it would latch the first tick's claims and withhold those inputs from
`Scene::handle()` for ever after. The rigs expose the same call (`r.begin_tick(now_ms)`), which
also moves the rig's clock.

Both frames inherit `FrameInput` (`core/frame_context.h`) — `FrameContext& ctx; InputMask mask;`
plus every accessor, every `claim_*()` and `gate()`, written once for both surfaces — in place of
the old `const InputSnapshot& input; RepaintGate& gate;` pair. Both stay aggregates: a public,
non-virtual base is allowed in one, and their aggregate initialisation simply begins with a
`{ctx, mask}` brace pair for the base subobject.

1. **Widgets read input only through the frame.** `turn()`, `edit(0)`/`edit(1)`, `horizontal()`,
   `vertical()`, `select_pressed()`, `back_pressed()`, `shift()`, `now_ms()`, and `held_pads()`
   on the grid; `gate()` reaches the repaint gate, `mask`/`focused()` say what was granted. The
   ratchet row above keeps it that way.
2. **A widget that renders a control consumes that control's inputs and claims them in the same
   call** — `claim_turn()`, `claim_edit(i)`, `claim_horizontal()`, `claim_vertical()`,
   `claim_select()`, `claim_back()`. A claim means *"this control owns that input this tick"*, so
   a focused `list` claims the turn on every frame it renders — not only when a detent arrives,
   and not only when it has rows (`list`, `columns` and `enum_value` all claim before their
   empty-model early-outs). The shell reads `ctx.claims` after rendering and may deliver whatever
   is left over to the scene; `InputClaims::any()` answers "was anything consumed at all", and
   `operator==` lets a spec pin a whole claim set at once.

   **A claim records ownership; it does not arbitrate.** An input reaches every frame whose mask
   grants it, so two sibling regions that were both granted the turn each move their `list`.
   Granting an input to at most one region is the parent's job, and that is what `InputMask` is
   for. `InputClaims::double_claim` is set when an input already claimed this tick is claimed
   again — the diagnostic for getting that wrong, not a defence against it.
3. **A frame sees exactly the inputs its parent granted it.** Focus is an `InputMask` —
   `{turn, edit, horizontal, vertical, select, back}`, one bit each (both gold knobs share `edit`:
   they are one control) — not a flag. `f.region(rect, m, body)` gives the child `mask & m`, so it
   only ever narrows and a child can never re-grant itself what its parent did not have. A
   masked-off input reads 0/false and its `claim_*()` is a no-op, so a widget rendered without it
   paints exactly as it would have but cannot move on it. `focused()` is shorthand for "my mask is
   `all()`", and `region(rect, bool)` is the coarse spelling (`all()` / `none()`).

   `region(rect, false)` is how a composite widget draws a sub-widget for its looks alone —
   `enum_value` and `toggle` render a `list` for its window and highlight while keeping the select
   encoder for themselves. A *partial* mask is how a container keeps one input and hands the rest
   on: `columns` grants the focused column `InputMask{.turn = false}` (and every other column
   `none()`), because it spends the turn on walking the column focus — so one detent moves the
   focus and nothing else, while the gold knobs, the press and the back button reach the value
   widget in the focused column. `columns` claims the press only for a submenu column, which is
   the only shape in which it consumes one.

   **`now_ms()` and `shift()` are *not* masked.** The clock is not an input a control owns, it is
   the tick's own time: an unfocused region still scrolls its labels, still times its gestures
   (pad events are unmasked, so their interpretation must be too) and still arms the surface-wide
   `RepaintGate` with a deadline in the future rather than one permanently in the past. `shift` is
   a modifier read alongside another gesture, and focus arbitrates the gesture.
4. **Pad input is positional and never masked.** `pad()`, `peek()`, `local_events()`,
   `first_press_in()`, `interact()` and `held_pads()` report presses in an unfocused region
   exactly as in a focused one: a press names its target by where it landed.
5. **`select_pressed`/`back_pressed` are edges the caller supplies once per press** (see
   `core/input.h`); a claim is not a latch — it records ownership for the tick and is cleared by
   the next `begin_tick()`.

`tests/spec/ui/frame_context_spec.cpp` pins all five, including partial masks (an input granted
while another is withheld, and a child that cannot ask for one back), the unmasked clock (a label
that still scrolls, a gesture that still holds, a gate deadline in the future), the tick boundary,
`double_claim` on two focused lists, and the horizontal menu spending one detent exactly once.

### `interact()` and `Rect` splits (task-6)

`GridFrame::interact(local)` (`grid/frame.h`/`.cpp`) hit-tests a rectangle without painting --
the toolkit-fixes task-6 port of the SDK `Frame::interact` (`deluge-grid-toolkit`'s
`imode.rs:315-343`). Like `pad()`, it walks *every* matching event in `local` this tick rather than
stopping at the first (the SDK's own loop has no `break` either): `pressed`/`released` accumulate
independently, so a pad pressed and released in the same tick -- or two different pads both inside
`local` -- can set both flags from two different events. `velocity` is only touched by a press, so
it ends up holding the *last* matching press's velocity, exactly as `pad()`'s does. `index` (this
port's addition, with no SDK counterpart) follows the same last-write-wins rule: recomputed on
every match, so after the loop it is the *last* matching event's row-major position within
`local` -- `(event.y - local.y) * local.w + (event.x - local.x)` -- letting a widget spanning
several pads (a fader, a row of steps) read back which pad fired last without its own scan. `held`
is unrelated to any of that: true whenever any pad inside `local`, clipped to `visible()`, is
currently down -- mirroring the SDK's separate held-scan loop. `local` is clipped to `bounds()`
first, exactly like `first_press_in`.

`gfx::grid::Rect` (`gfx/grid/rect.h`) has `split_rows(at)`/`split_cols(at)` -- the same two-way
split-at-a-position shape as `gfx::oled::Rect`'s siblings (`gfx/oled/geometry.h:50-51`) and the
SDK's `Frame::split_rows`/`split_cols` (`imode.rs:367-398`), returning a `(before, after)` pair --
and `inset(int32_t)` (shrink by one uniform amount on every side). See `src/gfx/README.md` for the
full rule.

### `bounds()` vs `visible()`

`bounds()` is the rect the region *asked* for, `{0, 0, w, h}` in its own local coordinates;
`visible()` is the part of it the parent granted, in those same coordinates (empty when the
region falls entirely outside its parent, where only the extent is meaningful). Layout measures
against `bounds()`, so local `(0, 0)` stays the region's own corner even when it overhangs and a
component's arithmetic does not shift under it; drawing and pad hit-testing crop to `visible()` on
their own. Both frames now answer the same way — `GridFrame::bounds()` used to report the
*clipped* extent and now reports the requested one. Two behavioural consequences, both intended:
an overhanging grid region keeps its own origin (pinned by `grid_frame_spec` and
`frame_context_spec`), and `grid::waveform_*` now mirrors about the requested width rather than
the clipped one, which is what `waveform.h`'s "provided the frame is not clipped" caveat wanted —
unchanged for every unclipped frame, i.e. for every caller today.

## Parity

Every OLED widget pins legacy geometry in `tests/spec/ui` at both 128×48 and 128×64 (paired
`*_48.pbm` / `*_64.pbm` goldens under `tests/golden/ui/widgets/`, e.g. `envelope_48.pbm` /
`envelope_64.pbm`); grid components pin legacy colours via `.state` goldens (e.g. `macros.state`,
`vu_meter.state`, `greyout.state`) instead, since a pad surface has one geometry and the thing
worth freezing is the RGB it computes.

## Known follow-ups

- **gfx clip rect for `glyphs`/`graphic`/`icon`/`invert_rounded` (closed: done).**
  `gfx::oled::Canvas` carries a clip rectangle (`set_clip`/`clear_clip`/`clip`) that *every*
  primitive honours pixel for pixel — glyphs and bitmaps included, clipped column by column and
  by a per-page bit mask rather than all-or-nothing. `OledFrame` opens a `ClipScope` over its
  region in each drawing method, so nothing a widget draws escapes its region on any edge. The
  scope *intersects* with whatever clip the canvas already carries and restores it on the way out,
  so it can only ever narrow — a clip set from outside the frame tree (a repaint bounded to a
  damage rectangle, say) keeps bounding every widget that draws inside it. With no clip set the
  canvas draws exactly as it did before. See `oled/frame.h` and `gfx/oled/canvas.h`.
- **`Response` for grid columns (closed: `void`).** `row`/`clip_row`/`status_pad`/
  `audition_pad`/`text_keyboard` return a `Response`; the per-column components (`macros`,
  `velocity_column`, `automation_column`, `tick_squares`, `vu_meter`, `sidebar_columns`) return
  `void` and stay that way. Their interaction is *which row*, which a response collapsed over a
  whole column cannot carry, and two of them have no interaction of their own at all. The
  read-back is `GridFrame::first_press_in({x, 0, 1, rows})` — see `grid/columns.h`.
- **`menu_base_y` into `Style` (closed: folded in).** `Style::menu_base_y` is
  `MenuItem::drawItemsForOled`'s `(OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14` as a profile value;
  `list`/`menu_rect` and `output_name` read it instead of `Canvas::geometry().height`.
  `kHorizontalMenuBaseY` stays a free constant in `core/metrics.h` and is *not* folded in: it is
  14 on every panel and follows a different legacy rule (horizontal-menu box top, see
  `columns.h`), so merging the two would conflate things that only coincide at 48 rows.

## Size

`./dbt sizediff` needs a full device link, which this environment cannot currently produce
(pre-existing ThinLTO mismatch between the Rust BSP and the ARM object files, unrelated to
this toolkit — the link fails with `ld.lld: … Invalid record` on `deluge_rust.*.rcgu.o`). In
its place, `./dbt build Debug` was run to completion of all C++ compilation and archiving (it
still ends with that known device-link error); the three device (ARM, `build/src/ui/Debug`)
static archives it produced, plus the `gfx` device archives for comparison:

Per-object `size` (text, ARM Debug build) by source file within each archive, taken with
`arm-none-eabi-size build/src/ui/Debug/*.o` after that run. The archive byte counts themselves are
deliberately not recorded: a Debug `.a` size is dominated by debug info and rots on every
toolchain bump, while the per-object text below is the number a budget question actually asks.

```
libdeluge_ui_core.a:  repaint_gate 436, led_frame 172, text_wrap 1137        (total text 1745)
libdeluge_ui_oled.a:  frame 2374, text 1289, scroll_text 450, list 1578,
                      values 5271, meters 5605, columns 3953, popup 1874,
                      misc 3743                                              (total text 26137)
libdeluge_ui_grid.a:  columns 2397, effects 862, frame 970, keyboard 1851,
                      rows 1433, sidebar 2224, waveform 1431               (total text 11168)
```

No firmware code calls into `ui-core`/`ui-oled`/`ui-grid` yet, so this is code-size-at-rest, not
a size delta against a linked firmware image — that comparison is `sizediff`'s job, once a scene
calls into the toolkit and the device link works again.
