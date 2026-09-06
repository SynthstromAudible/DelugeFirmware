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
| `LedId`, `IndicatorId`, `LedState`, `KnobIndicator`, `LedFrame` | `core/led_frame.h` |
| `knob_indicator_levels`, `knob_indicator_blink` (a gold knob's 4-segment column) | `core/knob_indicator.h` |
| text/geometry constants (`kMenuBaseY48/64`, `kHorizontalMenuBaseY`, `kScrollTime`, …), `text_height_of` | `core/metrics.h` |
| `RepaintGate` | `core/repaint_gate.h` |
| `Response` | `core/response.h` |
| `ListState`, `ColumnsState`, `ScrollText`, `Blink`, `Hold`, `Timer`, `WaveformState` | `core/state.h` |
| `Style` (every field read by a widget; `menu_base_y` is the panel-height value), `kDelugeStyle48`, `kDelugeStyle64` | `core/style.h` |
| `WrappedLines`, `wrap_lines` | `core/text_wrap.h` |

## OLED catalogue (`src/ui/oled`, spec B §6)

| widget / helper | file |
|---|---|
| `OledFrame` (`pixel`, `hline`, `vline`, `line`, `rect`, `rect_rounded`, `invert`, `invert_rounded`, `clear`, `glyphs`, `glyph`, `graphic`, `icon`, `icon_centered`, `region`, `bounds`, `visible`, the input accessors and `claim_*`, `gate`), `root_frame(canvas, style, ctx, mask = InputMask::all())` | `oled/frame.h` |
| `Align`, `TextStyle`, `text_height_for`, `spacing_x_for`, `measure`, `text`, `title`, `title_badge` | `oled/text.h` |
| `kSamplePositionLineGap`, `sample_position` | `oled/sample_position.h` |
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
| `GridFrame` (`pad`, `peek`, `local_events`, `first_press_in`, `interact`, `held_pads`, `region`, `bounds`, `visible`, the input accessors and `claim_*`, `gate`), `root_grid_frame(grid, ctx, mask = InputMask::all())` | `grid/frame.h` |
| `pad` — see `GridFrame::pad` above (the primitive every component is built from) | `grid/frame.h` |
| `NoteFill`, `NoteCell`, `ClipRowData`, `row`, `clip_row` | `grid/rows.h` |
| `LaunchStyle`, `StatusColours`, `StatusPad`, `AuditionPad`, `status_colour`, `status_pad`, `audition_colour`, `audition_pad`, `sidebar_columns` | `grid/sidebar.h` |
| `MacroKind`, `MacroCell`, `macro_colour`, `macro_wants_arm_flash`, `macros` | `grid/columns.h` |
| `NoteSquare`, `VelocityData` (uses `NoteSquare`), `velocity_colour`, `velocity_column` | `grid/columns.h` |
| `AutomationData`, `automation_colour`, `automation_column` | `grid/columns.h` |
| `vu_max_row`, `vu_row_colour`, `vu_meter` | `grid/columns.h` |
| `TickColour`, `TickSquare`, `tick_colour`, `tick_squares` | `grid/columns.h` |
| `PeakCol`, `waveform_col_colour`, `waveform_row`, `waveform_full` | `grid/waveform.h` |
| `kMaxMarkers`, `MarkerStack`, `marker_tint`, `marker_column` | `grid/markers.h` |
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

`root_frame`/`root_grid_frame` take an `InputMask` too, defaulted to `all()` — the shell's lever at
the *top* of the tree, where there is no parent frame to call `region(rect, false, …)` on. Several
scenes can render onto one surface in a tick (an overlay over a base scene); the shell builds the
frontmost, focused one's root with `all()` and every scene beneath it with `none()`, so a scene
under an overlay still renders — a `list` built over that root still paints its window and
highlight — but reads 0/false from every accessor and claims nothing, exactly as an unfocused
`region()` does one level down. Pads are unaffected by the root's mask either way (rule 4): a
`GridFrame` root built with `none()` still reports every press through `pad()`/`local_events()`,
because a press names its target by where it landed, not by focus.

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
2. **Reading is claiming.** `turn()`, `edit(i)`, `horizontal()`, `vertical()`, `select_pressed()`
   and `back_pressed()` each record the claim for their own input, on the frame that read them, in
   the same call that returns the value — there is no separate step a widget can forget. A claim
   means *"this control owns that input this tick"*, so a focused `list` claims the turn on every
   frame it renders — not only when a detent arrives, and not only when it has rows (`list`,
   `columns` and `enum_value` all claim before their empty-model early-outs, which is what the
   explicit `claim_turn()`, `claim_edit(i)`, `claim_horizontal()`, `claim_vertical()`,
   `claim_select()`, `claim_back()` methods are still for: owning an input without ever reading it).
   Both paths run through the same idempotent bookkeeping, keyed per `FrameInput` instance, so a
   widget that reads `turn()` three times, or reads it and then calls `claim_turn()` too, claims it
   exactly once. The shell reads `ctx.claims` after rendering and may deliver whatever is left over
   to the scene; `InputClaims::any()` answers "was anything consumed at all", and `operator==` lets
   a spec pin a whole claim set at once.

   **A claim records ownership; it does not arbitrate.** An input reaches every frame whose mask
   grants it, so two sibling regions that were both granted the turn each move their `list`.
   Granting an input to at most one region is the parent's job, and that is what `InputMask` is
   for. `InputClaims::double_claim` is set when an input already claimed this tick by *another*
   frame is claimed again — the diagnostic for getting that wrong, not a defence against it; it
   never fires for one frame reading (or claiming) the same input more than once. Before rule 2
   read this way, a hand-written reader of `f.back_pressed()` that never called `claim_back()`
   left the input looking unclaimed, so the shell's post-render delivery would hand it to
   `Scene::handle()` a second time — a genuine double-consumption with `double_claim` false,
   because nothing paired the read with a claim. Folding the claim into the read closes that gap:
   there is no longer a way to read a masked-in input through a frame without also owning it.
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
`double_claim` on two focused lists, and the horizontal menu spending one detent exactly once. It
also pins reading-is-claiming (one frame reading `turn()` three times claims it once and never
trips `double_claim`; two frames each only reading it, with no `claim_*()` call at all, still trips
`double_claim`) and `root_frame`/`root_grid_frame`'s `mask` argument (a `none()` root leaves a
`list` inert while the grid side's pads still report).

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

### Compositing and tweens (`gfx::grid`, task-6/7) -- no `ui/grid` caller yet

`gfx::grid::composite(base, overlay, BlendMode, out)` and the five `from -> to` tween functions
(`fade`, `scroll_horizontal`/`_vertical`, `smear_scroll_horizontal`/`_vertical`, `zoom`,
`explode`, `expand_collapse`) live one layer down, in `gfx/grid/composite.h` and
`gfx/grid/tween.h` -- see `src/gfx/README.md` for what each ports and the legacy quirks they
reproduce. Nothing in `src/ui/grid` calls either yet: overlay compositing and animated
transitions are a shell/scene concern (spec D §7's `render_grid`/tween driver), not a widget one,
so they wait for that caller rather than growing a placeholder wrapper here.

### Two-pass `list`: this frame's row count, not last frame's (task-8)

`list` counts `rows.size()` -- the span it was actually handed *this* frame -- before doing
anything else, clamps the cursor into that count, then walks `turn`, all before drawing a pixel.
`state.rows_last` is still published every frame, but only as a cache for a caller that windows
against it on its own (`enum_value` sizes its inner windowed `list` call from its own copy of the
field); `list` itself never reads it back. A list that shrinks or grows under the cursor is
therefore correct on the same frame it changes, not one frame late the way clamping against
`rows_last` would have left it. See `oled/list.h`.

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
- **`deluge-sdk` ideas surveyed and deliberately not harvested (task-10).** Two audited ideas were
  looked at and turned down, not overlooked:
  - **Ink-bounds centring** (the SDK's `hmenu.rs:57-110` `InkBounds`, which records a widget's
    actually-lit pixels via a pixel-discarding draw target and centres on *that* rather than on
    its nominal box, unioning both value extremes so layout doesn't jitter as a value changes).
    Not adopted for the same reason `measure()` returns the legacy advance width rather than ink
    bounds (§6 of spec B): every legacy centring/right-align call site — and every `meters.h`
    widget's `slot` centring — was laid out against the *nominal* geometry, blank padding
    included; switching to ink bounds would move pixels relative to legacy on every widget that
    isn't already visually flush with its box, which is exactly the kind of change this port's
    parity rule exists to prevent.
  - **The structural `FrameOutput` gate** (the SDK's `GridUi::run`, which returns
    `FrameOutput::Skipped` without ever invoking the render closure on a clean frame, so a skipped
    frame is *structurally* incapable of producing a stale result). Not adopted: it needs a
    closure-shaped render call (`ui.run(|f| ...)` returning a value) to have something to
    withhold, and this toolkit's render calls are virtual methods returning `void`
    (`Scene::render_oled`/`render_grid`, spec D §3), not closures — there is no return value for
    a `Skipped` to replace. The advisory shape stays: `RepaintGate::needs_frame()` is a bare
    predicate the shell consults before calling `render_oled`/`render_grid` at all (spec D §7), so
    a skipped frame already does not render — but nothing stops a scene from acting on stale POD
    state in `handle()`/`after_render()` regardless of whether that tick rendered, the way the SDK's
    structural gate would prevent by construction. Left as a known gap rather than closed.

## Legacy quirks reproduced on purpose

Ported behaviour that visibly diverges from a plausible fix, kept because parity with the
shipped firmware outranks correcting it — the user decides per item, later. Each is pinned by a
spec/golden that would have to change along with the fix, so an accidental "fix" fails CI before
it ships.

| # | quirk | where | pinned by | one-line fix |
|---|---|---|---|---|
| 1 | `popup_rect_for` centres popups on the full 48-row panel geometry, bezel-hidden rows included, so a popup sits visibly above the true visible centre on the shipped device (and would centre correctly, and therefore *inconsistently*, on a 64-row panel where `visible_top == 0`) | `oled/popup.cpp:109-119` (`panel_of(f)`) | `tests/spec/ui/popup_spec.cpp` "centres the popup box on the panel, at both geometries"; `popup_three_lines_48.pbm`, `popup_saved_48/64.pbm`, `popup_wrapped_48.pbm`, `popup_notification_48.pbm`, `popup_permanent_48/64.pbm` | make it a `Style` decision (`Style::popup_centre_on_full_height`, defaulting `true` for parity) and centre on `f.bounds()` when it is `false` |
| 2 | `decimal_value` draws at a canvas-absolute y (converted back to frame-local only by subtracting `f.origin.y`), ignoring `rect.y`/`rect.h` entirely — the one widget that cannot be placed anywhere in its frame, and the one that lands at a different position relative to `int_value` on a 64-row panel | `oled/values.h:81-90`, `oled/values.cpp:172-173` | `tests/spec/ui/values_spec.cpp` "D4"; `decimal_value_12_34_48.pbm` | offset the digit/cursor rows from `rect.y` instead of from the legacy literal, the way `int_value` already does |
| 3 | `kHorizontalMenuBaseY` is the literal `14` on **every** panel height, while `list`'s vertical-menu base y is `14`/`15` (48/64 rows) — a one-row inconsistency between the horizontal and vertical menus frozen into the profile system, based on a 64-row arm of `HorizontalMenu::renderMenuItems` that is dead code in legacy (the shipped Deluge panel is 48) | `core/metrics.h:36`, `oled/columns.cpp:284-285` | `tests/spec/ui/columns_spec.cpp` "puts the label row at the legacy base_y + box_height - label_height"; `columns_four_64.pbm` | give it a 64-row arm (`15`) mirroring `list`'s `kMenuBaseY64`, keyed off `f.canvas.geometry().height` the way `menu_rect` already is |
| 4 | `GreyoutMask::cols`' bits run right-to-left — bit 0 selects the *last* (rightmost) column, bit 17 selects column 0 — matching legacy `prepareColour`'s own mirrored numbering rather than a natural left-to-right convention a brand-new POD type would otherwise get | `grid/effects.h:18-33` | `tests/spec/ui/effects_spec.cpp` (bit-0/bit-17 cases, lines 72–133) | flip the bit test in `greyout()` to natural (left-to-right) order and mirror the index once, in the legacy bridge that builds a `GreyoutMask` from `greyoutCols`, instead of at every scene call site |
| 5 | `wrap_lines`' `max_chars_per_line` parameter is read by nothing — every legacy caller passes `19` (`kPopupMaxCharsPerLine`) but no line of `breakStringIntoLines`/`findLineSplitPoint`/`addLine`/`addCharacterToLine` ever reads the field; only `max_width_px` caps a line | `core/text_wrap.h:51-62`, `core/text_wrap.cpp:74-76` | `tests/spec/ui/popup_spec.cpp` "ignores max_chars_per_line, because legacy never reads the field" | drop the parameter from `wrap_lines`'s signature and its call sites' `kPopupMaxCharsPerLine` argument |
| 6 | `length_slider`'s `min_pos` defaults to `true`, because every legacy call site inherited `drawLengthSlider`'s `= 3` default (`number.h:57`) — a near-certain legacy typo (`3` is truthy, not a deliberate `min_pos = true`), reproduced verbatim as a **default argument** on a brand-new API, so every future call site inherits it too unless it opts out | `oled/meters.h:108-118`, `oled/meters.cpp:189-198,391` | `tests/spec/ui/meters_spec.cpp` "puts the thumb one pixel right when min_pos, and defaults it on" (D5); `meter_length_slider_48/64.pbm` | flip the default to `false` and re-record the two goldens |

Five more, in the four tweens `gfx/grid/tween.cpp` ported from `hid/led/pad_leds.cpp`
(toolkit-fixes task-7; full detail in `src/gfx/README.md`'s tween section):

| # | quirk | where | pinned by | one-line fix |
|---|---|---|---|---|
| 7 | Complementary blend weights are computed as `65535 - frac`, not `65536 - frac` (`explode`'s x/y intensity pairs, `expand_collapse`'s row-position weight) — a pad exactly on a square boundary renders at 65535/65536 intensity, imperceptibly darker than a true "still" frame | `pad_leds.cpp:679-680,690-691,370-371`, ported in `gfx/grid/tween.cpp` | `tests/spec/gfx/tween_spec.cpp`'s `legacy::` oracle byte-compares for `explode`/`expand_collapse` (the oracle itself uses `65535 -`, so it would reject a `65536 -` "fix" made to only one side) | change `65535 - frac` to `65536 - frac` in the port **and** the oracle together |
| 8 | `expand_collapse`'s row position is narrowed to `int8_t` (legacy `newRowPosition1Array`), which would wrap for a destination row past ±127 on a real display — unreachable today, and made explicitly unreachable here by `DELUGE_GFX_CONTRACT(row_to < rows)` rather than silently relying on it | `pad_leds.cpp:359`, `gfx/grid/tween.h`'s `ExpandCollapseParams` contract | `tests/spec/gfx/tween_spec.cpp`'s `expand_collapse` oracle byte-compares (any `row_to`/`t` case) | widen the row-position field to `int16_t` (or wider) in the port and the oracle together |
| 9 | `explode`'s per-column intensity scratch is left logically dependent on `xStart`/`xEnd` excluding every column whose destination lands off-screen, rather than being unconditionally written for every column — legacy's version of this left the *storage* uninitialised too (closed here in task-7 fix round 1 by zero-initialising the port's own scratch; the algorithm's dependence on the excluded range is what remains, not a memory hazard) | `pad_leds.cpp:672-678`, `gfx/grid/tween.cpp`'s `explode` | `tests/spec/gfx/tween_spec.cpp`'s `explode` oracle (which also zeroes its copy of the array, so a read of an excluded entry would show up as a mismatch — it doesn't) | write every column's intensity pair unconditionally (even the ones whose destination is excluded), rather than relying on the excluded range never being read |
| 10 | `zoom`'s inner-image pin arithmetic truncates each operand to 8 bits *before* multiplying (`(pin_square >> 8) * (in_image_times_bigger >> 8)`), losing up to ~1/256 of a pad of precision in the inner image's on-screen origin — legacy's own comment reads "Beware rounding inaccuracy" | `pad_leds.cpp:1251-1253`, `gfx/grid/tween.cpp`'s `zoom` | `tests/spec/gfx/tween_spec.cpp`'s `zoom` oracle byte-compares across `t`/magnitude cases | multiply the two full-precision operands first, then shift the product once, instead of shifting both operands down before multiplying |
| 11 | `zoom` treats a source pad that is genuinely black as "nothing drawn there" and *assigns* black to the output square, rather than leaving whatever the destination frame already held — the reason `zoom`'s output is a full frame, never an overlay | `pad_leds.cpp:1327-1333,1358`, `gfx/grid/tween.cpp`'s `zoom` | `tests/spec/gfx/tween_spec.cpp`'s `zoom` oracle byte-compares (any case with a black source region) | track "was anything drawn to this pad" independently of colour, and skip the black assignment when nothing was, instead of using black itself as the sentinel |

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
