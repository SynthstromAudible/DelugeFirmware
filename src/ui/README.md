# deluge_ui_core / deluge_ui_oled / deluge_ui_grid

The immediate-mode toolkit for the Deluge UI stack (spec: `docs/superpowers/specs/2026-08-26-ui-toolkit-design.md`,
spec B of the UI stack redesign, §2).

Rules (CMake deps + the layer checker from the gfx plan, extended):
1. `ui-oled` and `ui-grid` never include each other.
2. None of the three includes `notify`, `model`, `libdeluge`, `l10n`, `gui/`, `hid/`.
3. No mutable globals; no heap in any widget; widgets are not templates (only `region`
   bodies are, plus the two named legacy-bridge exceptions in `keyboard.h` below).
4. Every widget takes its state by reference from the caller and reads input only from the
   frame.

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

Three ratchet rows cover `src/ui` (baseline `scripts/layer_rules_baseline.txt`, all three
currently at count 0 — any occurrence is a regression):

| rule | applies to | forbids |
|---|---|---|
| `ui-includes-outside` | `src/ui/**` | any `#include` outside `ui/`/`gfx/` whose path contains a slash, **plus** the root headers the plan's Global Constraints name (`definitions*`, `board_config.h`, `board_layout.hpp`), which are slash-free at their include sites (a std header is neither, so it still never matches) |
| `ui-oled-no-grid` | `src/ui/oled/**` | `#include "ui/grid/..."` |
| `ui-grid-no-oled` | `src/ui/grid/**` | `#include "ui/oled/..."` |

Probe (also exercised by `ctest -R layer_rules` and the pre-commit hook, whose `files:`
pattern covers `src/ui/`): add `#include "ui/grid/frame.h"` to `src/ui/oled/text.h` →
`scripts/check_layer_rules.py` exits 1 naming `ui-oled-no-grid`; remove it → exits 0. The
named-root-header arm has its own probe: add `#include "definitions_cxx.hpp"` to any header
under `src/ui/` → exits 1 naming `ui-includes-outside`; remove it → exits 0.

## Core types (`src/ui/core`)

| type | file |
|---|---|
| `EncoderRole`, `PadEvent`, `PadMask`, `InputSnapshot` | `core/input.h` |
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
| `OledFrame` (`pixel`, `hline`, `vline`, `line`, `rect`, `rect_rounded`, `invert`, `invert_rounded`, `clear`, `glyphs`, `glyph`, `graphic`, `icon`, `icon_centered`, `region`, `bounds`), `root_frame` | `oled/frame.h` |
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
| `GridFrame` (`pad`, `peek`, `local_events`, `first_press_in`, `region`, `bounds`), `root_grid_frame` | `grid/frame.h` |
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

## Parity

Every OLED widget pins legacy geometry in `tests/spec/ui` at both 128×48 and 128×64 (paired
`*_48.pbm` / `*_64.pbm` goldens under `tests/golden/ui/widgets/`, e.g. `envelope_48.pbm` /
`envelope_64.pbm`); grid components pin legacy colours via `.state` goldens (e.g. `macros.state`,
`vu_meter.state`, `greyout.state`) instead, since a pad surface has one geometry and the thing
worth freezing is the RGB it computes.

## Known follow-ups

- **gfx clip rect for `glyphs`/`graphic`/`icon`/`invert_rounded`.** `OledFrame`'s clip is exact
  for the outline/fill primitives (`rect`, `rect_rounded`, `pixel`/`hline`/`vline`/`line`,
  `invert`, `clear`), decomposed into clipped spans. `glyphs`/`glyph` only clip to the frame's
  right edge and to the canvas; `graphic`, `icon`, `icon_centered` and `invert_rounded` are
  all-or-nothing (skipped whole if any part would fall outside `clip`). Closing this needs a
  clip rectangle threaded through `Canvas::draw_char` / `draw_graphic_multi_line` in gfx — out
  of scope for this leaf toolkit, and a prerequisite for widgets that draw text or blit inside
  sub-regions (`list`, `columns`, `popup_frame`). See `oled/frame.h`.
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
