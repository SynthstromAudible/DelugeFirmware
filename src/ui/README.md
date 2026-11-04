# deluge_ui_core / deluge_ui_oled / deluge_ui_grid

The immediate-mode widget toolkit the UI stack draws with. A scene builds a frame for each surface
every tick and calls widgets on it; each widget paints, reads the input it was granted, and records
what it consumed, all in one call. State lives in plain structs the caller owns and passes in by
reference. The toolkit is a leaf: it knows nothing of the model, the legacy UI or the hardware, and
it reproduces legacy's pixels and pad colours exactly, so users cannot see the port.

| target | holds | links |
|---|---|---|
| `deluge_ui_core` (`core/`) | input snapshot, frame context and claims, LED frame, style, metrics, POD state, repaint gate | `deluge_gfx_oled` (types only; nothing under `core/` includes `gfx/grid`) |
| `deluge_ui_oled` (`oled/`, namespace `deluge::ui::oled`) | `OledFrame` and the OLED widgets | `deluge_ui_core`, `deluge_gfx_oled` |
| `deluge_ui_grid` (`grid/`, namespace `deluge::ui::grid`) | `GridFrame` and the pad components | `deluge_ui_core`, `deluge_gfx_grid` |

All three are `STATIC` libraries built as C++26.

## Rules

- `ui/oled` and `ui/grid` never include each other.
- Nothing here includes anything outside `ui/`, `gfx/` and the standard library: no `model`,
  `notify`, `libdeluge`, `l10n`, `gui/`, `hid/`, nor the root headers `definitions*`,
  `board_config.h`, `board_layout.hpp`.
- No mutable globals and no heap. Frames are per-tick stack aggregates that own nothing; widget
  state is POD the caller keeps between ticks.
- Widgets are plain functions, not templates. The only templates are `OledFrame::region`,
  `GridFrame::region`, and the two legacy-bridge adapters in `grid/keyboard.h`.
- A widget reads input only through its frame's accessors, never through the snapshot behind it
  (see "The input-consumption contract").

`scripts/check_layer_rules.py` enforces the include rules and the input rule (ctest `layer_rules`,
pre-commit hook `check-layer-rules`). Its four `src/ui` rows, all held at zero in
`scripts/layer_rules_baseline.txt`:

| rule | applies to | forbids |
|---|---|---|
| `ui-includes-outside` | `src/ui/**` | any slashed `#include` outside `ui/`/`gfx/`, and the root headers above |
| `ui-oled-no-grid` | `src/ui/oled/**` | `#include "ui/grid/..."` |
| `ui-grid-no-oled` | `src/ui/grid/**` | `#include "ui/oled/..."` |
| `ui-widgets-read-input-directly` | `src/ui/{oled,grid}/**` except `frame.{h,cpp}` | `input.turn`, `input.edit`, `input.horizontal`, `input.vertical`, `input.select_pressed`, `input.back_pressed`, `input.shift`, `input.now_ms` |

The frame files are exempt because they are the accessors. The rule is line-based: a ratchet
against drift, not a proof.

## Catalogue

### Core (`core/`)

| type / function | file |
|---|---|
| `EncoderRole`, `PadEvent`, `PadMask`, `InputSnapshot` | `input.h` |
| `InputClaims`, `InputMask`, `FrameContext`, `FrameInput` (the accessors and claims both frames inherit, and the input-consumption contract) | `frame_context.h` |
| `LedId`, `IndicatorId`, `BlinkRate`, `BlinkPhase`, `LedState`, `KnobIndicator`, `LedFrame` | `led_frame.h` |
| `knob_indicator_levels`, `knob_indicator_blink` (a gold knob's 4-segment column) | `knob_indicator.h` |
| `blink_lit`, `blink_next_flip_in` | `blink.h` |
| `EncoderAcceleration`, `kGoldEncoderAcceleration`, `kHorizontalMenuAcceleration` (one acceleration curve shared with legacy's encoders) | `encoder_acceleration.h` |
| text and layout constants (`kMenuBaseY48/64`, `kHorizontalMenuBaseY`, `kScrollTime`, ...), `text_height_of` | `metrics.h` |
| `RepaintGate` | `repaint_gate.h` |
| `Response` | `response.h` |
| `ListState`, `ColumnsState`, `ScrollText`, `Blink`, `Hold`, `Timer`, `WaveformState` | `state.h` |
| `Style` (every field a widget reads; `menu_base_y` is the panel-height value), `kDelugeStyle48`, `kDelugeStyle64` | `style.h` |
| `WrappedLines`, `wrap_lines` | `text_wrap.h` |

### OLED (`oled/`)

| widget / helper | file |
|---|---|
| `ClipScope`, `OledFrame`, `root_frame` | `frame.h` |
| `Align`, `TextStyle`, `text_height_for`, `spacing_x_for`, `measure`, `text`, `title`, `title_badge` | `text.h` |
| `scroll_text` | `scroll_text.h` |
| `TextEditState`, `text_edit` | `text_edit.h` |
| `ListRow`, `menu_rect`, `list_visible_rows`, `list_window_start`, `list`, `scrollbar` | `list.h` |
| `Range`, `ValueStyle`, `DecimalLayout`, `int_value`, `decimal_value`, `enum_value`, `toggle` | `values.h` |
| `FilterKind`, `StageKind`, `Hint`, `percent`, `knob_arc`, `unipolar_bar`, `pan`, `slider`, `length_slider`, `filter_curve`, `envelope_stage`, `sidechain_ducking`, `meter` | `meters.h` |
| `Column`, `Paging`, `paging_of`, `column_under_button`, `page_switch`, `columns`, `title_paged`, `MeterColumn`/`meter_column`, `TextColumn`/`text_column`, `submenu_column`, `ToggleColumn`/`toggle_column` | `columns.h` |
| `DrawColumn`, `FilterGraph`, `draw_column`, `filter_graph`, `envelope_graph`, `eq_graph` | `column_drawings.h` |
| `kMaxRoutingStages`, `routing_chain`, `segmented_switch` | `routing.h` |
| `PopupStyle`, `popup_rect_for`, `popup_frame`, `popup_text`, `working`, `progress` | `popup.h` |
| `OutputNameData`, `output_name`, `segment_digits`, `vu`, `graphic`, `icon` | `misc.h` |
| `kSamplePositionLineGap`, `sample_position` | `sample_position.h` |
| `Rainfall`, `draw_block`, `draw_rainfall` | `rainfall.h` |
| `Starfield`, `draw_starfield` | `starfield.h` |
| `detail::int_to_string` (internal number formatting) | `detail/int_to_string.h` |

### Grid (`grid/`)

| component / helper | file |
|---|---|
| `GridFrame` (`pad`, `peek`, `flash`, `local_events`, `pad_events`, `first_press_in`, `interact`, `held_pads`, `region`, `bounds`, `visible`, `grid_gate`), `FlashColour`, `PadFlash`, `PadFlashList`, `root_grid_frame` | `frame.h` |
| `NoteFill`, `NoteCell`, `ClipRowData`, `row`, `clip_row` | `rows.h` |
| `LaunchStyle`, `StatusColours`, `StatusPad`, `AuditionPad`, `status_colour`, `status_pad`, `audition_colour`, `audition_pad`, `sidebar_columns` | `sidebar.h` |
| `MacroKind`, `MacroCell`, `macro_colour`, `macro_wants_arm_flash`, `macros`; `NoteSquare`, `VelocityData`, `velocity_colour`, `velocity_column`; `AutomationData`, `automation_colour`, `automation_column`; `vu_max_row`, `vu_row_colour`, `vu_meter`; `TickColour`, `TickSquare`, `tick_colour`, `tick_squares` | `columns.h` |
| `PeakCol`, `waveform_col_colour`, `waveform_row`, `waveform_full` | `waveform.h` |
| `kMaxMarkers`, `MarkerStack`, `marker_tint`, `marker_column` | `markers.h` |
| `KeyboardLayoutKind`, `TextKeyboardState`, `text_keyboard_char_at`, `text_keyboard_key_colour`, `text_keyboard_is_shift_pad`, `text_keyboard` | `keyboard.h` |
| legacy bridges: `LegacyImage`, `copy_legacy_image`, `copy_legacy_column`, `keyboard<Layout>`, `control_column<Column>` | `keyboard.h` |
| `GreyoutMask`, `greyout`, `GestureEvent`, `HoldPolicy`, `gesture` | `effects.h` |

`row`, `clip_row`, `status_pad`, `audition_pad` and `text_keyboard` return a `Response`. The
per-column components (`macros`, `velocity_column`, `automation_column`, `tick_squares`,
`vu_meter`, `sidebar_columns`) return `void`: their interaction is *which row*, which one
`Response` cannot carry. The caller reads it back with `GridFrame::first_press_in({x, 0, 1, rows})`.

Grid compositing and the transition tweens (`composite`, `fade`, `scroll_*`, `smear_scroll_*`,
`zoom`, `explode`, `expand_collapse`) live one layer down in `gfx/grid/` and are driven by the
shell, not by widgets; see `src/gfx/README.md`.

### Legacy bridges (`grid/keyboard.h`)

`keyboard(GridFrame&, Layout&)` and `control_column(GridFrame&, int32_t, Column&, void*)` let a
scene drive the legacy `KeyboardLayout` and `ControlColumn` classes through a `GridFrame`. They are
templates so the caller supplies the concrete `gui/` type and `ui/grid` never includes `gui/`. Each
renders into a zeroed `RGB[8][18]` scratch image (`LegacyImage`) and copies it into the frame,
sidebar included. They go once the keyboard layouts paint through the frame directly.

## Frames and the tick

One `FrameContext` is built per tick per surface set, by the shell in the firmware and by the rigs
in `tests/unit/spec/ui/support/frames.h` in the specs. Every frame in the tree holds a reference to
it, so a claim made deep inside a nested region lands in the one place the shell reads back.

```cpp
FrameContext ctx{input, gate};                 // + claims{}, tick, tick_ms
ctx.begin_tick(now_ms);                        // exactly once per tick, before any frame
OledFrame f = root_frame(canvas, style, ctx);  // GridFrame: root_grid_frame(grid, ctx)

f.region(pane, [](OledFrame& p) { ... });                          // inherit every input
f.region(pane, false, [](OledFrame& p) { ... });                   // draw only
f.region(pane, InputMask{.turn = false}, [](OledFrame& p) { ... }); // all but the turn
```

- **`begin_tick()` runs exactly once per tick, before any frame is built.** It clears the claims
  and bumps `tick`/`tick_ms`. A context reused without it latches the first tick's claims and
  withholds those inputs from `Scene::handle()` for ever. The rigs expose the same call
  (`r.begin_tick(now_ms)`), which also moves the rig's clock.
- **The context outlives its frames; frames live one tick.** `OledFrame` and `GridFrame` are
  aggregates of references, built on the stack and owning nothing. Both inherit `FrameInput`
  publicly and stay aggregates, so their brace initialisation starts with `{ctx, mask}` for the
  base.
- **Stacked scenes.** `root_frame`/`root_grid_frame` take an `InputMask` (default `all()`) and a
  `covered` flag. When several scenes render onto one surface, the shell builds the frontmost,
  focused scene's root with `all()` and every scene beneath with `none()`: they still paint, but
  read 0/false from every masked accessor and claim nothing. A scene beneath the one that takes the
  input is also built `covered`, which every region inherits: a covered frame reads no pad events,
  no held pads, no shift and no held Select.
- **After rendering**, the shell reads `ctx.claims`. An unclaimed input may go to
  `Scene::handle()`; a claimed one has an owner and the shell leaves it alone.
  `InputClaims::any()` asks whether anything was consumed; `operator==` lets a spec pin a whole
  claim set.
- **Pad flashes are events, not pixels.** `GridFrame::flash()` appends to the shared
  `PadFlashList` (dropped when the frame has none); the control surface plays the flash itself and
  the shell sends the list after the pad columns and clears it.
- **Repaint scheduling.** A widget with motion asks for its next frame through `gate()`; a grid
  widget that should repaint only the grid uses `grid_gate()`, which is the surface's own gate when
  the shell supplied one. `RepaintGate::needs_frame()` is what the shell consults before rendering.

## The input-consumption contract

The authoritative text is on `FrameContext` in `core/frame_context.h`; these are its five rules.
`tests/unit/spec/ui/frame_context_spec.cpp` pins all of them.

1. **Widgets read input only through the frame.** `turn()`, `edit(0)`/`edit(1)`, `horizontal()`,
   `vertical()`, `select_pressed()`, `back_pressed()`, `shift()`, `select_held()`, `now_ms()`, and
   `held_pads()` on the grid. `gate()` reaches the repaint gate; `mask`/`focused()` say what was
   granted. The `ui-widgets-read-input-directly` row holds this at zero.
2. **Reading is claiming.** `turn()`, `edit(i)`, `horizontal()`, `vertical()`, `select_pressed()`
   and `back_pressed()` record the claim for their input in the same call that returns the value.
   A claim means *"this control owns that input this tick"*, so a focused `list` claims the turn on
   every frame, whether or not a detent arrived and whether or not it has rows. `claim_turn()`,
   `claim_edit(i)`, `claim_horizontal()`, `claim_vertical()`, `claim_select()` and `claim_back()`
   are for owning an input without reading it (`list` and `enum_value` claim before their
   empty-model early-outs). Claiming is idempotent per frame: reading `turn()` three times,
   or reading it and then calling `claim_turn()`, claims once.

   **A claim records ownership; it does not arbitrate.** An input reaches every frame whose mask
   grants it, so two sibling regions both granted the turn each move their `list`. Granting an
   input to at most one region is the parent's job, through `InputMask`.
   `InputClaims::double_claim` is set when a *second* frame claims an input already claimed this
   tick: the diagnostic for getting that wrong, never tripped by one frame reading twice.
3. **A frame sees exactly the inputs its parent granted.** Focus is an `InputMask` of
   `{turn, edit, horizontal, vertical, select, back}`; both gold knobs share `edit` because they
   are one control, though each has its own claim flag. `region(rect, m, body)` gives the child
   `mask & m`, so focus only narrows. A masked-off input reads 0/false and its `claim_*()` is a
   no-op, so a widget drawn without it paints identically but cannot act. `region(rect, false)` is
   how a composite draws a sub-widget for its looks (`enum_value` and `toggle` draw a `list` for
   its window and highlight). A partial mask is how a container keeps one input and hands on the
   rest: `columns` grants the focused column `InputMask{.turn = false}` and the others `none()`,
   because the scene calling `columns` reads and claims the turn itself to edit the focused value,
   so one detent edits it once. The gold knobs, the press and Back reach the widget in the focused
   column. `columns` does not own the turn (focus moves by button) and claims the press only for
   a submenu column.

   **`now_ms()`, `shift()` and `select_held()` are not masked.** The clock is the tick's own time:
   an unfocused region still scrolls labels, times gestures and arms the gate with a future
   deadline. Shift and held Select are modifiers read alongside a gesture, and focus arbitrates
   the gesture. (A covered frame still reads shift and held Select as false.)
4. **Pad input is positional and never masked.** `pad()`, `peek()`, `local_events()`,
   `first_press_in()`, `interact()` and `held_pads()` report presses in an unfocused region exactly
   as in a focused one: a press names its target by where it landed. Only `covered` hides them.
5. **`select_pressed`/`back_pressed` are edges** the caller supplies once per press
   (`core/input.h`). A claim is not a latch: it lasts the tick and `begin_tick()` clears it.

## Geometry and clipping

- **`bounds()` vs `visible()`.** `bounds()` is the rect the region asked for, `{0, 0, w, h}` in its
  own coordinates; `visible()` is the part the parent granted, in the same coordinates (empty when
  the region lies wholly outside). Layout measures against `bounds()`, so local `(0, 0)` is the
  region's own corner even when it overhangs; drawing and pad hit-testing crop to `visible()`. The
  grid `waveform_*` components mirror about the requested width.
- **OLED clipping is complete.** Every `OledFrame` drawing method opens a `ClipScope` over its
  region, and `gfx::oled::Canvas` honours its clip in every primitive, glyphs and bitmaps included,
  so nothing a widget draws escapes its region. A `ClipScope` intersects with the canvas's existing
  clip and restores it on exit, so it only narrows: a clip set from outside the frame tree (a
  damage rectangle) still bounds every widget.
- **OLED coordinates hide the bezel.** `root_frame` puts local `(0, 0)` at
  `(0, visible_top)`, so a widget never learns the faceplate hides the top rows.
- **The grid root spans the sidebar.** Columns `0..15` are the main pads and `16..17` the sidebar,
  in one coordinate space; a sidebar component opens a `region` over `{16, 0, 2, rows}`.
- **`list` sizes from this frame's rows.** It counts the span it was handed, clamps the cursor,
  then walks the turn, before drawing, so a list that shrinks or grows under the cursor is right on
  the same frame. `ListState::rows_last` is published only for callers that window against it.

## Parity

Every OLED widget pins legacy geometry at both 128x48 and 128x64 (paired `*_48.pbm`/`*_64.pbm`
goldens under `tests/golden/ui/widgets/`). Grid components pin legacy colours with `.state`
goldens (`macros.state`, `vu_meter.state`, `greyout.state`). `measure()` and every centring or
right-aligning widget use legacy's nominal advance widths and boxes, blank padding included, not
ink bounds, because that is what legacy laid out against. `Style::menu_base_y` is
`MenuItem::drawItemsForOled`'s 14/15 per panel height; `kHorizontalMenuBaseY` stays a separate
constant (see quirk 3).

## Legacy quirks reproduced on purpose

Behaviour that a plausible fix would change, kept because parity with the shipped firmware comes
first. Each is pinned by a spec or golden that must change with the fix, so an accidental fix fails
CI.

| # | quirk | where | pinned by | fix |
|---|---|---|---|---|
| 1 | Popups centre on the full 48-row panel, bezel-hidden rows included, so they sit visibly above the true visible centre (and would centre correctly, so inconsistently, on a 64-row panel) | `popup_rect_for` (`panel_of`) in `oled/popup.cpp` | `popup_spec.cpp` "centres the popup box on the panel, at both geometries"; `popup_*_48.pbm` | a `Style` flag (default on for parity) choosing `f.bounds()` instead |
| 2 | `decimal_value`'s default layout draws at canvas-absolute rows, ignoring `rect.y`/`rect.h`, so it cannot be placed in its frame and sits differently from `int_value` on a 64-row panel | `decimal_value` in `oled/values.h` | `values_spec.cpp` "draws the legacy decimal at its canvas-absolute rows on both panels"; `decimal_value_12_34_48.pbm` | offset from `rect.y`, as `int_value` does |
| 3 | `kHorizontalMenuBaseY` is `14` on every panel while the vertical menu's is 14/15, from a 64-row arm of `HorizontalMenu::renderMenuItems` that is dead in legacy | `kHorizontalMenuBaseY` in `core/metrics.h`; `columns` in `oled/columns.cpp` | `columns_spec.cpp` "puts the label row at the legacy base_y + box_height - label_height"; `columns_four_64.pbm` | a 64-row arm of `15`, keyed off the canvas height |
| 4 | `GreyoutMask::cols` bits run right to left (bit 0 is the rightmost column, bit 17 is column 0), legacy `PadLEDs::prepareColour`'s numbering | `GreyoutMask` in `grid/effects.h` | `effects_spec.cpp` "greys the column a cols bit selects, counting from the right", "greys column 0 from the top cols bit" | natural order, mirrored once in the bridge that builds the mask from `greyoutCols` |
| 5 | `wrap_lines`' `max_chars_per_line` is read by nothing, as in legacy's `breakStringIntoLines`; only `max_width_px` caps a line | `wrap_lines` in `core/text_wrap.h` | `popup_spec.cpp` "ignores max_chars_per_line, because legacy never reads the field" | drop the parameter |
| 6 | `length_slider`'s `min_pos` defaults to `true`, because legacy's `menu_item::Number::drawLengthSlider` defaults `bool min_slider_pos = 3` (truthy); every new call site inherits it | `length_slider` in `oled/meters.h` | `meters_spec.cpp` "puts the thumb one pixel right when min_pos, and defaults it on"; `meter_length_slider_48/64.pbm` | default `false`, re-record the two goldens |

Five more are in the tweens `gfx/grid/tween.cpp` ports from `hid/led/pad_leds.cpp` (detail in
`src/gfx/README.md`). Each is pinned by `tests/unit/spec/gfx/tween_spec.cpp`'s `legacy::` oracle,
which byte-compares, so a fix changes the port and the oracle together.

| # | quirk | legacy | fix |
|---|---|---|---|
| 7 | Complementary blend weights are `65535 - frac`, not `65536 - frac` (`explode`'s x/y intensity pairs, `expand_collapse`'s row weight), so a pad exactly on a boundary renders at 65535/65536 | `renderExplodeAnimation`, `renderInstrumentClipCollapseAnimation` | `65536 - frac` |
| 8 | `expand_collapse` narrows the row position to `int8_t`; unreachable past ±127 because `ExpandCollapseParams` requires `row_to < rows` | `renderInstrumentClipCollapseAnimation` (`newRowPosition1Array`) | widen to `int16_t` |
| 9 | `explode` relies on its column range excluding every off-screen destination rather than writing every column's intensity pair (the port's scratch is zero-initialised, so this is a logic dependence, not uninitialised memory) | `renderExplodeAnimation` | write every column unconditionally |
| 10 | `zoom` shifts each operand down 8 bits before multiplying the inner-image pin, losing up to ~1/256 pad of origin precision | `renderZoomWithProgress` | multiply first, shift once |
| 11 | `zoom` treats a black source pad as "nothing drawn" and assigns black, rather than keeping what the destination held, which is why its output is a full frame, never an overlay | `renderZoomWithProgress`, `renderZoomedSquare` | track "drawn" separately from colour |

## How to add

- **An OLED widget:** a free function in `oled/` taking `OledFrame&`, a `Rect`, the value or data
  by reference, and any POD state (add it to `core/state.h` if it is shared). Read input only
  through the frame's accessors, claim what you own even on an empty model, schedule motion through
  `f.gate()`, and lay out against legacy's nominal geometry. Add the `.cpp` to `deluge_ui_oled` in
  `CMakeLists.txt`, a row to the catalogue above, and a spec in `tests/unit/spec/ui/` built on
  `OledRig` that pins paired 48/64 goldens with `expect_golden`.
- **A grid component:** a free function in `grid/` taking `GridFrame&`; paint and hit-test in one
  pass with `pad()`, and return a `Response` only if one response describes the interaction
  (otherwise `void`, read back with `first_press_in`). Spec it on `GridRig` with a `.state` golden.
- **A sub-region with partial focus:** `f.region(rect, InputMask{...}, body)`; grant each input to
  at most one child.
- **A legacy quirk you keep:** pin it with a spec or golden and add a row to the table above.

## Not yet ported

- The OLED `waveform` widget: it needs the model-side `WaveformRenderData`, so it waits for a
  sample-view scene. `WaveformState` in `core/state.h` is reserved for it.
- Native keyboard layouts and control columns: the legacy `KeyboardLayout`/`ControlColumn`
  classes still render through the `grid/keyboard.h` bridges.
