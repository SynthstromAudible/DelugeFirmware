# Audit of plans A, B, C (gfx, toolkit, notify) — 2026-08-27

Four independent reviews (deluge-sdk comparison, spark comparison, architecture, C++ idiom), run on ui/toolkit tip a8f12aa06. Synthesis follows; the four full reports are appended.


---

# UI foundation audit: `DelugeFirmware@ui/toolkit` vs `deluge-sdk@8e497f4`

Read-only comparison of the new C++ UI libraries (`src/gfx`, `src/ui`, `src/notify`) against the
earlier Rust work in `deluge-sdk` (`crates/deluge-ui-toolkit`, `crates/deluge-grid-toolkit`,
`crates/deluge-fonts`, `crates/deluge-image`, `crates/deluge-linux-ui`, `crates/deluge-protocol`,
`crates/deluge-sim-link`).

Paths beginning `crates/` are in `/home/kate/GitHub/deluge-sdk`; everything else is in
`/home/kate/GitHub/DelugeFirmware-clean`.

**Scope note.** `deluge-image` turns out to be irrelevant to this audit: it is firmware-image
format logic (ELF `PT_LOAD` classification, CRC-32, on-flash settings), not UI
(`crates/deluge-image/README.md:1-18`). It is not discussed further.

---

## Ratings

| # | Area | Rating | One-line reason |
|---|---|---|---|
| 1a | Canvas model / primitives | **better** | Page-major, geometry-as-a-value, zero-heap, legacy-exact; SDK leans on `embedded-graphics` and allocates a `Vec<Pixel>` per icon blit (`crates/deluge-ui-toolkit/src/graphics.rs:28-43`). |
| 1b | Clipping model | **worse** | Our clip is exact for outlines/fills but all-or-nothing for `graphic`/`icon`/`invert_rounded` and leaky for `glyphs` (`src/ui/oled/frame.h:22-28`); the SDK gets uniform clipping free from `DrawTarget::clipped()`. |
| 1c | Fonts / metrics / text wrap | **better** | We carry `y_adjust`, `monospaced`, a `render_height` sentinel and hoisted glyph metrics (`src/gfx/oled/font.h:20-60`) plus a full `breakStringIntoLines` port (`src/ui/core/text_wrap.h:29-63`); the SDK has no wrapper and no shrink-to-fit. |
| 1d | Icons | **equivalent** | Same 40-odd legacy icons either way; ours hand-transcribed (`src/gfx/oled/icons.cpp`, 864 lines), theirs generated from BMP at build time (`crates/deluge-ui-toolkit/build.rs:10-68`) — theirs is the cheaper pipeline, ours the surer parity. |
| 2a | Frames / regions / state ownership | **equivalent** | Same design, independently converged; ours adds the unclipped `size` so a left/top overhang doesn't shift the origin (`src/ui/oled/frame.h:42-49`). |
| 2b | Repaint gate | **worse** | Theirs is *structural* — a clean frame cannot be painted because the closure is never called (`crates/deluge-grid-toolkit/src/imode.rs:478-512`); ours is advisory and a shell that forgets to ask repaints silently (`src/ui/core/repaint_gate.h:12`). |
| 2c | Input snapshot | **better** | Velocity, a held-pad level mask, two edit encoders, shift/back edges (`src/ui/core/input.h:31-73`) vs one `MenuInput` variant per frame (`crates/deluge-ui-toolkit/src/menu.rs:58-72`). |
| 2d | Response type | **equivalent** | Ours is theirs plus grid fields and a disambiguated `index` (`src/ui/core/response.h:11-30` vs `crates/deluge-ui-toolkit/src/menu.rs:76-83`). |
| 2e | Style / theming | **better** | Every field read by a widget and `static_assert`-pinned to the legacy metric it reproduces (`src/ui/core/style.h:21-78`); `MenuStyle` carries four fields (`crates/deluge-ui-toolkit/src/menu.rs:242-256`). |
| 2f | OLED widget catalogue | **better** | Broader (popup/working/progress, `output_name`, `segment_digits`, paged columns, enum/decimal/toggle) and pixel-pinned; but the SDK's menu *walker* has no counterpart (see 2g). |
| 2g | Menu/navigation composition | **worse** | The SDK composes a whole drilldown menu with a two-pass count/draw walker (`crates/deluge-ui-toolkit/src/menu.rs:346-415`); we stop at a `list()` widget whose cursor walk is bounded by the *previous* frame's row count (`src/ui/oled/list.h:96-99`). |
| 3a | Pad-grid model / dirty rows | **better** | Runtime dimensions + per-row dirty mask matching `renderMainPads(whichRows)` (`src/gfx/grid/pad_grid.h:21-66`); the SDK blits all 144 pads every frame (`crates/deluge-grid-toolkit/src/grid.rs:139-146`). |
| 3b | Tweens / animations | **worse** | Four pure functions (`src/gfx/grid/tween.h:9-27`) against six, including the smear/zoom/explode/expand-collapse ports we deferred (`crates/deluge-grid-toolkit/src/animations/mod.rs:10-15`). |
| 3c | Colour maths | **equivalent** | Both are the same `RGB` port; ours fixed-point and `constexpr`, theirs adds HSV round-trip and SIMD `lerp_slice` (`crates/deluge-grid-toolkit/src/color.rs:215,252,489`). |
| 3d | Compositing / overlays | **worse** | No transparency layer: `greyout` mutates in place and is documented non-idempotent (`src/ui/grid/effects.h:50-57`); the SDK has `Option<Color>` cells with Replace/Overlay/Tint (`crates/deluge-grid-toolkit/src/layer.rs:17-112`). |
| 3e | Hit-testing / regions | **equivalent** | Same paint-and-hit-test-in-one-call primitive; ours adds `peek`, `first_press_in` and a snapshot-keyed event cache (`src/ui/grid/frame.h:93-158`). |
| 3f | Gestures | **better** | A general tap/hold/second-pad FSM with every legacy transition cited (`src/ui/grid/effects.h:89-146`); theirs is baked into one widget (`crates/deluge-grid-toolkit/src/widgets/clip_grid.rs:145-215`). |
| 4a | Unit / golden coverage | **better** | 7,014 lines of spec over 20 files, 75 goldens, and a second `reference` canvas so a port is diffed against the *original* rather than its own last run (`tests/spec/ui/support/frames.h:57-83`); the SDK has 141 inline unit tests and no UI pixel goldens. |
| 4b | Simulator / end-to-end harness | **worse** | The SDK ships a headless script/snapshot driver with a CI recipe (`docs/simulator.md:182-278`); ours is spec'd but unbuilt (`docs/superpowers/specs/2026-08-26-ui-stack-redesign-design.md:494`). |
| 5 | Notifications (`src/notify`) | **better (no counterpart)** | `DirtySet`/`NoticeQueue` have nothing to compare against — the SDK has no model to decouple a UI from. |

---

## 1. Drawing primitives, canvas model, fonts, icons

### 1.1 Canvas / surface model

Ours (`src/gfx/oled/canvas.h:51-296`) is a non-owning 1-bpp **page-major** surface over
caller storage, with the panel size arriving as a value (`Geometry`,
`src/gfx/oled/geometry.h:10-19`), so `bytes()[page * width + x]` bit `b` is row `page*8+b`.
`OwnedCanvas<G>` (`canvas.h:304-313`) adds statically-sized storage aligned to 32 bytes for DMA
(`canvas.h:298-301`). Nothing in the layer knows the board.

The SDK has no canvas of its own. Everything draws onto an `embedded_graphics::DrawTarget<Color
= BinaryColor>` (`crates/deluge-ui-toolkit/src/lib.rs:19-25`), with the concrete buffer supplied
by the host — `deluge-linux-ui`'s `OledTarget` is a **row-major** 43×16-byte fb, MSB-first
(`crates/deluge-linux-ui/src/oled.rs:4-9`), while `deluge-sim-link` carries the *page-major*
768-byte form (`crates/deluge-sim-link/src/lib.rs:27-28`). Note the SDK carries both layouts and
has a standing "bit order is a working assumption, unverified against the kernel driver"
caveat (`crates/deluge-linux-ui/src/oled.rs:10-16`); our page-major choice is the one that
matches the real panel and the wire protocol.

**What the SDK does that we don't:** the `DrawTarget` indirection buys `translated()`,
`clipped()` and `cropped()` adapters for free — the toolkit is genuinely display-agnostic and
the faceplate offset is a caller concern (`crates/deluge-ui-toolkit/src/lib.rs:116-123`,
`MenuStyle::top_inset`). We solve the same problem by putting `visible_top` in `Geometry` and
having `root_frame` set the origin (`src/ui/oled/frame.h:137-139`), which is tidier for us but
non-portable.

**What we do that they don't:** shrink-to-fit
(`Canvas::draw_string_centred_shrink_if_necessary`, `canvas.h:193-194`), right-alignment
(`:204-205`), a scroll offset threaded through `draw_string` (`:163-164`), `kEdge`/`kCentre`
sentinels deliberately chosen as `INT32_MIN`/`INT32_MIN+1` so a computed `-1` can't be
misread (`geometry.h:21-29`), rounded rects with a `BorderRadius` and a caller-policy
`rounded` flag (`:142-143`), and `invert_area_rounded` (`:281-282`). None of these exist on
the SDK side.

### 1.2 Clipping — our one clear regression

`OledFrame` carries an absolute clip rect and an origin (`src/ui/oled/frame.h:33-40`). Clipping
is **exact** for `pixel`/`hline`/`vline`/`line`/`invert`/`clear`/`rect`/`rect_rounded` (each
decomposed into clipped spans, `frame.h:74-85`), but `graphic`, `icon`, `icon_centered` and
`invert_rounded` are **all-or-nothing**, and `glyphs` only clips to the frame's right edge and
to the canvas. `src/ui/README.md:123-130` records this as the top follow-up and correctly names
it a prerequisite for text-in-subregion widgets (`list`, `columns`, `popup_frame`).

The SDK never had this problem: `DrawTarget::clipped(&Rectangle)` clips *every* primitive
uniformly, glyphs and bitmaps included, because clipping happens at the pixel-iterator level
rather than per-primitive. This is the strongest structural argument in the SDK's favour
anywhere in this audit. The fix on our side is the one already identified — a clip rectangle
threaded through `Canvas::draw_char` and `Canvas::draw_graphic_multi_line` — and it is worth
doing before more widgets are written against the leaky contract.

### 1.3 Fonts and metrics

Ours: five faces behind `FontId` (`src/gfx/oled/font.h:18`), with a `Font` record carrying
`native_height`, `render_height` (with a documented sentinel-0 arm for unlisted heights),
`y_adjust` and `monospaced` (`font.h:20-34`). Selection is by *legacy text height*
(`font_for_text_height`, `:37`) — the same table `Canvas::drawChar` used — and glyph metrics
were lifted out of `Canvas` so measuring needs no framebuffer (`font.h:43-61`). `text_height_of`
(`src/ui/core/metrics.h:76-90`) documents the two non-obvious mappings: `MetricBold9` is
selected by height **10**, and `Apple7` means legacy height **9**, not 7, because `kApple7Up`
carries `y_adjust = 1` and height 9 is the dominant call shape (126 sites vs 29).

Theirs: the same five faces (`crates/deluge-fonts/README.md:7-13`), with `GlyphDescriptor {
w_px, glyph_index }` (`crates/deluge-fonts/src/lib.rs:64-69`), `height()`/`baseline()`
accessors, and accurate `text_width_with_spacing` (`:288`). `VariFont` wraps a face with a
character spacing and baseline (`crates/deluge-ui-toolkit/src/text/vari_font.rs:26-35`), and
`VariTextStyle` implements `embedded_graphics::text::renderer::TextRenderer`
(`crates/deluge-ui-toolkit/src/text/vari_text_style.rs:194-252`) so Deluge variable-width fonts
drop into e-g's `Text` layout engine — measure, draw, whitespace, line height.

- **Theirs and not ours:** the `TextRenderer` integration (irrelevant to us — we have no e-g);
  a `♭` glyph at descriptor index 95 (`vari_font.rs:83-84`), which we fold away in
  `glyph_index` (`src/gfx/oled/font.h:39-41`); and a **seven-segment emulation module**
  (`crates/deluge-fonts/src/seven_segment.rs:1-30`, with a documented segment bit layout and
  decimal-point handling that doesn't consume a digit position). We have `segment_digits`
  (`src/ui/oled/misc.h:79`), so this is covered, but their bit-layout doc comment is better
  than ours.
- **Ours and not theirs:** `char_spacing_px(c, height, is_last_char)` — the "no gap after the
  last glyph" rule (`font.h:54-57`) — which is exactly what `wrap_lines` needs and which their
  `text_width_with_spacing` does not model.

### 1.4 Text wrapping

We have it, they don't. `wrap_lines` (`src/ui/core/text_wrap.h:29-63`) is a full port of
`breakStringIntoLines` + `findLineSplitPoint` + `addLine` + `addCharacterToLine`, returning
`std::string_view`s into the caller's string with per-line pixel widths and a `longest`
(`text_wrap.h:22-27`) — no copies, no allocation. Every legacy rule is documented, including the
ones that look like bugs: a trailing `' '`/`'_'` joins the line it ended, `kWrapMaxLines`
overflow is **silent truncation**, and `max_chars_per_line` is read by nothing yet retained so
that honouring it later is a visible change (`text_wrap.h:51-58`). This has no SDK counterpart
at all; the SDK's menu truncates into a `heapless::String<24>` (`crates/deluge-ui-toolkit/src/menu.rs:185-195`).

### 1.5 Icons

Same content, different pipeline. Ours: `Icon { data, width, height, num_bytes_tall }`
(`src/gfx/oled/icon.h:6-11`), column-major 1-bpp, with ~40 externs
(`src/gfx/oled/icons.h:5-47`) backed by 864 lines of transcribed bitmap data.

Theirs: `IconData::from_bmp` parses a BMP header **at compile time** for width/height
(`crates/deluge-ui-toolkit/src/icons.rs:30-52`) and `build.rs` scans `assets/icons/*.bmp` and
emits one `pub const` per file (`crates/deluge-ui-toolkit/build.rs:36-57`), with the BMPs
themselves generated by `util/extract_deluge_icons.py` from `oled_icons.cpp`
(`crates/deluge-ui-toolkit/src/icons.rs:1-4`). Their runtime cost is worse — `get_bmp_pixel`
(`icons.rs:56-82`) does per-pixel header arithmetic and `draw_icon_data` collects a
`Vec<Pixel>` per blit (`crates/deluge-ui-toolkit/src/graphics.rs:28-43`) — but the *authoring*
story is much better: no hand-transcription, and a test that asserts every icon's dimensions
(`icons.rs:90-134`).

---

## 2. Immediate-mode toolkit design

Both sides landed on the same core shape, and the resemblance is not a coincidence: our
`Response { changed, focused, entered }` (`src/ui/core/response.h:11-13`) is field-for-field the
SDK's `menu::Response` (`crates/deluge-ui-toolkit/src/menu.rs:76-83`), and our grid frame's
"painting and hit-testing are one call" doctrine (`src/ui/grid/frame.h:29-33`) restates
`imode.rs:3-5`. The differences are all downstream of that shared premise.

### 2.1 Frames

| | ours | SDK |
|---|---|---|
| OLED frame | `OledFrame` — canvas + clip + origin + **unclipped size** + style + input + gate (`src/ui/oled/frame.h:33-40`) | none; widgets take `&mut D: DrawTarget` and a `Point` |
| grid frame | `GridFrame` — grid + clip + input + gate + a 128-byte event cache (`src/ui/grid/frame.h:64-158`) | `Frame` — grid + input + `now_ms` + repaint + clip (`crates/deluge-grid-toolkit/src/imode.rs:234-240`) |
| sub-regions | `region(Rect, F&&)` on both frames, templated | `Frame::region` (`imode.rs:354-364`) |
| layout splits | **OLED only** (`src/gfx/oled/geometry.h:50-51`) | `split_rows`/`split_cols` on the *grid* frame (`imode.rs:367-398`) |
| local events | `local_events()` — filtered into a member buffer, cached on the snapshot's span (`src/ui/grid/frame.h:102-116`) | `events()` — a lazy `filter_map` iterator, zero storage (`imode.rs:254-265`) |

Two things stand out.

**`GridFrame` is heavy.** The header is candid about it: 176 bytes, of which 128 are the
`local_events` scratch, zero-initialised for every nested `region()` child, and "spec §8's 'a
frame is ~40 bytes' does not describe this one" (`src/ui/grid/frame.h:17-25`). The SDK gets the
same functionality for free because `events()` returns a lazy iterator. In C++ the equivalent
is a callback — `template <class F> void for_each_local_event(F&&) const` — which would cost
nothing per frame and make `first_press_in` (`frame.h:126`) a two-line wrapper. The span-returning
API can stay for the callers that want it, filling a caller-supplied buffer.

**Grid `Rect` has no split helpers.** `src/gfx/grid/rect.h:7-21` has `contains` and `intersect`
but no `split_rows`/`split_cols`, even though the OLED `Rect` has both
(`src/gfx/oled/geometry.h:50-51`) and the SDK put them on the grid side specifically
(`imode.rs:367-398`, with a test at `:578-603` proving a press in the bottom pane doesn't leak
into the top one). Every grid scene that wants "sidebar in the last two columns, rows above"
currently spells the arithmetic by hand. This is a five-line fix.

### 2.2 Repaint gate — the SDK's is structural, ours is advisory

`GridUi::run(now_ms, input, ui)` computes `needs = first || repaint.now || timed_due ||
!input.is_idle()`, and on a clean frame **returns `FrameOutput::Skipped` without ever invoking
the closure** (`crates/deluge-grid-toolkit/src/imode.rs:482-512`). The gate cannot be bypassed,
because the gate owns the framebuffer and the call. Five tests pin the behaviour
(`imode.rs:519-575`), including that a continuous `request_repaint` is self-limiting
(`:544-554`) and that a timed request fires exactly once and clears (`:566-575`).

Ours is a bare predicate: `RepaintGate::needs_frame(now_ms, had_input, model_dirty)`
(`src/ui/core/repaint_gate.h:12`, implementation `repaint_gate.cpp:12-23`). The logic is
identical — including the same wrap-safe `static_cast<int32_t>(now - deadline) >= 0` deadline
comparison (`repaint_gate.cpp:13`), which is *better* than the SDK's plain `now_ms >= t`
(`imode.rs:488`) on a millisecond counter that wraps. But nothing structurally prevents a shell
from painting anyway. Since the shell doesn't exist yet (spec D), now is the moment to decide.

Two smaller pieces of the SDK's gate are worth copying regardless:

- `PadInput::is_idle` documents that "a steadily held pad is not new activity; widgets that
  animate while held should call `request_repaint`" (`imode.rs:115-120`). Our `had_input`
  parameter leaves that judgement to an unwritten shell with no note anywhere.
- `MenuState::needs_redraw()` (`crates/deluge-ui-toolkit/src/menu.rs:139-149`) — "this frame
  changed *which screen* renders next (entered a submenu or opened an editor); rebuild and flush
  one more frame before blocking for input". We have `request_repaint`, which covers it
  mechanically, but no widget currently calls it on a screen transition because no widget
  changes screens.

### 2.3 State ownership, input, response

**State ownership is identical and both sides are right about it.** Ours: POD structs the scene
owns and passes by reference (`src/ui/core/state.h:6-77`). Theirs: `MenuState` is "retained
navigation state, **owned by the app**" (`menu.rs:85-87`), `ClipGridState` is "caller-owned
interaction state" (`crates/deluge-grid-toolkit/src/widgets/clip_grid.rs:79-89`).

Our state structs are better documented and each deviation from spec is justified in place:
`ScrollText`'s `text_w_last`/`rect_w_last` change-detection keys (`state.h:23-31`), `Blink`'s
`until_ms` as a *phase anchor* rather than a deadline (`state.h:35-42`), `Hold::hold_reported`
as the one-shot latch that legacy got from a `uiTimerManager` timer (`state.h:48-60`). Three
fields are honestly labelled as unread placeholders with a named future caller
(`ListState::editing` `:9-13`, `Timer` `:62-69`, `WaveformState` `:70-77`) — a discipline the
SDK does not practise.

**Input is where we are clearly ahead.** `InputSnapshot` (`src/ui/core/input.h:50-73`) carries
`turn`, `edit[2]`, `horizontal`, `vertical`, edge-triggered `select_pressed`/`back_pressed`, a
level `shift`, a span of `PadEvent`s **with velocity**, a `PadMask held_pads` level, and
`now_ms`. The edges-vs-levels contract is spelled out with the failure mode it prevents (a
level `select_pressed` would enter a submenu once per tick) and pinned by a test that asserts
the *un-defended* behaviour (`input.h:52-68`). The SDK's `MenuInput` is a single enum variant
per frame — `Turn(i32)`, `Edit(i32)`, `Press`, `Back` (`menu.rs:58-72`) — with no velocity, no
shift, and no held level; its `PadInput` (`imode.rs:89-121`) has events and a held mask but no
velocity (`PadEvent { pad, pressed }`, `imode.rs:48-51`).

**Response.** Ours merges the OLED trio with grid flags plus `velocity` and a signed `index`
(`src/ui/core/response.h:11-30`). The `index` field's history is instructive and is recorded in
the spec amendments: `velocity` had been carrying three incompatible meanings at once (a real
pad velocity, a pressed column encoded as `x + 1`, and a typed character), which `index`'s
`-1` sentinel resolves. The SDK keeps two separate `Response` types — `menu::Response` for
OLED (`menu.rs:76-83`) and `imode::Response { pressed: Option<Pad>, released: Option<Pad>, held
}` for grid (`imode.rs:191-211`). Theirs carries *which pad* natively; ours needs
`first_press_in` for that (see §3.4).

### 2.4 Style / theming

`Style` (`src/ui/core/style.h:21-44`) is a per-board-profile value with two shipped instances
(`kDelugeStyle48`, `kDelugeStyle64`). Every field is read by at least one widget — a stated rule
so a profile can't set a number that does nothing (`style.h:11-12`) — and where a field's value
coincides with a legacy metric, a `static_assert` ties the two so a profile edit that would move
pixels fails to compile (`style.h:75-78`). `menu_base_y` was folded in specifically because
`list`/`output_name` had been branching on `Canvas::geometry().height`, which allowed a
mismatched `(Style, Geometry)` pair to take the 64-row branch with the 48-row style
(`src/ui/README.md:137-142`).

`MenuStyle` (`crates/deluge-ui-toolkit/src/menu.rs:242-256`) has four fields — two fonts,
`top_inset`, `max_visible` — with the rest as file-scope constants (`menu.rs:47-53`). Our
approach is strictly better for a multi-panel future.

### 2.5 Widget catalogue

**OLED, ours** (`src/ui/README.md:63-76`): `text`/`title`/`measure`, `scroll_text`, `list` +
`scrollbar`, `int_value`/`decimal_value`/`enum_value`/`toggle`, `percent`/`knob_arc`/
`unipolar_bar`/`pan`/`slider`/`length_slider`/`filter_curve`/`envelope_stage`/
`sidechain_ducking`/`meter`, `columns`/`title_paged`/`meter_column`/`text_column`/
`submenu_column`/`toggle_column`, `popup_frame`/`popup_text`/`working`/`progress`,
`output_name`/`segment_digits`/`vu`/`envelope`/`graphic`/`icon`.

**OLED, theirs**: `Menu` (vertical drilldown walker, `menu.rs`), `HMenu` (horizontal param
columns, `hmenu.rs`), `ListMenuView` (scrollable list with label-scroll animation,
`components/list_menu_view.rs:72-181`), `Scrollbar` (`components/scrollbar.rs:23-40`),
`Header`/`Title`, `Envelope`/`ADSR` (`components/envelope.rs:19-68`), `Waveform`,
`SlicedWaveform`, `LoopedWaveform`, `BasicEditor`/`FloatEditor`/`TextValueEditor`/
`UnipolarValueEditor`/`BipolarValueEditor` (full-screen value editors), the `params::*`
column widgets, and `DottedLine`/`FilledPolygon`/`Icon` primitives.

The `params::*` set maps almost one-to-one onto our `meters.h`, and we consolidated sensibly:
their `LowPassFilter` + `HighPassFilter` are our `filter_curve(f, slot, norm, FilterKind)`
(`src/ui/oled/meters.h:128`), their `Attack` + `Release` are our `envelope_stage(..., StageKind)`
(`:137`). Same for `Percent`/`UnipolarKnob`/`UnipolarBar`/`BipolarBar`/`Pan`/`Slider`/
`LengthSlider`/`SidechainDucking`.

**Only on their side:** the two menu walkers (`Menu`, `HMenu`); the full-screen value editors as
composable widgets; `Envelope`/ADSR as a single drawn curve rather than per-stage slots; the
three waveform components; and `DottedLine`/`FilledPolygon` as reusable primitives.

**Only on our side:** everything popup/console (`popup_frame`, `popup_text`, `working`,
`progress` — `src/ui/oled/popup.h`), `output_name`, `segment_digits`, `vu`, the paged
`columns` system with `Paging`/`paging_of` (`src/ui/oled/columns.h`), `enum_value`'s windowed
slice rendering, `decimal_value` with a blinking edit cursor, and `text_wrap`.

**Grid, ours** (`src/ui/README.md:82-99`): `pad`, `row`/`clip_row`, `status_pad`/`audition_pad`/
`sidebar_columns`, `macros`, `velocity_column`, `automation_column`, `vu_meter`, `tick_squares`,
`waveform_row`/`waveform_full`, `text_keyboard`, `greyout`, `gesture`.

**Grid, theirs** (`crates/deluge-grid-toolkit/src/lib.rs:45-50`): `clip_grid`, `clip_list`,
`ClipCellComponent`, `draw_clip_row`, `status_sidebar`, `UnipolarAutomationColumn`/
`BipolarAutomationColumn`, `NoteVelocityColumn`, `TextKeyboardComponent`, `draw_waveform`.

The overlap is close. **Only theirs:** `clip_grid`/`clip_list` — *windowed* widgets that scroll
a viewport over a larger logical grid and own a scroll+selection+gesture FSM
(`widgets/clip_grid.rs:79-99`, `widgets/clip_list.rs:30-60`). **Only ours:** `macros`,
`tick_squares`, `vu_meter`, `greyout`, and a standalone `gesture` FSM.

---

## 3. Grid components

### 3.1 Buffer model

`PadGrid` (`src/gfx/grid/pad_grid.h:28-66`): runtime `GridDimensions { main_cols, sidebar_cols,
rows }`, a `RGB cells_[16][32]` row-major store sized for the largest supported grid, and a
**per-row dirty mask** (`dirty_rows()`, `mark_row_dirty`, `:49-60`) that matches the existing
`renderMainPads(whichRows)` protocol. The header explicitly warns that `cells_` is *not*
layout-compatible with legacy `PadLEDs::image` (32 columns vs 18) and that `row(y)` is the
aliasing seam (`:21-27`). `mark_all_dirty` spells out the `rows >= 32` case because `1u << 32`
is UB (`:56-58`).

`Grid` (`crates/deluge-grid-toolkit/src/grid.rs:8-13`): fixed `[[Option<Color>; 18]; 8]`, no
dirty tracking, blitted whole every frame (`:139-146`), and blanked at the top of every painted
frame (`imode.rs:501`). The `Option<Color>` is the interesting part — see §3.3.

We are ahead on the model (runtime dims, dirty rows, larger capacity); they are ahead on
transparency.

### 3.2 Tweens / animations — our biggest content gap

Ours (`src/gfx/grid/tween.h:9-27`), four pure functions with caller-owned timing and an explicit
`out` buffer: `fade` (Q16 `t`), `scroll_horizontal` (sidebar untouched), `scroll_vertical`
(sidebar moves with the rows — the legacy asymmetry, documented at `:18-21`), `grey_out`. Both
scrolls clamp their offset with a stated reason (`tween.cpp:20-22`). `src/gfx/README.md:27` lists
zoom / explode / expand-collapse as deferred.

Theirs (`crates/deluge-grid-toolkit/src/animations/mod.rs:10-15`), six, behind an `Animation`
trait with `tick(delta_ms) -> Option<Grid>` (`:64-76`) and a `build_animation` factory returning
`Box<dyn Animation>` (`:79-140`):

| animation | file | notes |
|---|---|---|
| `FadeAnimation` | `animations/fade.rs` | ≈ our `fade` |
| `HorizontalScrollAnimation` / `VerticalScrollAnimation` | `animations/scroll.rs` | ≈ our scrolls, plus a `scroll_to_black` mode |
| `HorizontalSmearScrollAnimation` / `VerticalSmearScrollAnimation` | `animations/smear_scroll.rs:1-77` | **sub-pixel** scroll: `Color::blend_static(a, b, (frac * 65535) as u16)` per pad — the hardware-PIC smooth-scroll look. No counterpart. |
| `ExplodeAnimation` | `animations/explode.rs:1-60` | bilinear explode/implode about an origin; a port of legacy `renderExplodeAnimation` |
| `ZoomAnimation` | `animations/zoom.rs:1-50` | dual-buffer cross-fade with `pin_squares`; a port of legacy `renderZoomWithProgress` |
| `ExpandCollapseAnimation` | `animations/expand_collapse.rs` | session ↔ clip transition |

The three we deferred are already written on the SDK side, against the same legacy source
(`pad_leds.cpp`), and the smear scroll is a fourth we never scoped. Their `Box<dyn Animation>` +
`f32` shape is wrong for us (heap, virtual dispatch, floats), but the *pixel maths* is the
expensive part and it transliterates directly into our `(from, to, t, out)` signature.

### 3.3 Compositing

`GridLayer` (`crates/deluge-grid-toolkit/src/layer.rs:32-37`) is a full grid of `Option<Color>`
with a `BlendMode` of `Replace` / `Overlay` (draw only where the destination is black) / `Tint`
(component-wise multiply) (`:19-26`), flattened by `GridCompositor::composite`
(`:77-111`), with a test per mode (`:118-145`).

We have no transparency and no compositor. Our nearest equivalent is `greyout`
(`src/ui/grid/effects.h:42-62`), which reads pads back through `GridFrame::peek` and rewrites
them in place — and the header honestly documents the consequence: it is "neither idempotent nor
commutative with painting", a second call greys an already-grey pad, and anything painted after
it isn't greyed (`effects.h:50-57`). Legacy avoided this by greying in the *send* path without
mutating the image; a `Tint` layer is the structurally correct answer, and it is what the SDK
has.

### 3.4 Hit-testing, regions, gestures

The paint-and-hit-test primitive is the same idea on both sides: `GridFrame::pad(x, y, colour)
-> Response` (`src/ui/grid/frame.h:93`) vs `Frame::button(pad, color) -> Response`
(`imode.rs:346-349`). Ours reports `pressed`/`released`/`held` plus velocity; theirs reports
`pressed: Option<Pad>` / `released: Option<Pad>` / `held`, i.e. it carries *which* pad but no
velocity.

Beyond the primitive we each added something the other lacks:

- **Ours:** `peek()` for read-back overlays (`frame.h:100`), and `first_press_in(Rect)`
  (`frame.h:126`) — the "which pad in my column was hit" scan written once, which exists because
  our per-column components return `void`. The README argues that decision at length
  (`src/ui/README.md:131-136`): a `Response` collapsed over a whole column can't carry *which
  row*, and two of the six columns have no interaction at all. Sound.
- **Theirs:** semantic **event enums** instead of flags. `ClipGridEvent`
  (`widgets/clip_grid.rs:36-52`) is `Press` / `Tap` / `Hold` / `Clone { from, to }`, returned as
  a `heapless::Vec` from one call, explicitly "mode-agnostic — the host decides what each means
  for its current mode (launch / open / create / clone)". `ClipListEvent` is the same idea per
  row. This is a genuinely better shape for composite widgets than a flag struct plus a
  caller-side re-scan.

**Gestures.** Ours is a standalone, reusable FSM: `gesture(GridFrame&, Hold&, HoldPolicy,
int32_t& second_x, int32_t& second_y) -> GestureEvent` with `None`/`Tap`/`Hold`/`Release`/
`SecondPad` (`src/ui/grid/effects.h:72-146`). The documentation is the best in either codebase:
every transition cites the legacy line it ports (`session_view.cpp:817-824`, `:884-902`,
`:944-945`, `:963-975`, `arranger_view.cpp:1307`, `qwerty_ui.cpp:217-220`), both deliberate
generalisations away from the session-view FSM are named, the "every event in the tick is
processed in order, and the *last* result wins" rule is justified, and the losing case is
spelled out (`[release A, press B, press C]` reports `SecondPad` and drops `Tap`).

Theirs is the same FSM but welded into `clip_grid` (`widgets/clip_grid.rs:145-215`) and not
reusable. Two details there are still worth stealing: a `ClipGridConfig { clone: bool }` switch
so a mode where multi-touch means "many launches, not a clone" can disable the second-finger
branch (`:54-62`), and `ClipGridState::held()` exposing the held cell across frames so hosts can
drive encoder-while-held gestures without their own bookkeeping (`:102-105`).

Both sides independently arrived at the same good trick: arm the hold deadline on the gate
rather than poll — ours at `effects.h:120-123`, theirs at `clip_grid.rs:211` ("wake exactly at
the threshold instead of busy-polling").

### 3.5 Colour maths and palettes

Our `RGB` (`src/gfx/grid/rgb.h`) is the legacy class moved wholesale: `transform`/`transform2`
(`:102-105`, `:279-281`), `forTail` (`:113`), `forBlur` (`:125`), `average` (`:139`), `dim`
(`:153`), `dull` (`:166`), `greyOut` (`:178`), `blend`/`blend2` (`:196`, `:211`), `rotate`
(`:308`), `fromHue`/`fromHuePastel` (`:84`, `:92`). All fixed-point, mostly `constexpr`, with a
`static_assert(is_trivially_copyable_v<RGB>)` (`:357`).

Their `ColorExt` (`crates/deluge-grid-toolkit/src/color.rs:18-70`) is the same list plus:

- `to_hsv()` / `from_hsv(h, s, v)` round-trip in `f32` (`color.rs:215`, `:252`),
- `palette::cycle_hue(color, delta)` — 15°-per-step hue rotation that leaves achromatic colours
  alone (`crates/deluge-grid-toolkit/src/palette.rs:54-62`),
- `lerp` / `lerp_slice` with an optional NEON path over 16 pixels at a time
  (`color.rs:473-516`),
- ~20 named-colour constructors.

The palettes themselves are the same data on both sides — standard, `pastel`, `kelly`, `wad`,
compare `src/gfx/grid/palette.h:8-107` with `crates/deluge-grid-toolkit/src/palette.rs:17-167`.
They additionally carry `section::DEFAULT_COLORS`, the 24 `defaultClipSectionColours`
(`palette.rs:65-99`), which we have not moved into `gfx` yet.

One note on our side: `src/gfx/grid/sine_table.cpp` duplicates `util::sineWaveSmall` verbatim to
keep `gfx` from depending on `util`, and `src/gfx/README.md:17-22` correctly flags that as a
silent-drift hazard. The SDK sidesteps it with `libm` behind a `F32Ext` trait
(`crates/deluge-grid-toolkit/src/float_ext.rs:1-24`) — not an option for us at fixed-point
parity, so the duplication stands, but the README's suggestion (hoist the table somewhere both
`util/` and `gfx/` can see) is the right resolution.

---

## 4. Testing

### 4.1 Unit and golden coverage — ours is much stronger

20 spec files, 7,014 lines, over `tests/spec/ui/`: `columns_spec` (808), `meters_spec` (863),
`popup_spec` (849), `values_spec` (637), `grid_columns_spec` (557), `list_spec` (513),
`rows_spec` (363), `effects_spec` (336), `keyboard_spec` (325), `sidebar_spec` (274),
`text_spec` (247), `waveform_grid_spec` (216), `grid_frame_spec` (191), `misc_oled_spec` (376),
`scroll_text_spec` (129), `oled_frame_spec` (123), `repaint_gate_spec` (58), `led_frame_spec`
(41), `rect_spec` (25). Plus 75 golden files under `tests/golden/ui/widgets/`.

The rig (`tests/spec/ui/support/frames.h:44-83`) is templated on `Geometry`, so the same widget
is checked at 128×48-with-bezel and 128×64-flush — the point of geometry-as-a-value. Goldens are
paired `*_48.pbm` / `*_64.pbm` for OLED and `.state` dumps for grid, on the stated grounds that
a pad surface has one geometry and the thing worth freezing is the RGB it computes
(`src/ui/README.md:113-119`).

The single best idea in our harness has no SDK counterpart: **`OledRig::reference`**, a second
identically-sized canvas "for drawing the legacy routine's output into, so a port can be
compared against its original rather than against its own last run"
(`tests/spec/ui/support/frames.h:80-83`). That converts a golden from a regression tripwire into
an actual parity proof.

The SDK has 141 inline `#[test]`s across the two toolkits (79 in `deluge-ui-toolkit`, 62 in
`deluge-grid-toolkit`), plus 7 in `deluge-fonts` and 19 in `deluge-linux-ui`. There are **no
pixel goldens for the UI**. Their pixel-level assertions go through purpose-built targets: a
`NullTarget` that discards (`crates/deluge-ui-toolkit/src/menu.rs:908-928`) and a `CountTarget`
that counts lit pixels to confirm the renderer drew something and that the advance matches the
measured width (`crates/deluge-fonts/src/lib.rs:410-424`). The `imode` gate tests
(`crates/deluge-grid-toolkit/src/imode.rs:519-642`) are excellent behavioural specs and are the
one place their tests beat ours in *kind* — we have `repaint_gate_spec.cpp` at 58 lines against
their six focused gate tests plus region-routing and clipping tests.

### 4.2 Simulator and end-to-end — theirs is real, ours is a plan

The SDK ships a headless golden-frame harness (`docs/simulator.md:182-278`):

```
cargo deluge sim --headless --script tests/menu.txt --out got/
diff -r got/ tests/golden/
```

with a script format of `<at_ms> <command> <args>` over `button <id> down|up`, `pad <x> <y>
down|up`, `encoder <index> <delta>`, `snapshot <name>`, `quit` (`docs/simulator.md:199-236`).
Each snapshot writes `<name>.png` (OLED, byte-stable) and `<name>.state` (a diffable dump:
`cv`, `gate`, `synced`, `led <index>`, `pad x y r g b`) (`:250-266`).

**Our `.state` goldens already use that exact format** — `tests/golden/ui/widgets/macros.state`
begins `pad 0 0 200 100 50`. That is the alignment our own spec asks for
(`docs/superpowers/specs/2026-08-26-ui-stack-redesign-design.md:494`: "same script and output
format as deluge-sdk's headless harness so spark's simulator and golden tooling apply
unchanged"), and it is already half-honoured.

The link is also already specified in the other direction. `docs/simulator.md:280-296` documents
external-process mode driving **DelugeFirmware's own `deluge_host`** over `deluge-protocol`:

```
DELUGE_HOST_LINK=127.0.0.1:9000 ./build-sim/deluge_host
deluge-simulator --connect 127.0.0.1:9000
```

`deluge-protocol` (`crates/deluge-protocol/src/lib.rs:48-88`) is a small framed byte protocol —
`UPDATE_DISPLAY` (768 bytes, page-major, matching our canvas layout exactly), `SET_ALL_PADS`
(432 bytes col-major RGB), `SET_LED`, `SET_KNOB_INDICATOR`, `PAD_PRESSED`, `ENCODER_ROTATED` —
pinned by a committed golden JSON with an `UPDATE_GOLDEN=1` regeneration path
(`crates/deluge-protocol/tests/golden.rs:1-8`). `deluge-sim-link` is the in-process variant
(`crates/deluge-sim-link/src/lib.rs:1-20`), with a `display_gen` counter so the GUI re-renders
only on change (`:69-70`) — the same dirty-tracking instinct as our `PadGrid::dirty_rows`.

**Verdict:** our unit/golden layer is well ahead; our end-to-end layer does not exist yet, and
the SDK's is not just a model to copy but a system we can literally connect to.

---

## 5. Ideas in the SDK we should have harvested and did not

Ordered by value. Each names the type, file and line.

1. **Two-pass count-then-draw for the menu/list** — `Menu::show`,
   `crates/deluge-ui-toolkit/src/menu.rs:346-415`. The build closure runs twice: a count pass
   that draws nothing (`:396`), then focus clamp + scroll window + scrollbar-gutter decision
   against the now-known count (`:403-406`), then the draw pass (`:409-415`). The stated payoff:
   "no lag, no follow-up redraw" on a screen's first frame (`:349-353`).
   **Why we need it:** our `list` walks the cursor bounded by "the row count the *previous*
   frame saw (`state.rows_last`, or `rows.size()` on the first frame)"
   (`src/ui/oled/list.h:96-99`) — a one-frame lag by construction. The same file admits `list`
   draws no scrollbar partly because the gutter width isn't known in time
   (`list.h:126-129`). `ListState` already has `rows_last` (`src/ui/core/state.h:8`), so the
   two-pass form would let that field become a pure cache instead of the authority.

2. **The three deferred animations, already ported** —
   `crates/deluge-grid-toolkit/src/animations/explode.rs:1-60` (bilinear explode/implode, a port
   of `renderExplodeAnimation`), `.../zoom.rs:1-50` (dual-buffer cross-fade with `pin_squares`, a
   port of `renderZoomWithProgress`), `.../expand_collapse.rs`. Plus a fourth we never scoped:
   `.../smear_scroll.rs:52-77`, sub-pixel scrolling via
   `Color::blend_static(a, b, (frac * 65535) as u16)`.
   `src/gfx/README.md:27` lists exactly these as deferred. The maths is done; it needs
   transliterating into our `(from, to, t, out)` signature with `RGB::blend`'s Q16 index in place
   of `f32`.

3. **`split_rows` / `split_cols` on the grid `Rect`** — `imode.rs:367-398`, with the pane-routing
   test at `:578-603`. Our `src/gfx/grid/rect.h:7-21` has neither, though
   `src/gfx/oled/geometry.h:50-51` has both. Five lines, and it makes "sidebar in the last two
   columns" a one-liner instead of hand arithmetic in every scene.

4. **`InkBounds` — centre by actual ink, not by nominal bounding box** —
   `crates/deluge-ui-toolkit/src/hmenu.rs:57-110`, used by `place` at `:232-262`. A
   pixel-discarding `DrawTarget` records the lit extent of a widget, so a knob arc or a pan
   cylinder is centred on its visible pixels rather than on a box that includes blank padding.
   The refinement at `:244-254` is the good part: it unions the ink at **both value extremes**
   (`for t in [lo, hi]`), so the layout doesn't jitter as the value changes. Our `meters.h`
   widgets centre within their `slot` rect (`src/ui/oled/meters.h:69-168`) and inherit whatever
   padding the legacy geometry had.

5. **A structural repaint gate** — `GridUi::run` +
   `FrameOutput::{Skipped, Painted}`, `imode.rs:415-512`. Make it impossible to paint a clean
   frame by having the gate own the call. Our `RepaintGate::needs_frame`
   (`src/ui/core/repaint_gate.h:12`) is a predicate a shell can forget to consult. Our deadline
   arithmetic is the better half of the two (wrap-safe, `repaint_gate.cpp:13`, vs their plain
   `now_ms >= t` at `imode.rs:488`) — combine ours with their shape.

6. **`GridLayer` / `BlendMode::{Replace, Overlay, Tint}`** —
   `crates/deluge-grid-toolkit/src/layer.rs:17-112`. The structurally correct fix for
   `greyout`'s documented non-idempotence (`src/ui/grid/effects.h:50-57`): tint as a composited
   layer rather than an in-place rewrite, which restores legacy's "applied exactly once per
   frame however many times the image was drawn" property.

7. **Semantic event enums for composite widgets** — `ClipGridEvent`,
   `widgets/clip_grid.rs:36-52`; `ClipListEvent`, `widgets/clip_list.rs:16-28`. Mode-agnostic
   `Press`/`Tap`/`Hold`/`Clone { from, to }` returned from one call, versus our flag `Response` +
   caller-side `first_press_in` re-scan. Worth adopting for `macros`, `velocity_column` and
   `automation_column` if their `void` return ever proves too thin.

8. **Windowed grid widgets** — `ClipGridState { scroll_x, scroll_y, selected }` with
   `scroll_to`/`scroll_by` clamped to `[0, dims - win]` (`widgets/clip_grid.rs:82-119`), and the
   `to_logical` mapping at `:143-148`. Our `row`/`clip_row` take already-windowed data; the
   scroll-window logic will have to be written somewhere, and this is a clean home for it.

9. **`MenuState::needs_redraw()`** — `menu.rs:139-149`. "This frame changed which screen renders
   next; draw once more before blocking for input."

10. **HSV round-trip and hue cycling** — `ColorExt::to_hsv` / `from_hsv`
    (`crates/deluge-grid-toolkit/src/color.rs:215,252`), `palette::cycle_hue`
    (`palette.rs:54-62`). Also `section::DEFAULT_COLORS`, the 24 clip-section colours
    (`palette.rs:65-99`), which our `src/gfx/grid/palette.h` does not yet carry.

11. **`PadInput::is_idle`'s documented rule** — `imode.rs:115-120`: a steadily held pad is *not*
    new activity; widgets that animate while held must ask for a repaint. Our
    `needs_frame(had_input, …)` leaves that judgement to an unwritten shell with no note.

12. **`ClipGridConfig`** — `widgets/clip_grid.rs:54-62`: a per-call switch turning the
    second-finger clone branch off for modes where multi-touch means many independent launches.
    Our `gesture()` always reports `SecondPad`.

13. **BMP-driven icon build pipeline** — `crates/deluge-ui-toolkit/build.rs:10-68` +
    `IconData::from_bmp` (`icons.rs:30-52`) + `util/extract_deluge_icons.py`. Against our 864
    hand-transcribed lines in `src/gfx/oled/icons.cpp`. A CMake codegen step over the same BMPs
    would emit our column-major `Icon` blobs with no runtime cost at all.

14. **The headless script/snapshot harness and the wire link** — `docs/simulator.md:182-296`,
    `crates/deluge-protocol/src/lib.rs:48-88`. Not "an idea to copy" so much as an existing
    system to plug into: our `.state` format already matches, and `deluge_host` is already a
    named `--connect` target.

---

## 6. Deliberate divergences — and whether they hold up

| # | Divergence | Verdict | Reasoning |
|---|---|---|---|
| 1 | No `embedded-graphics`; our own `Canvas` + frames | **justified** | Legacy pixel parity requires the exact legacy rasteriser. e-g would re-derive glyph placement, and the goldens exist precisely to prove we didn't. The cost is the clipping gap (§1.2), which is closable without adopting e-g. |
| 2 | No heap anywhere in widgets | **justified, and enforced** | The SDK allocates freely: `Box<dyn Animation>` per transition (`animations/mod.rs:79-140`), `Vec<Pixel>` per icon blit (`graphics.rs:28-43`), `Vec<f32>` per waveform (`components/waveform.rs:19`). None of that is acceptable in the audio-tick path. Our rule is checked mechanically (`src/ui/README.md:9-14`, `scripts/check_layer_rules.py`). |
| 3 | Fixed-point colour maths, no `f32` | **justified** | `RGB::blend`'s Q16 index reproduces legacy bit-exactly; the SDK's `f32` + `libm` path (`float_ext.rs`) cannot, and would make goldens toolchain-dependent. The cost is no HSV — worth adding as a separate fixed-point helper (§5.10). |
| 4 | Templates only for `region` bodies (+ two named legacy bridges) | **justified** | Code size. The SDK is generic over `D: DrawTarget` at every call site, which monomorphises per target. Our two legacy-bridge templates are scoped and have a deletion date (`src/ui/README.md:101-111`). |
| 5 | Dirty-row tracking instead of full blit | **justified** | Matches the existing `renderMainPads(whichRows)` protocol; the SDK's blit-everything (`grid.rs:139-146`) would be a regression against current firmware behaviour. |
| 6 | One merged `Response` for OLED + grid | **justified, mildly awkward** | The SDK keeps two. Ours documents that a widget may set both halves (`src/ui/core/response.h:8-10`) and the `index` disambiguation fixed a real triple-overload bug. Fine as-is; revisit only if a third surface appears. |
| 7 | Column components return `void` | **justified** | Argued at `src/ui/README.md:131-136`: a `Response` collapsed over a column can't carry *which row*, and two of six have no interaction. `first_press_in` is the read-back. The SDK's answer (event vectors) is better but heavier; ours is the right call at this size. |
| 8 | `greyout` mutates in place | **justified for now, wrong long-term** | Honestly documented as non-idempotent and non-commutative (`effects.h:50-57`). It diverges from legacy, which greyed in the send path. `GridLayer`-style tinting (§5.6) is the real fix. |
| 9 | `GridFrame` carries a 128-byte event cache | **not justified** | 176 bytes per frame, zero-initialised for every nested `region()` child (`src/ui/grid/frame.h:17-25`), against the SDK's zero-storage lazy iterator (`imode.rs:254-265`). A callback form (`for_each_local_event`) gets the same behaviour for free in C++ too. Worth revisiting. |
| 10 | Advisory rather than structural repaint gate | **not justified** | See §2.2 / §5.5. The shell doesn't exist yet, so the cost of changing shape is currently zero. |
| 11 | Grid `Rect` without split helpers | **not justified** | An oversight rather than a decision — the OLED `Rect` has them. §5.3. |
| 12 | `list()` widget instead of a menu-tree walker | **justified at this layer** | A drilldown walker needs to know about menu *items*, which is model-side; the leaf toolkit correctly stops short. But the two-pass count/draw trick inside `list` is independent of that and should be taken (§5.1). |
| 13 | Hand-transcribed icon data | **weakly justified** | Parity is surer and there's no build-time dependency, but 864 lines of transcribed hex is a maintenance liability the SDK avoided (§5.13). |
| 14 | `sine_table.cpp` duplicates `util::sineWaveSmall` | **justified, flagged** | Keeps `gfx` leaf-clean; the drift hazard is documented (`src/gfx/README.md:17-22`) with the right resolution named. |
| 15 | `src/notify` has no SDK counterpart | **justified** | `DirtySet` (pointer-free atomics, `[any]`-safe, `src/notify/dirty_set.h:40-70`) and `NoticeQueue` solve a problem the SDK doesn't have — it has no legacy model to decouple a UI from. Nothing to harvest here in either direction. |

### A note on where we are simply ahead

Documentation quality is not close. Nearly every non-obvious decision on our side carries the
legacy line number it reproduces and the reason it deviates — `src/ui/grid/effects.h:89-146`
(the gesture FSM against five legacy call sites), `src/ui/core/text_wrap.h:36-58` (every wrap
rule, including the ones that look like bugs), `src/ui/core/metrics.h:29-36` (why
`kHorizontalMenuBaseY` must stay distinct from `kMenuBaseY48/64`), `src/ui/oled/list.h:74-129`
(the whole layout, rule by rule). Section 12 of the toolkit spec records every amendment made
during implementation with a justification per line. The SDK's docs are good by Rust-crate
standards but carry nothing comparable, because the SDK was never trying to reproduce anything
pixel-for-pixel.

---

# UI foundation audit: `ui/toolkit` (DelugeFirmware) vs spark

**Ours:** `/home/kate/GitHub/DelugeFirmware-clean`, branch `ui/toolkit`
**Theirs:** `/home/kate/GitHub/spark` @ `79a65d44`, plus its pinned upstream
`deluge-sdk` @ `a5c8195` (`~/.cargo/git/checkouts/deluge-sdk-68c8ed2599168112/a5c8195`),
which is where spark's grid primitives actually live.

## 0. Important provenance correction

The umbrella spec (`docs/superpowers/specs/2026-08-26-ui-stack-redesign-design.md:73`)
says decision D5 (separate OLED and grid libraries) was modelled on spark's
`spark-grid` / `spark-display-oled` split. That is only half true, and the half that
matters is the other one:

- spark **does** have two crates: `ui/grid` (`spark-grid`) and `ui/display-oled`
  (`spark-display-oled`, `ui/display-oled/src/lib.rs:55` `trait OledView`).
- But spark's grid crate is a thin re-export shell over an *upstream* crate:
  `ui/grid/src/lib.rs:55` `pub use deluge_grid_toolkit::{BlendMode, Color as RGB, ColorExt,
  Grid, GridCompositor, GridLayer, GridRgb as GridRGB, Pad}`, pinned at
  `Cargo.toml:237` to `github.com/FirestormAudio/deluge-sdk`.
- The **immediate-mode frame model our `GridFrame` is a port of** is
  `deluge-grid-toolkit`'s `src/imode.rs` — not anything in spark. spark uses it in
  exactly one place (`ui/grid/src/views/session.rs:270` `ui: GridUi`), and even there
  it is bolted onto a retained view (`:271-274` "Input is still routed through the
  existing handlers, so the widget runs with empty input each frame").
- spark's OLED side has **no widget library at all**. `ui/display-oled` contains seven
  view structs and nothing reusable; the reusable OLED primitives are again upstream, in
  `deluge-sdk`'s `deluge-ui-toolkit` (`src/params/`, `src/components/`, `src/text/`).

So the accurate statement is: *the OLED/grid split and the immediate-mode frame both come
from deluge-sdk; spark is a consumer that has only partly adopted them.* §13 of the
umbrella spec ("Harvested from deluge-sdk and spark",
`2026-08-26-ui-stack-redesign-design.md:542`) already says this correctly; D5 at line 73
overstates spark's role and should be reworded.

---

## 1. The OLED-vs-grid component split

### 1.1 How the boundary is drawn

| | spark / deluge-sdk | ours |
|---|---|---|
| grid primitives | `deluge-grid-toolkit` (`grid.rs`, `imode.rs`, `layer.rs`, `pad.rs`, `palette.rs`, `color.rs`) | `src/gfx/grid/` (`pad_grid.h`, `rgb.h`, `palette.h`, `rect.h`, `tween.h`) |
| grid components | `deluge-grid-toolkit/src/widgets/` (8 files) + `spark-grid/src/components/` (~24 files) — **two parallel sets** | `src/ui/grid/` (7 files), one set |
| OLED primitives | `deluge-ui-toolkit` (`text/`, `primitives/`, `icons/`, `graphics.rs`) | `src/gfx/oled/` (`canvas.h`, `font.h`, `icons.h`, `geometry.h`) |
| OLED components | `deluge-ui-toolkit/src/params/`, `src/components/` | `src/ui/oled/` (9 files) |
| enforcement | none — crate deps only; the two grid component sets duplicate each other (`spark-grid/src/components/waveform_display.rs` vs `deluge-grid-toolkit/src/widgets/waveform_display.rs`) | `scripts/check_layer_rules.py`, ctest `layer_rules`, pre-commit hook; three ratchet rows at count 0 (`src/ui/README.md:28-48`) |

**Verdict on the boundary itself: ours is cleaner and mechanically enforced.** spark
carries the duplication the umbrella spec's "Avoided" list names
(`2026-08-26-ui-stack-redesign-design.md:557` "duplicated component sets") — it is
literally observable: `ClipRowData`/`draw_clip_row` exists in
`deluge-grid-toolkit/src/widgets/clip_row.rs:12,60` *and* the older component version is
still shipped in `spark-grid/src/components/clip/row.rs`.

### 1.2 What a "component" owns

spark has **three incompatible component contracts** live at once:

1. **Retained, self-sizing, renders into a fresh buffer** —
   `ui/grid/src/components/component.rs:16`:
   ```rust
   pub trait Component {
       fn render(&self) -> Grid;      // allocates a whole 18x8 Grid per component
       fn needs_redraw(&self) -> bool;
       fn get_size(&self) -> Size;
   }
   pub trait FlexibleComponent: Component { fn set_size(&mut self, size: Size); }
   ```
   Component owns its own state (`ui/grid/src/components/column_controls/velocity.rs:17-29`
   holds `v_display`, `stored_velocity`, `velocity_max/min/step`), and `needs_redraw()` is
   a lie in practice — `velocity.rs:107-109` returns `true` unconditionally. `render()`
   returns a full `Grid` even for a 1-column widget (`velocity.rs:74-105` allocates
   18×8 to paint 8 pads), which the caller then has to blit at an offset.

2. **Immediate-mode free functions over a borrowed frame** —
   `deluge-grid-toolkit/src/widgets/sidebar.rs:23`:
   ```rust
   pub fn status_sidebar<F>(f: &mut Frame, rows: usize, row: F) where F: Fn(usize) -> SidebarRow
   ```
   No state, no allocation, paints through the frame's clip.

3. **`embedded-graphics` `Drawable` + `Positionable`** on the OLED side —
   `deluge-ui-toolkit/src/positionable.rs:7`, e.g.
   `src/params/unipolar_bar.rs:34` `struct UnipolarBar { point, size, value }` with
   `set_value` and a `Drawable::draw` impl. Retained position and value, no input at all.

**Ours has exactly one contract per surface**, and it is contract (2) for both:

- `OledFrame` (`src/ui/oled/frame.h:33`): aggregate of `{canvas, clip, origin, size, style,
  input, gate}`; primitives at `:88-129`; `region()` at `:58`. Widgets are free functions
  taking `(OledFrame&, Rect, State&, data…) -> Response` —
  `src/ui/oled/list.h:130`, `src/ui/oled/columns.h:145`.
- `GridFrame` (`src/ui/grid/frame.h:64`): `{grid, clip, input, gate}` + a `local_events`
  cache; components are free functions `src/ui/grid/rows.h:44`,
  `src/ui/grid/sidebar.h:143,240`, `src/ui/grid/effects.h:181,265`.
- All state is caller-owned POD in `src/ui/core/state.h:7-77` (`ListState`,
  `ColumnsState`, `ScrollText`, `Blink`, `Hold`, `Timer`, `WaveformState`).
  Rule 4 in `src/ui/README.md:11` states this as a hard invariant.

### 1.3 State and input flow

**spark (retained path, the one actually used in the Deluge app):**

```
DelugeEvent → DelugeUI::handle_event (apps/deluge/core/src/ui/mod.rs:182)
  → ViewManager::current_view_mut().handle_event(&UIEvent, &UIContext) -> (Vec<UIAction>, bool)
  → if !handled → GlobalControls::handle_event (mod.rs:212)
  → actions applied; RenderPipeline::mark_display_dirty / mark_grid_dirty
      (apps/deluge/core/src/ui/render_pipeline.rs:49,54)
  → once per tick: flush_display(view) (:61) and flush_grid(grid) (:84)
```
Render and input are **two separate passes over two separate code paths**:
`View::render_grid(&self)` (`apps/deluge/core/src/ui/views/mod.rs:510`) computes
pad→colour, and `View::handle_event` (`:495`) independently recomputes pad→meaning. Every
`Vec<UIAction>` return is a heap allocation per event
(`views/mod.rs:495,500,516,554`).

**deluge-sdk imode path (the good one):**
`Frame::button(pad, colour) -> Response` (`imode.rs:346`) = `paint` + `interact` in one
call; `GridUi::run(now_ms, input, closure)` (`imode.rs:482`) is the gate — it blanks the
grid (`:501`) and *never invokes the closure* on a clean frame, returning
`FrameOutput::Skipped` (`imode.rs:415`).

**Ours:** `GridFrame::pad(x, y, colour) -> Response` (`src/ui/grid/frame.h:93`) is the
same one-call primitive; `RepaintGate::needs_frame(now_ms, had_input, model_dirty)`
(`src/ui/core/repaint_gate.cpp:12`) is the same gate, but it is *external to the frame* —
the shell owns the decision (`2026-08-26-ui-shell-design.md:199-202`), which is the right
call for a firmware with one tick and N surfaces. Input reaches widgets only through
`InputSnapshot` (`src/ui/core/input.h:50`), with a documented edges-vs-levels contract at
`:55-69` that spark has nothing equivalent to (spark's `UIContext`
`ui/grid/src/events.rs:20` is a `Vec<Pad>` of pressed pads plus two bools, and
`press_pad`/`release_pad` at `:51,70` silently change semantics under sticky keys).

### 1.4 Where we match imode, and where we diverge

| imode (`deluge-grid-toolkit/src/imode.rs`) | ours (`src/ui/grid/frame.h`) | note |
|---|---|---|
| `Frame{grid, input, now_ms, repaint, clip}` `:234` | `GridFrame{grid, clip, input, gate}` `:64` | `now_ms` folded into `InputSnapshot::now_ms` (`input.h:72`) |
| `paint(pad, colour)` `:293` | folded into `pad()` | — |
| `interact(rect) -> Response` `:315` | **missing** | see §4.1 |
| `button(pad, colour) -> Response` `:346` | `pad(x, y, colour) -> Response` `:93` | ours adds `velocity` and `held` |
| `events()` region-local iterator `:254` | `local_events()` `:116` + 32-entry cache | ours is heap-free but costs 128 B *per frame object*, admitted at `frame.h:21-24` |
| — | `first_press_in(rect)` `:126` | ours only |
| — | `peek(x, y)` `:100` | ours only; needed because `greyout` mutates in place |
| `region(rect, body)` `:354` | `region(rect, body)` `:137` | identical semantics |
| `split_rows/split_cols` `:367,384` | **missing** | see §4.2 |
| `request_repaint` / `request_repaint_after` `:401,406` | `RepaintGate` `repaint_gate.h:10,11` | equivalent, but ours is per-surface and shell-owned |
| `PadMask` `[u8; 18]` `:55` | `PadMask` `uint32_t[32]` `input.h:31` | ours sized for 32×32 boards |
| `FrameOutput<R>` `:415` | **missing** | shell returns nothing; see §4.3 |
| `Response{pressed: Option<Pad>, released, held}` `:191` | `Response{changed, focused, entered, pressed, released, held, velocity, index}` `response.h:11` | ours merges OLED+grid; `index` is per-widget-defined (`response.h:22-29`) where spark's carries a typed `Pad` |

**Rating — component split: better.** One contract per surface instead of three; layer
rules enforced by CI rather than convention; no duplicated component set; the OLED side
actually has a widget library (spark's does not). Cost: we lost `interact`, the
`Rect` split helpers, and a typed pad in `Response`.

---

## 2. Scene / shell / surface model vs spec D

### 2.1 What spark has

`ViewManager` (`apps/deluge/core/src/ui/view_manager.rs:47`):

- `view_stack: Vec<Box<dyn View>>` (`:58`) plus a separate `current_view`;
  `transition_to` `:89`, `push_view` `:107`, `pop_view` `:121`.
- `View` trait (`apps/deluge/core/src/ui/views/mod.rs:481`) with
  `render_display(&mut FrameBuf<BinaryColor, [BinaryColor; 128*43]>)` `:507`,
  `render_grid(&self) -> Grid` `:510`, `get_led_states() -> Vec<(HardwareLED, bool)>` `:516`,
  `on_enter/on_exit` `:534,539`, `handle_core_event` `:562`,
  `is_grid_overlay()` `:574`, `occludes_grid()` `:588`,
  `save_state/restore_state` via `Box<dyn ViewState>` `:605,620`,
  `frontend_settings()` `:633`.
- `SceneId` equivalent: `ViewType` (`views/mod.rs:138-260`), a 30-variant enum carrying
  args (`NoteClip(ClipId)`, `Rack(Uuid, Option<ClipId>, Option<PitchMap>)`,
  `Slicer{kit_id, file_path: String, start_manual}`).
- Grid layering: `render_grid_layered()` `:140` — find the topmost non-overlay base
  (`:146`), `dimmed(2)` it (`:149`), composite overlays through
  `GridCompositor`/`GridLayer`/`BlendMode` (`:155-165`, layer impl in
  `deluge-grid-toolkit/src/layer.rs:19-112` with `Replace`/`Overlay`/`Tint`).
- LEDs: `get_led_states_layered()` `:177` — a `HashMap<HardwareLED, bool>` filled
  bottom-up so the top view wins, deeper views supply the rest (`:177-199`).
- `tick_stack(delta_ms)` `:201` stops descending at the first `occludes_grid()`.

### 2.2 What spark does **not** have

- **No surfaces.** One OLED, hard-coded `128×43` in the trait signature itself
  (`views/mod.rs:507`), and the number appears again in `ui/display-oled/src/lib.rs:57`,
  `apps/deluge/core/src/ui/widgets.rs:19`, with a *contradicting* `HEIGHT: usize = 48`
  in `apps/deluge/core/src/ui/layout.rs:15`. `grep -r 'display_count|SurfaceRole'` over
  spark returns nothing.
- **No board profile.** Button identity is a hard-coded `match` on raw PIC ids
  (`apps/deluge/core/src/interface/protocol.rs:381-423`), and LED identity is a second
  hard-coded `match` (`:298-358`) that **panics** on the gold-knob LEDs (`:352`).
  Firestorm is a wholly separate app (`apps/firestorm/`) sharing only `spark-grid`.
- **No style/profile object.** Fonts and metrics are picked per call site inside each
  view.
- **Two parallel scene enums** — `spark_grid::ViewType` and the app's `ViewType`, bridged
  by `grid_view_type()` (`views/mod.rs:340`) and a `From` impl (`:390-410`) that loses
  information (`Automation => Session`, `Rack => Session` at `:402-403`). This is
  precisely the thing our spec lists under "Avoided"
  (`2026-08-26-ui-stack-redesign-design.md:557`).

### 2.3 Ours (spec D, **not executed** — no `src/ui/shell` exists on this branch)

`2026-08-26-ui-shell-design.md`:

- `SurfaceRole{Main, Param0..3, Aux, None}` `:36`; `BoardProfile` `:39-47` with
  `button_by_matrix`, `led_by_button`, `encoder_by_index/kind`, `oled_role_by_index`,
  `Style*`, `ShortcutTable`; `SurfaceSet` `:53`.
- `Scene` `:71-87` — a near-superset of spark's `View`, including `is_grid_overlay()`
  `:81` and `occludes_grid()` `:82` copied verbatim from `views/mod.rs:574,588`, plus
  `oled_transparent(SurfaceRole)` `:83` and `greyout_mask()` `:84` which spark has no
  analogue for.
- `SceneId` as a POD tagged union `:89`; registry = **one static instance per
  `SceneKind`** `:92`; stack = `std::array<Scene*, 8>` `:94`.
- LEDs: `derive_leds` composes services then stack bottom→top into `LedSurface`, diffs
  against `last_*`, blink phase `now_ms / period_ms` (`:243-245`) — the same layering rule
  as `view_manager.rs:177-199` but with a flat array instead of a per-frame `HashMap`,
  and with a phase rule spark simply does not have (spark's LEDs are booleans:
  `apps/deluge/core/src/hardware/leds.rs:250` `LEDState{on, changed}`).
- `LedFrame` is the only shell-adjacent piece that *is* built:
  `src/ui/core/led_frame.h:21`, 64 fixed entries + two knob rings, no heap.
  `LedId` is deliberately left `enum class LedId : uint8_t;` undefined at `:10` — "defined
  by the board profile (spec D)".

**Rating — scene/shell/surface: better on paper, unproven in code.** Every axis spark
lacks (surfaces, roles, board profiles, multi-display, blink phase, a single scene enum,
static instances) is designed, and the two flags worth copying were copied. But spec D is
*not implemented*: `LedFrame` is the only artifact, so this is a design win, not a
shipped one. Downgrade to **equivalent** if you score on running code.

---

## 3. Simulator protocol and testing

### 3.1 Protocol

spark's panel protocol is `apps/deluge/core/src/interface/protocol.rs`:
- Framing `[len_lo][len_hi][type][data…]`, `len = 1 + N` (`:5-11`, `to_bytes` `:138`).
- To-device types `0x20` UpdateDisplay … `0x2A` ClearAllLeds (`:149-177`).
- From-device `0x01`–`0x05` input, `0x10`–`0x13` responses (`:232-283`).
- Resync heuristic on bad length: consume one byte, return `Some((None, 1))`
  (`:206-214`), with an explicit note that the real fix is COBS (`:204-205`).

Ours (`src/bsp/host/host_link.h`) implements the *same* wire format with the direction
inverted — `deluge_host` plays the device, emitting `MessageToDeluge` and consuming
`MessageFromDeluge` (`host_link.h:18-26`) — and adds one opcode:
```
#define HOST_LINK_MSG_UPDATE_DISPLAY_N 0x2B   // [index][page-major pixels]; host-only extension
```
(`src/bsp/host/host_link.h:49`). That is a clean additive extension in a gap spark left
(spark's highest to-device type is `0x2A`), and it is the only multi-display-aware opcode
in either codebase. spark's panel will ignore it; index 0 still goes out as `0x20`, so
interop is preserved.

Two gaps worth noting:
- The extension has **no reader** yet — `grep 0x2B` over `src/bsp/host/host_link.c` finds
  nothing; only the constant exists. Until `deluge_display_blit_oled_n`
  (`2026-08-26-ui-stack-redesign-design.md:478`) is wired, `0x2B` is a reservation.
- We inherited spark's **non-self-synchronising framing**, including the same
  resync-by-one-byte weakness (`protocol.rs:200-214`). spark documented it; we did not
  copy the note into `host_link.h`.

### 3.2 Testing

**spark: effectively none for UI rendering.**
`apps/deluge/core/src/ui/rendering_tests.rs:1-12` states the strategy outright — "count
the number of lit ('On') pixels", asserting `lit > 0` (`:45`) and grid dimensions
(`:52-57`). `integration_tests.rs` covers routing and `save_state` round-trips
(`:28-52,57`), which is real but narrow. There is no golden-frame infrastructure anywhere
in spark (the only `golden` hits are DSP vectors under `libs/dsp/deluge/tests/`).

**Ours: 19 spec files, 75 goldens, already green.**
`tests/spec/ui/*_spec.cpp` (19 files) with `tests/golden/ui/widgets/` (75 artifacts:
paired `*_48.pbm`/`*_64.pbm` for OLED, `*.state` for grid — `src/ui/README.md:113-119`).
`2026-08-26-ui-stack-redesign-design.md:498` bans exactly spark's approach: "Lit-pixel-count
tests are not accepted as snapshots."

**The planned script harness** (`2026-08-26-ui-test-harness-design.md`) is three levels:
level 1 is done, levels 2–3 depend on the unbuilt shell. The script format `:28-41` is
deluge-sdk's, not spark's — spark has no headless script runner at all; its simulator is
an interactive Iced app (`apps/deluge/native/src/simulator_app.rs:1-27`) with an SVG
faceplate (`:40`).

**Rating — protocol: equivalent** (same wire format, one clean additive opcode ahead, but
the opcode is unimplemented and we inherited the framing weakness without its caveat).
**Rating — testing: much better** (75 byte-exact goldens vs `assert!(lit > 0)`).

---

## 4. Ideas in spark/deluge-sdk we should have harvested and did not

### 4.1 `Frame::interact(rect) -> Response` — hit-test without painting
`deluge-grid-toolkit/src/imode.rs:315`. Reports `pressed`/`released`/`held` over an
arbitrary rectangle, decoupled from painting. We have three partial substitutes and no
whole one: `pad()` (`src/ui/grid/frame.h:93`) paints *and* tests one pad; `peek()` (`:100`)
reads colour but no input; `first_press_in()` (`:126`) reports presses only, never
releases or holds. So "did anything in this column get released this tick?" — the exact
question `src/ui/grid/columns.h`'s `void`-returning components make their callers ask
(`src/ui/README.md:131-136`) — currently needs a hand-rolled `local_events()` scan. Adding
`Response interact(gfx::grid::Rect local) const` to `GridFrame` is ~15 lines and subsumes
`first_press_in`.

### 4.2 `Frame::split_rows(at)` / `split_cols(at)` — pane splitting
`deluge-grid-toolkit/src/imode.rs:367,384`. Returns two local `Rect`s, handling degenerate
splits via `Rect::EMPTY` (`:136`). Our `gfx::grid::Rect` (`src/gfx/grid/rect.h:7`) has
`intersect` and nothing else; every scene will hand-write `{16, 0, 2, rows}` for the
sidebar (already spelled out longhand in `src/ui/grid/frame.h:38`). The umbrella spec
claims `Rect` splitting was adopted (`2026-08-26-ui-stack-redesign-design.md:551`
"`Rect` splitting instead of a layout engine") — it was not. Add `split_h`/`split_v` to
both `gfx::grid::Rect` and `gfx::oled::Rect`.

### 4.3 `FrameOutput<R>` — the gate returns the closure's value, or nothing
`deluge-grid-toolkit/src/imode.rs:415-432` plus `GridUi::run` `:482-512`. Two properties
we dropped: (a) a skipped frame is *structurally* unable to produce a result, so nothing
downstream can accidentally consume stale interaction; (b) the frame closure's return
value is threaded out, so a scene can say `let clicked = ui.run(…, |f| f.button(…).clicked())`.
Our `RepaintGate::needs_frame` (`src/ui/core/repaint_gate.cpp:12`) returns a bare `bool`
and the shell decides what to call — nothing prevents a scene from reading its own POD
state after a skipped frame and acting on it. Cheap fix: have the shell's per-surface
render helper return `std::optional<T>` mirroring `FrameOutput`.

### 4.4 `GridCompositor` / `GridLayer` / `BlendMode`
`deluge-grid-toolkit/src/layer.rs:19-112` — `Replace` / `Overlay` (draw only where
destination is black) / `Tint` (component-wise multiply), used by
`apps/deluge/core/src/ui/view_manager.rs:155-165` to composite an overlay scene over a
dimmed base. Our spec D §7 (`2026-08-26-ui-shell-design.md:215-217`) specifies only
"dim by right-shift … render overlays", i.e. `Replace` with no transparency concept, and
`src/gfx/grid/tween.h` has `fade`/`scroll_*`/`grey_out` but no compositor. `Overlay`
(paint only over black) is exactly the primitive an overlay scene needs to let the base
show through where it drew nothing, and today there is no way to express it — an overlay
scene must paint every pad or leak the base's colours. Worth adding `BlendMode` to
`PadGrid` or a `composite(dst, src, mode)` free function in `gfx/grid`.

### 4.5 The variant-exhaustiveness test pattern
`apps/deluge/core/src/ui/views/mod.rs:413-445` — `all_grid_view_types_convert()`
enumerates every `spark_grid::ViewType` variant in an array and round-trips it, so a
variant added upstream without a mapping fails the test rather than falling into a
`_ =>` arm. We have several places that will want this once spec D lands:
`BoardProfile::button_by_matrix` / `led_by_button`
(`2026-08-26-ui-shell-design.md:41-42`) are tables keyed by enum ordinal with no
mechanism to prove they are complete, and `SceneKind` → static-instance registry
(`:92`) is the same shape. Cheap, and it is the only test idea in spark worth taking.

### 4.6 `PitchMap` as data carried in the navigation target
`ui/grid/src/pitch_map.rs`, threaded through `ViewType::Rack(Uuid, Option<ClipId>,
Option<PitchMap>)` and `ViewType::ScaleSettings(ClipId, PitchMap)`
(`apps/deluge/core/src/ui/views/mod.rs:175,235`). Pad→note is a data table passed between
scenes rather than behaviour baked into a layout class. Ours keeps the legacy
`KeyboardLayout` classes alive behind two template escape hatches
(`src/ui/grid/keyboard.h`, documented as temporary at `src/ui/README.md:101-111`) with the
rewrite deferred to phase 6. A `PitchMap`-shaped POD would let those templates die now,
and would give `SceneId` (`2026-08-26-ui-shell-design.md:89`) something concrete to carry.

### 4.7 `FlexibleComponent::set_size` and `Component::get_size`
`ui/grid/src/components/component.rs:22,27`. Not a design we want (retained + self-sizing),
but the *idea* that a component declares its natural extent has no counterpart in ours —
every caller must know a widget's size a priori to build its `region`. For the OLED side
this bites: `src/ui/oled/text.h`'s `measure` exists, but `list`/`columns`/`popup_frame`
have no "how tall do you want to be" query, so a scene laying out a title + list + status
line has to hard-code the split. A `Size natural_size(const Style&, data…)` companion for
the two or three widgets that have a data-dependent height would be enough.

### 4.8 (documentation) `ui/grid/src/components/README.md:3-15`
The composition tree for "lazy chop view" — an ASCII diagram of how a view decomposes into
nested regions. `src/ui/README.md` has excellent catalogues but no worked composition
example; the first scene author will want one.

---

## 5. Deliberate divergences and whether they are justified

| Divergence | spark / sdk | ours | Justified? |
|---|---|---|---|
| **No heap** | `Vec<UIAction>` per event (`views/mod.rs:495`), `Grid` allocated per component render (`velocity.rs:74`), `Box<dyn View>` stack (`view_manager.rs:58`), `Box<dyn Animation>` (`animations/mod.rs:79`), `HashMap` per LED frame (`view_manager.rs:178`) | `FixedVector<Action, 8>` in `Result` (`shell-design.md:145`), `std::array<Scene*,8>` (`:94`), fixed `LedFrame[64]` (`led_frame.h:23`), 32-entry `local_events` cache (`frame.h:25`) | **Yes.** RZ/A1 with no dynamic allocation in the UI path; spark runs on Linux/desktop. The one cost is honestly recorded: `GridFrame` is 176 B of which 128 B is the event cache, and `region()` zero-inits it per nested child (`frame.h:21-24`). That is a real per-region cost spark does not pay; consider lazily-initialised or shell-supplied scratch if profiling shows it. |
| **POD, caller-owned state** | component owns state (`velocity.rs:17`), or `Box<dyn ViewState>` with `Any` downcast (`views/mod.rs:464-475`) | `src/ui/core/state.h:7-77`, passed by reference into every widget | **Yes**, and better: `save_state`/`restore_state` become a memcpy of a known-size POD rather than a boxed `Any` downcast that "may panic" (`views/mod.rs:615-617`). Open question 1 (`shell-design.md:280`) — slot sizing for `replace_root` — is the price. |
| **Legacy pixel parity** | none attempted; spark reimplements the look approximately (`ui/display-oled/src/views/session.rs:74-78` "matching Deluge firmware" from a comment, not a test) | every OLED widget pinned at 48 and 64 rows against legacy geometry (`src/ui/README.md:113-119`), `static_assert`s tying `Style` fields to legacy metrics (`src/ui/core/style.h:75-78`), legacy line citations in nearly every header (e.g. `src/ui/grid/effects.h:139-147` quoting `pad_leds.cpp:293-294`) | **Yes.** We are replacing a shipped renderer for existing users; spark is building a new product. This is the single biggest quality gap in our favour and the reason the divergence is not optional. |
| **One static instance per scene** | `Box<dyn View>` created on navigation (`view_manager.rs:356 create_view`), dropped on pop | registry of static instances (`shell-design.md:92`) | **Yes**, with a caveat already logged as open question 3 (`stack-redesign-design.md:593`): ~40 scenes' POD state is always resident. Needs the measurement that question asks for; nothing on this branch measures it. |
| **Immediate mode everywhere** | mixed: retained `View`/`Component` in the app, imode only in `session.rs:270` and even there fed empty input (`:271-274`) | immediate mode is the only mode (`src/ui/README.md:9-13`) | **Yes.** spark's hybrid is the worst case — it pays imode's per-frame recompute *and* keeps the retained component's state, and the comment at `views/session.rs:271-274` admits the input path was never migrated. Ours is the consistent version of what spark was reaching for. |
| **Single scene enum** | two (`spark_grid::ViewType` + app `ViewType`) with a lossy bridge (`views/mod.rs:390-410`) | one `SceneId` (`shell-design.md:89`) | **Yes**, explicitly listed as avoided (`stack-redesign-design.md:557`). |
| **Widgets are not templates** | Rust generics used freely (`status_sidebar<F>` `sidebar.rs:23`) | rule 3, `src/ui/README.md:9`; only `region` bodies + two named legacy bridges | **Mostly.** Code size on a 32-bit target justifies it, and the two exceptions are documented with a removal phase (`README.md:101-111`). But it forced the `void* ctx` callback shape in `src/ui/oled/columns.h:185-205`, which is a type-safety regression against `sidebar.rs:23`'s `Fn(usize) -> SidebarRow`. A non-owning `function_ref`-style pair (`void(*)(OledFrame&, bool, void*)`, `void*`) is what is there; it is fine, but flag it as the one place the no-templates rule cost us. |
| **Text clipping incomplete** | `embedded-graphics` clips properly via `DrawTarget` | `glyphs`/`graphic`/`icon`/`invert_rounded` clip only partially (`src/ui/oled/frame.h:22-29`, `src/ui/README.md:123-130`) | **No — this is a genuine regression**, self-declared as a prerequisite for `list`/`columns`/`popup_frame` drawing inside sub-regions. spark gets this free from `embedded-graphics`. It should be closed before the first scene composes nested OLED regions, not deferred. |

---

## 6. Ratings

| Area | Rating | One-line reason |
|---|---|---|
| OLED/grid component split | **better** | one immediate-mode contract per surface with CI-enforced layer rules, vs spark's three coexisting contracts and a duplicated grid component set |
| Frame/widget model | **better** | ports imode faithfully and adds `peek`/`velocity`/`first_press_in`, but loses `interact`, `split_rows/cols` and `FrameOutput` |
| Scene/shell/surface model | **better (design) / equivalent (code)** | surfaces, roles, board profiles, blink phase and one scene enum all exceed spark, but spec D is unimplemented — `LedFrame` is the only artifact |
| Multi-display & board profiles | **better** | spark has no concept of either (`display_count`/`SurfaceRole` grep returns nothing; `128×43` is hard-coded into the `View` trait signature) |
| LED handling | **better** | same bottom-up-with-top-wins layering as `view_manager.rs:177`, minus the per-frame `HashMap`, plus blink phase and knob rings; spark's LEDs are bare booleans that panic on gold knobs |
| Simulator protocol | **equivalent** | same wire format, `0x2B` is a clean additive multi-display opcode — but it has no implementation yet and we inherited spark's non-self-synchronising framing without its COBS caveat |
| Testing | **much better** | 75 byte-exact goldens across 19 spec files vs `assert!(lit > 0)` (`rendering_tests.rs:45`) |
| Compositing / overlays | **worse** | spark has `BlendMode::{Replace,Overlay,Tint}` + `GridCompositor` (`layer.rs:19-112`); we have dim-by-shift and no transparency, so an overlay scene must paint every pad |
| Layout helpers | **worse** | `Rect` splitting is claimed as harvested (`stack-redesign-design.md:551`) but `gfx::grid::Rect` only has `intersect` |
| OLED clipping completeness | **worse** | `glyphs`/`graphic`/`icon` clip partially or all-or-nothing (`src/ui/oled/frame.h:22-29`); spark gets exact clipping from `embedded-graphics` |
| Documentation & provenance | **better** | every widget cites the legacy line it reproduces; but D5 (`stack-redesign-design.md:73`) misattributes the split to spark when it comes from deluge-sdk |

## 7. Top 5 to harvest, in order

1. **`GridFrame::interact(Rect) -> Response`** — `imode.rs:315`; subsumes
   `first_press_in` and answers release/held over a region. ~15 lines.
2. **`Rect::split_h/split_v`** on both `gfx::grid::Rect` (`src/gfx/grid/rect.h:7`) and
   `gfx::oled::Rect` — `imode.rs:367,384`. The spec already claims we have this.
3. **`BlendMode` / `composite()`** in `gfx/grid` — `layer.rs:19-112`. Without `Overlay`,
   spec D's overlay scenes (`shell-design.md:215-217`) cannot let the base show through.
4. **`FrameOutput<R>`-shaped return from the gate** — `imode.rs:415`; makes "skipped frame
   produced no interaction" a type-level fact rather than a convention.
5. **Variant-exhaustiveness tests for the profile tables** —
   `views/mod.rs:413-445`; apply to `BoardProfile::button_by_matrix` / `led_by_button` /
   the `SceneKind` registry before spec D's tables are written by hand.

Plus two corrections, not harvests: reword D5
(`2026-08-26-ui-stack-redesign-design.md:73`) to credit deluge-sdk rather than spark, and
either implement `0x2B` in `src/bsp/host/host_link.c` or mark the constant as reserved.

---

# Architecture review — `src/gfx`, `src/notify`, `src/ui` (branch `ui/toolkit`)

Reviewer pass over the three new libraries, the specs they implement
(`2026-08-26-ui-stack-redesign-design.md`, `…-ui-toolkit-design.md`,
`…-ui-notifications-design.md`), the plans that follow
(`2026-08-27-ui-shell.md`, `…-ui-test-harness.md`, `…-ui-scenes-menus-1.md`),
the implementation sources, and the specs/goldens under `tests/spec/{gfx,ui,notify}` and
`tests/golden/`.

| # | Item | Verdict |
|---|---|---|
| 1 | Boundaries and dependency direction | **Sound**, with two named naming/namespace debts |
| 2 | Immediate-mode design | **Acceptable with caveats** — the *widget* layer is sound; the input-consumption and focus contract above it is missing |
| 3 | Concurrency and real-time | **Acceptable with caveats** — `DirtySet` is correct; the drain's placement and two non-atomic side channels need work |
| 4 | Parity strategy | **Sound** — a real regression net, not a self-golden; ~6 quirks were over-preserved |
| 5 | Footprint | **Acceptable with caveats** — fits easily; three cheap wastes |
| 6 | What plan D hits first | **Needs rework before plan D starts** (items D1–D4 below) |

Sizes below were measured on this host (x86-64, `g++ -std=c++23 -I src`); ARM 32-bit values
are smaller wherever a pointer or `std::span` is involved.

---

## 1. Boundaries and dependency direction

### 1.1 Leaf-ness — delivered

The umbrella spec's §5 rules are actually true of the tree, and mechanically enforced rather
than asserted:

- `src/gfx/CMakeLists.txt:1-10` — `deluge_gfx_oled` / `deluge_gfx_grid` link **nothing**. Rule
  enforced twice: `scripts/check_gfx_layer.py` (ctest `gfx_layer_check`) and the ratchet row
  `gfx-includes-outside` (`scripts/check_layer_rules.py:48-54`).
- `src/notify/CMakeLists.txt:3-5` — `deluge_notify` links nothing; ratchet row `notify-is-leaf`
  (`check_layer_rules.py:56-61`). The library genuinely includes only `<atomic> <cstdint>
  <cstddef> <string_view> <cstring>`.
- `src/ui/CMakeLists.txt:12,29,44` — `ui_core → gfx_oled`; `ui_oled → ui_core, gfx_oled`;
  `ui_grid → ui_core, gfx_grid`. Three ratchet rows at baseline 0
  (`check_layer_rules.py:62-85`): `ui-includes-outside`, `ui-oled-no-grid`, `ui-grid-no-oled`.
- `ui_core` deliberately **dropped** its `gfx_grid` dep (`src/ui/CMakeLists.txt:9-12`) after
  noticing it dragged gfx-grid into every consumer's link closure. That is exactly the right
  instinct.

The `ui-includes-outside` regex (`check_layer_rules.py:69`) is well constructed: it matches any
slashed non-`ui/`/`gfx/` include **plus** the slash-free root headers (`definitions*`,
`board_config.h`, `board_layout.hpp`) that the naive `.*/` form could not see — a hole that was
found and closed rather than left. `<cstdint>` is neither slashed nor named, so std headers
never match. Both probes are documented in `src/ui/README.md:92-96`.

**Gaps in the ratchet, in priority order:**

1. **No rule enforces "no mutable globals" in `gfx`/`ui`** (umbrella rule 4, toolkit rule 3).
   The code holds — I found no mutable file-scope state in either tree — but nothing stops the
   next PR. A `symbol` row matching a non-`const`/`constexpr` namespace-scope definition is
   imperfect but would catch the obvious cases.
2. **Nothing enforces the direction *into* the new libraries.** Nothing stops
   `src/deluge/gui/**` from growing a hard dependency on `ui/**` in a shape that later blocks
   deletion. Not urgent (that is the intended direction) but worth a row once the shell lands.
3. `model-calls-ui`'s regex (`check_layer_rules.py:36-38`) is a superset of `model-calls-display`
   (`:45`), so `display->` is counted twice. Harmless for a ratchet, confusing in a report.

### 1.2 `util/notify.h` as the typed seam — right call

`src/deluge/util/notify.h` is the correct shape. The library stores `int16_t error_code` /
`uint16_t string_id` and knows nothing else; the app-layer header adds `Error` and
`l10n::String` overloads (`util/notify.h:27-51`). The reason given (`gui/l10n/strings.h` is not
a leaf — it pulls `io/midi/midi_device.h`) is real, and the header takes the extra step of
*forward-declaring* `deluge::l10n::String` (`util/notify.h:21-23`) instead of including, so 31
translation units under model/playback/storage/io get the raisers without a `gui/` include.

Two checks I ran on that seam, both clean:
- `enum class String : size_t;` in `util/notify.h:22` matches
  `gui/l10n/strings.h:7` exactly (`: size_t`), so no ODR/underlying-type mismatch.
- 994 `STRING_FOR_*` values fit in the `uint16_t string_id`, and `EMPTY_STRING = 0`
  (`strings.h:8`) coincides with the "no id" sentinel, so the sentinel cannot shadow a real
  string.

The one wart: `popup_type` is a bare `uint8_t` requiring `static_cast<uint8_t>(PopupType::X)` at
each call site, and `0` means "unspecified" while `PopupType::NONE` is also `0` — handled by
`popup_type_of()` at the boundary (`notify_drain.cpp:21-23`) with a good comment. Acceptable as
an interim; it is the kind of untyped integer that silently rots, so the `PopupType`-into-notify
move should not be left indefinitely.

### 1.3 The drain — a clean temporary on one axis, a new tangle on two

`drain_notifications()` (`src/deluge/gui/ui/notify_drain.cpp:25-165`) called from
`ui.cpp:390`, first under `pendingUIRenderingLock`.

Good:
- The placement rationale is right and is written down (`ui.cpp:384-389`): the drain performs
  real UI mutation, so it must precede legacy rendering, and being inside the lock stops a
  render it triggers from re-entering and draining twice.
- `legacy_redraw_for` is factored into its own TU (`notify_drain_mapping.cpp`) precisely so it
  can be unit-tested without the app (`tests/spec/notify/drain_mapping_spec.cpp`). Good
  instinct.
- The hook re-dispatch is honest about *why* it exists: `raise(Dirty::PlaybackState)` cannot
  reproduce `ArrangerView::notifyPlaybackBegun()`'s `mustRedrawTickSquares` + end-audition, or
  `AudioClipView::clipNeedsReRendering()`'s scroll-back side effect (`notify_drain.cpp:32-38,
  61-81`). The codemod that created the call sites carries a matching all-caps CAUTION
  (`scripts/codemods/notify_raise.py:16-26`) recording that this pairing was broken once
  already. That is exactly the right way to carry a temporary.

Not good:

- **The drain sits behind the pad-output-space early-out.** `doAnyPendingUIRendering`
  (`ui.cpp:374-395`) returns *before* `drain_notifications()` when
  `deluge_control_pad_output_space()` is low. So while the pad bus is congested — the exact
  "flicking through presets very fast" case the guard exists for — notices are not drained,
  the count-in popup is not polled, and the cull-alert LED is not raised. `NoticeQueue` is
  16-deep overwrite-oldest (`notice_queue.h:24`, `.cpp:18-21`), so popups can be *lost* under
  congestion. Legacy `display->displayPopup` was synchronous and had no such coupling. This is a
  behaviour regression introduced by the drain's placement, and the fix is one line: drain
  above the early-out.
- **Two function-local statics make the drain untestable and unresettable.**
  `static bool was_playing` (`notify_drain.cpp:43`) and `static int32_t shown_count_in`
  (`:97`). They cannot be reset for a test, and they survive a song load / UI reset. If
  playback is already running at the first drain, no begun-edge fires. `drain_notifications()`
  itself has no test — only `legacy_redraw_for` does.
- The drain re-dispatches to *named legacy singletons* (`arrangerView`, `audioClipView`,
  `soundEditor`, `getRootUI()`) — `notify_drain.cpp:50-81`. Each is a documented,
  reason-carrying exception, but there are four of them plus a `PadLEDs::clearTickSquares()`
  (`:91`), and the file is 165 lines. It is on the edge of being a second god-object. It should
  be deleted wholesale by spec F phase 4/5, not extended; consider capping it with a ratchet row
  counting re-dispatch sites in `notify_drain.cpp` so it can only shrink.

### 1.4 Ratchet rows — right invariants, with one caveat

The five committed rows (`scripts/layer_rules_baseline.txt`) are the right axes:
`model-includes-ui` (~250 entries), `model-calls-ui` (the 652-line census's live half),
`model-calls-display`, plus the three zero-baseline leaf rules. The baseline is per-file, not
global, which is the correct granularity — it prevents shuffling a violation from one file to
another. The top offenders match the spec's census (`playback_handler.cpp` 48/18,
`instrument_clip_minder.cpp` 54/17, `action_logger.cpp` 54/10).

Caveat: the ratchet counts *lines*, not semantics, so a mechanical reformat that merges two
`display->` calls onto one line silently "improves" it. Minor; the burn-down is a human process
anyway.

### 1.5 Accidental coupling — two real debts

**(a) `deluge::gui::colours` lives inside gfx-grid.** `src/gfx/grid/palette.h:4` opens
`namespace deluge::gui::colours`, and `RGB` itself is still at **global scope**
(`src/gfx/grid/rgb.h:39`). So the leaf-most graphics library exports names into the namespace
of the layer that the whole redesign exists to dissolve, and `ui/grid` code refers to
`gui::colours::black` from inside `deluge::ui::grid` (`src/ui/grid/frame.cpp:53,60`). This is
recorded as deferred in `src/gfx/README.md:12-16` with a real cost (~600 call sites), which is
a defensible reason to defer — but it should be scheduled, not left open-ended, because every
new scene written against `gui::colours::` deepens it. A namespace alias
(`namespace deluge::gfx::grid::colours = deluge::gui::colours;`) plus a rule that *new* code
uses the alias would stop the bleeding for ~zero cost.

**(b) camelCase API inside a snake_case library.** `RGB` keeps `fromHue`, `fromHuePastel`,
`forTail`, `forBlur`, `greyOut`, `adjustFractional`, `blendChannel`
(`src/gfx/grid/rgb.h:84,92,113,125,177,298,338`), while `Canvas` next door is fully snake_case
and `src/ui/**` is uniformly snake_case. The naming migration note
(`naming-snake-case-migration`) allows camelCase only in "compatibility façades"; `RGB` is not a
façade, it is the type. Same root cause as (a) — the 600 call sites — and the same fix window.

Everything else is clean: `deluge::gfx::{oled,grid}`, `deluge::ui::{core-in-`deluge::ui`,oled,grid}`,
`deluge::notify`, `deluge::ui::shell` (planned) are consistent and non-overlapping. camelCase
occurrences under `src/ui/**` are all inside doc comments citing legacy symbols, except the two
legacy-bridge templates in `grid/keyboard.h:175,176,201` which call `layout.renderPads(image)` —
correct, because that is the legacy class's own name, and both bridges are marked for deletion in
phase 6 (`src/ui/README.md:149-159`).

**No cycles.** `ui-oled` and `ui-grid` genuinely never see each other; `ui-core` does not
include either.

---

## 2. The immediate-mode design

### 2.1 Frames as borrowed clipped views — sound, with two asymmetries

`OledFrame` (`src/ui/oled/frame.h:33-135`) and `GridFrame` (`src/ui/grid/frame.h:205-300`) are
aggregates of references built on the stack per tick, owning nothing, with `region()` producing a
translated + intersected child. Both files carry an explicit "must stay an aggregate, nothing
below this line" note (`oled/frame.h:132-134`, `grid/frame.h:293-295`) because `region()` uses
aggregate initialisation — the sort of constraint that gets violated six months later, and it is
written down.

Clipping is honest about its own limits rather than claiming completeness
(`oled/frame.h:66-86`): exact for `pixel/hline/vline/line/invert/clear/rect/rect_rounded`
(`rect_rounded` decomposed into edges + corner points so it clips span-by-span,
`frame.cpp:126-140`), all-or-nothing for `invert_rounded/graphic/icon/icon_centered`, and
right-edge-only for `glyphs`. The `line()` implementation deliberately re-walks Bresenham
through the clipped `pixel()` rather than clipping endpoints, so a clipped line is a strict
subset of the unclipped one (`frame.cpp:75-112`) — a subtle correctness point most ports get
wrong.

Two asymmetries that will surface in scenes:

- `OledFrame::bounds()` returns the **unclipped** size (`oled/frame.h:42-49`, backed by a `size`
  member); `GridFrame::bounds()` returns the **clipped** extent (`grid/frame.h:207-217`). Both
  are documented and both are individually defensible, but a scene that splits rects across the
  two surfaces gets different answers to the same question. Pick one.
- `GridFrame::region()` filters input (`local_events()` is region-relative and `pad()` is
  region-local); `OledFrame::region()` passes `input` through **unchanged**
  (`oled/frame.h:57-64`). So the "panes route their own input with no routing code" property of
  spec B §4 holds on the grid and is simply absent on the OLED. See §2.3.

`GridFrame`'s two recorded deviations from spec B §4 are both correct and both caught real bugs:
the `clip`-vs-`region()` name collision (C++ forbids it), and dropping the copied `events` span
because a span captured at frame-build time cannot see events installed on the snapshot later in
the same tick (`grid/frame.h:193-204`). The cache is keyed on `input.pads.data()/size()` rather
than a "have I run" latch (`grid/frame.cpp:70-93`) for the same reason, and
`tests/spec/ui/grid_frame_spec.cpp` pins the swap.

### 2.2 Free functions over POD state — sound

The shape rule ("`Response` for interactive widgets; `void` for pure draws") is now stated the
same way for both halves (spec B §12, `src/ui/README.md:179-184`), and the `void`-returning grid
columns are the right call: their interaction is *which row*, which a `Response` collapsed over a
column cannot express, and `GridFrame::first_press_in()` (`grid/frame.cpp:95-108`) is the one-line
read-back that closes it.

`Response` after the `velocity`/`index` split (`core/response.h:86-105`) is genuinely improved:
`velocity` is the pad's velocity and nothing else; `index` is a widget-defined selection with a
`-1` sentinel that cannot collide with column 0 or a NUL. The header records *why* — the old
single `uint8_t` had been carrying three incompatible meanings. That is a real bug class removed.

Weak spot: `Response::focused` is never set by `list` (`oled/list.h:100-102` says so explicitly)
and is set by the value widgets from a caller-supplied `focused` parameter. So "who has focus" is
entirely the scene's problem, per-scene, with no shared vocabulary. With ~40 scenes and
multi-pane layouts on the second device that is a lot of duplicated boilerplate. See D1.

`Style` (`core/style.h:253-311`) is the strongest piece of the core: every field is read by at
least one widget (an explicit branch-review rule), and where a profile value coincides with a
legacy metric there is a `static_assert` tying the two (`style.h:307-310`) so a profile edit that
would move pixels fails to compile. Folding `menu_base_y` in — instead of `list`/`output_name`
branching on `Canvas::geometry().height` — closed a genuine hazard (a mismatched
`(Style, Geometry)` pair taking the 64-row branch with the 48-row style).

Three declared-but-unread placeholders survive: `Timer` (`core/state.h:215-222`), `EncoderRole`
(`core/input.h:7-11`), `ListState::editing` (`core/state.h:162-166`), plus `WaveformState`
(`:223-230`) held for a widget deferred to spec E. Each header names the caller it is waiting for
and says "delete it if none appears". Acceptable; they are 12 bytes total.

### 2.3 `InputSnapshot` — correct semantics, unenforceable contract

Edge vs level is right and is the thing most immediate-mode ports get wrong:
`select_pressed`/`back_pressed` are **edges**, `shift` and `held_pads` are **levels**
(`core/input.h:55-72`). The reasoning is spelled out — `list`, `columns` and `toggle` derive
`Response::entered` straight from `select_pressed` with no latch, and an immediate-mode widget
has no per-caller identity to hang "I already reported this" on — and
`tests/spec/ui/list_spec.cpp` pins the *un-defended* behaviour (two ticks with the flag set →
`entered` twice) so the requirement is visible to whoever writes the shell.

That is the best available answer given the constraints, but it is a contract carried only in a
comment and a test. Nothing in the type system distinguishes an edge field from a level field. A
one-word wrapper (`struct Edge { bool v; }`) would make the shell's obligation a compile error
instead of a comment. Cost S, and it is exactly the kind of thing that is cheap now and
impossible after 40 scenes exist.

`PadMask` (`core/input.h:31-47`) is the right idea (a level to `PadEvent`'s edge, because a press
and its release are many ticks apart) and correctly range-guards both `test` and `set`. Its width
is wasteful — see §5.

### 2.4 State ownership for ~40 scenes — coherent

The story holds: widgets are stateless, scenes own POD, and a test or script can poke `cursor`
directly. `MenuState` in the scenes plan (`2026-08-27-ui-scenes-menus-1.md:176`) already carries
`ui::ListState list[8]` (one per menu depth) plus one `ColumnsState`/`ScrollText`/`Blink` — about
70 bytes for the entire menu system, against 269 classes and 402 globals today. That is the
design working.

The one soft spot is the shell's `SceneState` (`2026-08-27-ui-shell.md:290`): an opaque
`alignas(8) uint8_t bytes[256]` with `save_state`/`restore_state` memcpy semantics, one slot per
`SceneKind` (48). No per-scene size assert, no type tag — a scene whose POD grows past 256 bytes
will silently truncate. A `static_assert(sizeof(MyState) <= kSceneStateBytes)` in each scene, or a
templated helper that does it for you, costs nothing and removes the whole failure mode.

### 2.5 `Style` + `Geometry` to the second device — the pair is right, the plumbing is not

`Geometry{width, height, visible_top}` as a runtime value (`gfx/oled/geometry.h:10-17`), applied
in exactly one place (`root_frame` sets `origin = (0, visible_top)`, `oled/frame.cpp:12-21`), is
the correct design and it actually holds: no widget reads `visible_top`.

Two problems for the second device:

1. **One `Style` per board, not per surface.** `BoardProfile{ … const Style* style; }`
   (shell spec §2; `2026-08-27-ui-shell.md:88`) and `render_oled` builds every surface's root
   frame with `*profile_.style` (`:558`). But `Style::menu_base_y` is *panel-height dependent*
   (14 at 48 rows, 15 at 64 — `core/style.h:269-273`, `core/metrics.h:339-340`), and the shell
   plan's own test board mixes a 128×48 and a 128×64 panel (`:219, :652`). A mixed-geometry
   board therefore draws one of its menus one row off. `Style` must be selected per surface
   (per `Geometry`), not per board. This is D4 below.
2. **`columns` deliberately does not follow `menu_base_y`.** `kHorizontalMenuBaseY = 14` on every
   panel (`core/metrics.h:341-348`) because `HorizontalMenu::renderMenuItems` spells a literal
   with no height branch, while `list` follows 14/15. On the shipped 48-row Deluge they
   coincide; on a 64-row panel the horizontal and vertical menus sit one row apart *for no
   reason other than a legacy literal that never ran at 64*. See §4.

### 2.6 `Frames` / `SurfaceRole` (spec D) — reconcile before D

- Spec B §4 declares `Frames { std::span<oled::OledFrame> oled; grid::GridFrame& grid; LedFrame& leds; }`
  as a **ui-core** type. It does not exist in `src/ui/core` (`src/ui/README.md:98-109` lists
  every core type; `Frames` is absent). The shell plan defines its own instead
  (`2026-08-27-ui-shell.md:291`): `std::array<OledFrame*, kMaxOled> oled; GridFrame* grid;` —
  different container, pointers instead of references, and **no `leds` member** (LEDs go through
  the separate `Scene::leds(LedFrame&)` call). Either fix spec B or accept the shell's shape and
  amend; do not leave two published shapes.
- Worse: the shell plan calls `scene->render(f)` **once per OLED surface** with only that
  surface's slot populated (`:558`), and again for the grid pass with only `f.grid` set. So
  `render(Frames&)` is invoked N+1 times per tick with a partially-populated struct, and every
  scene must null-check every pointer it touches. Spec D §7 reads as one call. This is a real
  API wart that ~40 scenes will each pay for; decide now (per-surface `render_oled(role,
  OledFrame&)` + `render_grid(GridFrame&)` virtuals would be cleaner than a sparse `Frames`).
- `SurfaceRole` correctly lives in the shell, not the toolkit — the toolkit does not need it.

---

## 3. Concurrency and real-time

### 3.1 `DirtySet` — correct, and correctly reasoned

The memory-ordering argument in `dirty_set.cpp:41-68` is right, and I checked it independently:

- Raiser: `clip_words_[s][w].fetch_or(relaxed)` then `bits_.fetch_or(release)`
  (`dirty_set.cpp:33-39`) — the release orders the word write before the flag.
- Drainer: 16 × `clip_words_.exchange(relaxed)` then `bits_.exchange(acq_rel)`
  (`:49-54`). The **release half** of the `acq_rel` exchange forbids the preceding relaxed
  exchanges from being reordered after it, so the drainer's word-take is genuinely ordered
  before its flag-take.
- Consequence: the only observable interleaving is *flag set, word not yet taken* — and
  `drain()` derives `ClipContent`/`NoteRows` from the words it just took (`:61-66`), so a word
  can never appear without its flag. The residual case (flag set, empty clip set) is documented
  as "unknown — treat as all dirty" (`dirty_set.h:54-60`): over-invalidation, never loss.

`tests/spec/notify/dirty_set_spec.cpp:34-59` exercises this with a real second thread and asserts
the invariant on *every* snapshot (`!clips.test(7) || has(ClipContent)`), not on average. That is
a genuine concurrency test.

Rule 1 (no pointers, only session-clip indices) is honoured and is the right call: a stale index
after a delete over-invalidates and can never dereference freed memory
(`dirty_set.h:30`, `clip.cpp:807-817`).

**Cost issue:** `drain()` unconditionally performs 16 atomic `exchange` RMWs on the clip words
*every* pass, even when nothing is dirty (`dirty_set.cpp:49-53`), and returns a 132-byte
`DirtySet` by value. On the RZ/A1 each `exchange` is an LDREX/STREX pair. `drain_notifications()`
runs from `doAnyPendingUIRendering()`, which is called far more often than once per 10 ms. A
`std::atomic<uint32_t> clip_summary_` OR'd in `raise_clip` would let `drain()` early-out on
`bits_.load(relaxed) == 0 && clip_summary_.load(relaxed) == 0` — and a raise landing right after
that check is picked up on the next drain, which is already the documented contract. Cost S.

Minor: `raise()` uses `release` on a `fetch_or` with nothing prior to publish (`:30`) — harmless,
but it is a needless barrier on the hottest raise path. `raise_clip`'s release is the one that
matters.

### 3.2 `NoticeQueue` — the COOP-WRITE claim is honestly scoped but load-bearing

`push()` (`notice_queue.cpp:14-25`) on a full ring advances `tail_` (release) and then writes
`ring_[head % cap]` by plain struct copy. Under real preemption a concurrent `pop()` reading the
slot that advance just released can tear. The header says so, in the right place, and names the
replacement (`notice_queue.h:26-31`, spec C §6's SPSC queue). That is the correct way to ship a
known-conditional primitive.

But note what the condition actually is: `push` is documented `[any]` (`:34`) and both `[audio]`
and `[task]` producers exist (spec C §6, ~24 audio-raised sites). It is safe **only** because the
Deluge's audio work runs inside `deluge_app_render` under cooperative scheduling. If audio ever
moves to a real preemptive context — which is the direction of travel in
`target_architecture.md` §4.2 — this becomes a live data race, not a theoretical one. Worth a
`static_assert`-shaped tripwire or at least a mention in the audio-threading spec.

Two smaller things:
- `push()` returns `!overwrote` (`:24`) — i.e. **false on success-with-loss**, which reads
  backwards at every call site and is ignored by all four raisers
  (`notice_queue.cpp:37-68` never check it). Either drop the return or make it
  `bool pushed_without_loss`.
- `size()` (`notice_queue.h:37`) does two independent acquire loads; a concurrent push+pop can
  make it report a value transiently outside `[0, capacity]`. Only used by tests today.

### 3.3 What runs on the audio path

Good: `raise()` is one `fetch_or`; `raise_clip()` is two. No allocation, no lock, no UI-stack
walk, no virtual call — a strict improvement over legacy's `uiNeedsRendering()`, which walked the
UI stack calling virtual `renderMainPads()` from audio context.

Three exceptions to flag:

1. **`Clip::notify_content_changed()` does an O(n) linear search on the audio path.**
   `clip.cpp:809` calls `currentSong->sessionClips.getIndexForClip(this)`, which is
   `std::ranges::find` over the clip array (`clip_array.h:58-61`). It is called from
   `Clip::posReachedEnd` (`clip.cpp:837`), i.e. inside the sequencer tick tree. Still cheaper
   than what it replaced, but a cached `int16_t session_index` on `Clip` (maintained by the
   array) would make it O(1) and remove the `currentSong` read from `[audio]` entirely.
2. **`AudioEngine::cull_alert_pending` is a plain non-atomic `bool`.**
   `audio_engine.h:199` (`inline bool`), written from `[audio]` (`audio_engine.cpp:500`),
   read-and-cleared from `[task]` (`notify_drain.cpp:122-123`). Benign on a single-core in-order
   Cortex-A9, but it is a cross-context flag in a subsystem whose entire premise is that
   cross-context facts are atomic. Make it `std::atomic<bool>` with `exchange(false)`, or better,
   give it a `Dirty` bit.
3. **`count_in_visual()` is polled, not raised**, returning a non-atomic `int32_t`
   (`playback_handler.h:173`) written from the tick tree and read every drain
   (`notify_drain.cpp:97-118`). The README acknowledges the polling (`src/notify/README.md:69-71`);
   it does not acknowledge the non-atomicity. Same fix shape.

Both (2) and (3) are unconditional polls on every drain, which is also why the drain cannot early
out on `d.any()`.

### 3.4 Allocation and blocking

Verified clean. Nothing in `src/gfx` or `src/ui` calls `new`, `malloc`, `std::vector` or
`std::string`; `canvas.cpp:534` even carries a comment recording where a `std::vector` was
removed ("gfx does not allocate in drawing paths"). The toolkit is heap-free by construction.

One exception, in gfx: `DrawLineOptions::point_callback` is
`std::optional<std::function<void(Point)>>` (`gfx/oled/canvas.h:43`), consumed at
`canvas.cpp:213-214`, with exactly one caller
(`src/deluge/gui/menu_item/filter/filter_container.h:101`). `std::function` construction is not
guaranteed allocation-free, and it pulls `<functional>` into every gfx consumer. Replace with a
`void (*)(void*, Point) + void* ctx` pair or a non-owning function-ref. Cost S.
(`RGB`'s `requires std::convertible_to<UnaryOp, std::function<…>>` at `rgb.h:103,278` is a
concept check only — no runtime cost, but it is why `<functional>` is included at all.)

### 3.5 The drain's real-time behaviour

Covered in §1.3: the pad-output-space early-out starves the drain, and the two hidden statics.
The `pendingUIRenderingLock` re-entrancy guard is correct and the ordering rationale is written
down. `RepaintGate` (`core/repaint_gate.cpp:133-151`) uses signed-difference comparisons for both
arming and firing, so it is wraparound-correct across the 32-bit ms rollover — checked.

One `RepaintGate` design note: `needs_frame(now, had_input, model_dirty)` takes `had_input` as a
single global bool, and the shell plan passes the same `had_input_` to every surface
(`2026-08-27-ui-shell.md:549`). So any pad press repaints every OLED panel on a three-panel
device. Cheap today (one panel); worth routing per-surface before spec H.

---

## 4. Parity strategy

**Verdict: a real regression net.** This is the strongest part of the work.

### 4.1 Why it is not a self-golden

The critical property is that goldens are not the only oracle. `OledRig` carries a **second
canvas** (`tests/spec/ui/support/frames.h:58-61`), and specs replay the *legacy call sequence*
onto it through the raw gfx `Canvas`, then assert byte equality with the port's output.
`values_spec.cpp:22-34` states the method explicitly: each widget is pinned three ways — (i)
individual canvas coordinates read off the named legacy renderer with file:line, (ii)
byte-for-byte equality against the legacy call sequence replayed with *every legacy magic number
spelled as a literal* rather than borrowed from the constants the port lays out with, (iii) a
golden PBM. Point (ii)'s parenthetical is the thing that makes it a real oracle: a port that
merely agrees with itself fails. `reference`/`reference_pbm` is used across 8 spec files (68
sites in `values_spec` alone).

`expect_golden` (`tests/spec/gfx/support/golden.h:25-36`) requires `GFX_GOLDEN_UPDATE` to be
exactly `"1"`, not merely set, with the reason written down: re-recording destroys the evidence
that the extraction is behaviour-preserving, and `GFX_GOLDEN_UPDATE=0` must not trigger it. Small
thing, right instinct.

The umbrella spec's "lit-pixel-count tests are not accepted as snapshots" (§11) is honoured — I
found no pixel-count assertions standing in for a golden.

### 4.2 Two geometries — mostly, and the README overstates it

`tests/golden/ui/widgets/` holds 41 `_48.pbm`, 23 `_64.pbm`, 11 `.state`. 18 scenes have a 48
golden with no 64 pair — including **all four `list_*`**, `int_value`, `decimal_value`,
`enum_value_window`, `title`, `toggle_on`, and 4 of 6 `columns_*`: precisely the menu widgets
`MenuScene` is built from and precisely where 14-vs-15 diverges. In fairness the 64 path *is*
asserted, by explicit coordinate assertions through `Rig64` (`list_spec.cpp:57,341,383`,
`columns_spec.cpp:439,475`, `values_spec.cpp:103-113`), and `menu_rect` is compared at both
geometries (`list_spec.cpp:52-58`). So the coverage is real — but
`src/ui/README.md:163-165` claims "Every OLED widget pins legacy geometry … at both 128×48 and
128×64 (paired `*_48.pbm` / `*_64.pbm` goldens)", which is not true of 18 of them. Fix the README
or record the missing pairs (cost S; recording them is the better answer, since the 64 path is
what the second device runs).

### 4.3 Legacy quirks that were reproduced and should instead have been fixed

The team demonstrably *can* deviate when something is clearly a bug: `list` deliberately does not
reproduce legacy's mid-scroll two-pixel corner transient (`oled/list.h:110-117`), and
`tick_squares` drops legacy's dead `channel >= 64` brightening (spec B §7). Both are correct
calls with written reasons. The following got the opposite treatment and, I think, should not
have:

1. **`length_slider`'s `min_pos` defaults to `true`** because every legacy call site inherited
   `= 3` — described in the spec itself as "a near-certain legacy typo, kept verbatim rather
   than corrected" (spec B §6, §12). Reproducing a known typo into a brand-new API's *default
   argument* means every future call site inherits it too. Fix it, update the golden, note it in
   the changelog.
2. **`popup_rect_for` centres on the full 48-row geometry including the 5 bezel-hidden rows**
   (spec B §6/§12), so every popup sits above the visible centre — a visible cosmetic bug on the
   shipped device. On a 64-row panel with `visible_top = 0` the same code centres correctly, so
   the second device will look *different* rather than consistent. This should have become a
   `Style` decision (`popup_centre_on_full_height`) defaulting to the legacy behaviour, not a
   hard-coded rule.
3. **`decimal_value` draws at a canvas-absolute y**, ignoring the frame's origin
   (`oled/values.h`, `values_spec.cpp:56-60` "D4"). This is the one widget that *breaks the frame
   abstraction* — it cannot be placed in a region, and on a 64-row panel it lands at a different
   relative position than `int_value`. Everything else in the toolkit is origin-relative. Make it
   origin-relative and change the golden.
4. **`kHorizontalMenuBaseY = 14` on every panel** vs `list`'s 14/15 (`core/metrics.h:339-348`).
   The 64-row arm of `MenuItem::drawItemsForOled` is *dead code in legacy* (the Deluge panel is
   48). So both numbers are guesses about a panel that has never existed, and the toolkit has
   frozen a one-row inconsistency between the horizontal and vertical menus into the profile
   system that the second device is supposed to inherit. Decide the rule once for a 64-row panel;
   parity at 48 is unaffected either way.
5. **`GreyoutMask::cols` bits run right-to-left** (bit 0 selects the *last* column), matching
   legacy's mirrored numbering in `prepareColour` (spec B §7/§12). A brand-new POD type has no
   reason to inherit a mirrored convention; normalise it here and mirror in the legacy bridge.
   Every future scene author will get this wrong once.
6. **`wrap_lines`' `max_chars_per_line` is read by nothing.** `core/text_wrap.h:51-58` documents
   that no line of the legacy routine ever reads it, and that `popup_spec.cpp` pins that passing a
   different value changes nothing. Carrying a permanently-dead parameter into a new API so call
   sites "can still spell `kPopupMaxCharsPerLine`" is the wrong trade; drop it.
7. **`working()` draws no word and no dots** (spec B §6/§12) — faithful to
   `displayWorkingAnimation`, which only tests `word` to pick a direction and prints nothing. The
   parity call is defensible; the outcome is that the toolkit ships a widget whose entire visible
   output is a corner animation while taking a `word` parameter it never renders. Either drop the
   parameter or implement the brief.

Correct calls I would **not** change: `Apple7` = legacy height 9 (`core/metrics.h:374-387` — the
dominant call shape, with the `y_adjust` reasoning); `toggle`'s turn-selects/press-doesn't-flip
semantics (D1/D2 in `values_spec.cpp:36-47`, traced to `toggle.cpp:14-17` and
`sound_editor.cpp:357`); `measure()` returning advance width rather than ink bounds (every legacy
call site is laid out against it); `list` drawing no scrollbar.

### 4.4 Coverage gaps

- `drain_notifications()` itself is untested (§1.3). Its two hidden statics make it untestable
  without refactoring them into a small struct the test can construct.
- `NoticeQueue` tests share the process-wide `notices()` singleton and defensively drain it first
  (`notice_queue_spec.cpp:110-113`) — correct, but a sign the free functions should take a queue
  reference so tests need not touch a global.
- No test covers `RepaintGate` wraparound near 2^32, though the implementation is correct.
- `tests/spec/ui/repaint_gate_spec.cpp` is 58 lines for the one piece of shared timing logic every
  animated widget depends on. Thin.

---

## 5. Footprint

Measured on this host; ARM 32-bit will be smaller wherever a pointer/span is involved.

| Type | Host size | Note |
|---|---|---|
| `gfx::grid::PadGrid` | **1544 B** | `RGB cells_[16][32]` = 1536. Legacy `PadLEDs::image[8][18]` is **432 B** — 3.5× |
| `ui::PadMask` | **128 B** | `uint32_t cols[32]`, 32 rows of bits, for an 18×8 grid |
| `ui::InputSnapshot` | **160 B** | 128 of it `PadMask`; copied by value in `without_turn` |
| `ui::grid::GridFrame` | **192 B** (~168 ARM) | 128 of it the `local_events` cache |
| `ui::LedFrame` | **792 B** | 64 × `pair<LedId, LedState>` (12 B each) + rings |
| `notify::NoticeQueue` | **904 B** | 16 × 56-byte `Notice` |
| `notify::DirtySet` | **132 B** | returned **by value** from every `drain()` |
| `ui::WrappedLines` | **168 B** (~104 ARM) | returned by value |

Against the platform (`linker_script_rz_a1l.ld:65,77`): **3 MB** internal RAM (`RAM012L`) for
code+data and **32 MB** SDRAM. Nothing here is close to a fit problem. Three items are simply
wasteful:

1. **`PadGrid` is 3.5× the legacy image** because `kMaxGridCols = 32` / `kMaxGridRows = 16`
   (`gfx/grid/pad_grid.h:18-19`) are speculative headroom for a device whose grid is
   *specified as the same 8×(16+2)* (umbrella D2). The shell will hold one per surface plus a
   tween `from` snapshot plus `ColumnPair last_sent[16]` (768 B) — call it ~4 KB where legacy used
   ~900 B. Either shrink the capacity to what any planned board needs, or accept it explicitly and
   place it in SDRAM (`PLACE_SDRAM_BSS`, `board_config.h:115`) rather than internal RAM.
2. **`PadMask` is 4× wider than any board needs**, and it is the bulk of `InputSnapshot`, which
   is **copied by value** twice per `enum_value`/`toggle` call by `without_turn`
   (`oled/values.cpp:42-45, 271-272, 300-301`). `uint16_t cols[32]` (rows ≤ 16) halves it; a
   frame-level "does this widget take the turn" flag removes the copy entirely.
3. **`GridFrame::region()` zeroes 128 bytes per nested region.** The cache members carry `= {}`
   (`grid/frame.h:296-299`) and `region()` builds the child by *aggregate initialisation* of the
   first four members — under which the remaining members are value-initialised, i.e. the whole
   128-byte buffer is zeroed, for every region, whether or not `local_events()` is ever called.
   The header even acknowledges the cost ("`GridFrame` is 176 bytes, of which 128 are this, and
   `region()` zero-initialises it for each nested child"). Since aggregate init cannot be told to
   skip it, the fix is to stop being an aggregate (add a constructor) or to hold a pointer to
   per-tick scratch owned by the root frame. Cost S–M.

`kMaxWindowRows` does not exist under this name; the corresponding bounds are `kWrapMaxLines = 8`
(`core/text_wrap.h:10`, legacy `TEXT_MAX_NUM_LINES`), `LedFrame::kMaxEntries = 64`,
`kMaxLocalEvents = 32` (`grid/frame.h:166`) and `Style::menu_rows` (3/5). All are legacy-derived
or generously sized; none is a risk.

**Archive text** (ARM Debug, `src/ui/README.md:207-213`): `ui_core` 1745, `ui_oled` 26137,
`ui_grid` 11168 ≈ **39 KB**. Two caveats the README should state: (a) these are **Debug** figures,
so they are not a Release budget; (b) during phases 4–6 this is *additive* — `gui/menu_item/`
(24k lines) is still linked — so the transitional peak is the number that matters, not the
end-state. The README's decision not to record Debug `.a` byte counts (dominated by debug info,
rots on toolchain bumps) is the right call. Record a Release `text` total once the device link
works again (see `next-build-environment-2026-08` memory).

The shell plan adds ~17 KB more `.bss`: OLED pool 4 × 1024 = 4 KB, `SceneState` 256 × 48 = **12
KB**, `GridSurface::last_sent` 768 B. The `SceneState` pool is the one worth revisiting (spec D
open question §12.1 already flags it) — 48 slots × 256 B for what will realistically be a handful
of *live* replace-navigation targets. A small LRU of 8 slots would be 2 KB.

---

## 6. What the shell/scenes plans will hit first (ranked)

**D1. There is no input-consumption protocol between `Scene::handle()` and widgets in `render()`.**
This is the big one. Widgets consume input during render: `list` applies `f.input.turn`
(`oled/list.cpp:107-108`), `columns` applies it (`columns.cpp:385-386`), `enum_value` and
`toggle` apply it (`values.cpp:248, 288`), and `entered` comes straight from
`select_pressed` (`list.cpp:123`, `values.cpp:309`). Meanwhile the shell routes an
`Encoder{Select,+1}` `UiEvent` to `Scene::handle()` *before* render (`2026-08-27-ui-shell.md:546`),
and the scenes plan says plainly that `MenuScene::handle` will do "`turn` moves the list cursor /
column focus" (`2026-08-27-ui-scenes-menus-1.md:187`). Both will run in the same tick and the
cursor moves **twice per detent**. The only existing mitigation is `without_turn`
(`values.cpp:40-45`) — a *file-static helper* inside `values.cpp`, not public API, that copies the
whole 160-byte snapshot to zero one field. Decide the contract before D: either widgets own
encoder/button input and `handle()` only sees what no widget claimed, or `handle()` owns it and
the shell hands widgets a masked snapshot. Then make the masking a public, cheap frame operation.

**D2. Navigation discovered in `render()` has nowhere to go.** `Scene::render(Frames&)` returns
`void` (`2026-08-27-ui-shell.md:301`), but `entered` is only reported by a widget *during* render
(`list.cpp:123`). Actions are applied in `pump_input`, which runs *before* rendering
(`:546-553`). So a `MenuScene` that learns "the user entered this submenu" during render must
stash it in POD state and act one tick later — and there is no post-render action phase to act
in. Add a post-render `Result`/action drain to the tick, or let scenes push actions into a
shell-owned queue from `render`.

**D3. `render(Frames&)` is called N+1 times per tick with a sparse `Frames`.** The shell plan
calls `scene->render(f)` once per OLED surface with only that surface's slot populated, and again
for the grid pass (`:558`). Spec D §7 reads as one call. Every scene must null-check every frame
pointer, and any grid drawing placed in `render` runs N+1 times. Also `Frames` is specified in
ui-core by spec B §4 but implemented only in the shell, with a different shape and no `leds`
member. Reconcile before the first scene is written.

**D4. `BoardProfile::style` is one `Style` for the whole board.** `Style::menu_base_y` is
panel-height dependent (14/15, `core/style.h:269-273`) and `render_oled` uses `*profile_.style`
for every surface (`:558`) — while the plan's own test board mixes 128×48 and 128×64 (`:219`).
Make `Style` per-surface (chosen with `Geometry`), or put a `Style` in `OledSurface`.

**D5. The `glyphs`/`graphic`/`icon`/`invert_rounded` clip gap bites the first overlay scene.**
`glyphs` clips only to the frame's right edge and the canvas; the three blit/XOR operations are
all-or-nothing (`oled/frame.h:81-86`, `src/ui/README.md:171-178`). Plan D's `PopupScene`,
`ConsoleScene` and `WorkingScene` all draw text inside sub-regions over a transparent composite
(`:453-456`), which is exactly the case where a too-wide string will overdraw the scene beneath.
Thread a clip rect through `Canvas::draw_char` / `draw_graphic_multi_line` first. Cost M, and the
README already calls it "a prerequisite for widgets that draw text or blit inside sub-regions".

**D6. OLED `region()` does not filter input, so focus routing is unbuilt.**
`OledFrame::region()` passes `input` through unchanged (`oled/frame.h:57-64`). Spec B §5 says
"a scene has one current container and forwards `turn` to it" — but there is no mechanism to
forward. Two lists in two panes both consume `turn`. Related to D1; the fix is the same public
masking helper, plus a documented `focused` convention.

**D7. `void`-returning grid components + coordinate spaces.** `sidebar_columns` opens its own
`region{16, 0, 2, rows}` (spec B §7), so a caller wanting the press must call
`first_press_in()` in *root* coordinates, not sidebar-local ones — an easy off-by-16. Combined
with `GridFrame::bounds()` reporting the clipped extent while `OledFrame::bounds()` reports the
unclipped one (§2.1), coordinate-space mistakes are the likeliest early scene bug. Document one
rule and add a spec that pins it.

**D8. l10n bridging is a pair of raw function pointers with a null-means-digits fallback.**
`TextLookup`/`ErrorLookup` (`2026-08-27-ui-shell.md:80-81`) are installed by spec E1
(`2026-08-27-ui-scenes-menus-1.md:62`), and until then the shell shows "the id as digits"
(`:655`). Workable, but every shell-level golden recorded before E1 will need re-recording after
it. Either land a trivial identity/test bridge with stable fake strings, or defer the text-bearing
goldens.

**D9. `MenuScene`'s notification popup goes through `notify::notice_text`** while an `Action::Popup`
also exists (spec D §12.3 leaves the choice open, `2026-08-27-ui-scenes-menus-1.md:187` picks the
notice). Two paths to the same screen is how the current `display->` sprawl started. Pick one in D.

**D10. `SceneState` is an unchecked 256-byte blob.** No per-scene `static_assert`, no type tag,
memcpy semantics (`:290`, `:305`). Add the assert helper before scenes start using it (§2.4).

---

## 7. Top 10 recommendations

Ranked by (risk × cheapness). "Before D" = should land before the shell plan starts, because it
changes an interface ~40 scenes will be written against.

| # | Recommendation | Cost | Before plan D? |
|---|---|---|---|
| 1 | **Define the input-consumption contract** between `Scene::handle()` and render-time widgets, and give it a cheap public form (a `focused`/`takes_turn` flag on the frame, or a shell-supplied masked snapshot — not a 160-byte copy). Promote `without_turn` out of `values.cpp`. Pin it with a spec. (D1, D6) | M | **Yes — blocking** |
| 2 | **Add a post-render action path** so `Response::entered` discovered in `render()` can navigate in the same tick, and settle whether `render(Frames&)` is one call or N+1 with a sparse struct. Reconcile spec B §4's `Frames` with the shell's. (D2, D3) | M | **Yes — blocking** |
| 3 | **Make `Style` per surface, not per board** (`OledSurface` carries its own, chosen with `Geometry`), and decide the 64-row `menu_base_y` / `kHorizontalMenuBaseY` rule once. (D4, §2.5, §4.3 item 4) | S | **Yes** |
| 4 | **Move `drain_notifications()` above the pad-output-space early-out** in `doAnyPendingUIRendering` (`ui.cpp:381-390`) so notices and the count-in poll are not starved — and cannot be *lost* to the 16-deep overwrite-oldest ring — while the pad bus is congested. Hoist the two hidden statics into a resettable struct so the drain becomes testable. | S | No (but do it now — it is a live regression) |
| 5 | **Thread a clip rect through `Canvas::draw_char` / `draw_graphic_multi_line`** so `glyphs`/`graphic`/`icon`/`invert_rounded` clip to the frame. Prerequisite for popup/console overlays. (D5) | M | **Yes** |
| 6 | **Fix the six over-preserved legacy quirks**: `length_slider::min_pos` default, popup centring (make it a `Style` value), `decimal_value`'s canvas-absolute y, `GreyoutMask` bit direction, drop `wrap_lines`' dead `max_chars_per_line`, resolve `working()`'s unused `word`. Update goldens and changelog. (§4.3) | M | Partly — the `Style`/`decimal_value` ones yes, the rest can follow |
| 7 | **Make the two audio-path side channels atomic**: `AudioEngine::cull_alert_pending` → `std::atomic<bool>` with `exchange(false)` (or a `Dirty` bit); the count-in visual → atomic or a fact. Add an early-out to `DirtySet::drain()` via a `clip_summary_` word so the 16 clip-word `exchange`es are skipped when nothing is dirty. Cache the clip's session index so `notify_content_changed()` is not an O(n) `find` on `[audio]`. (§3.1, §3.3) | S | No |
| 8 | **Shrink the three wasteful footprints**: `PadMask` to `uint16_t cols[32]`; `PadGrid`'s `kMaxGridCols/Rows` to what a planned board needs (or place it in SDRAM); stop `GridFrame::region()` zeroing 128 bytes per nested region (give `GridFrame` a constructor, or root-owned scratch). Add per-scene `static_assert(sizeof(State) <= kSceneStateBytes)` and reconsider 48 × 256 B. (§5, D10) | S–M | No (but #8's `GridFrame` change is easier before scenes nest regions everywhere) |
| 9 | **Schedule the two namespace/naming debts**: `RGB` out of the global namespace and the palette out of `deluge::gui::colours`; `RGB`'s camelCase methods to snake_case. Interim at ~zero cost: add `namespace deluge::gfx::grid::colours = deluge::gui::colours;` and require new code to use it. (§1.5) | L (S for the interim) | No |
| 10 | **Close the enforcement gaps**: a ratchet row for "no mutable globals" under `src/gfx`/`src/ui`; a row counting legacy re-dispatch sites in `notify_drain.cpp` so the temporary can only shrink; record the 18 missing `_64` goldens (or fix `src/ui/README.md:163-165`, which currently overstates coverage); replace `DrawLineOptions::point_callback`'s `std::function` with a function-ref. | S | No |

### Closing note

The foundation is better than the specs promised in the places that matter most: the leaf rules
are mechanically enforced rather than asserted; the parity oracles compare against replayed legacy
call sequences rather than against themselves; the `DirtySet` ordering argument is correct and is
tested with a real racing thread; and the spec's own §12 "Amendments" section records every
deviation with a reason, which is the single most useful artifact in the whole set for the next
implementer.

What is missing is not inside the three libraries — it is the *contract between them and the
scene layer above*: who consumes an encoder detent, where a navigation decision made during
painting goes, and whether a scene renders once or once per surface. Those three questions
(recommendations 1–3) are cheap to answer now and expensive to answer after forty scenes exist.
They should be settled before plan D starts.

---

# C++ idiom audit — `src/gfx`, `src/notify`, `src/ui` (+ app glue, specs)

Branch `ui/toolkit`, repo `/home/kate/GitHub/DelugeFirmware-clean`. Read-only audit; the only file
written is this one. `git status` remains clean apart from the pre-existing `release-blockers.md`.

Method: every file in the three libraries read (14 080 lines incl. `gfx/oled/fonts/fonts.c`, which
is generated data and excluded from the judgements below); the app glue read in full; the specs
sampled (support headers + 8 spec files) and grepped across. Evidence gathered by compiling every
new TU with `clang++ -std=c++2c -Wall -Wextra -Wconversion -Wsign-conversion -Wshadow` in a scratch
dir under `/tmp/claude-1000/…`, by running the repo's own `.clang-tidy` over a sample of the new
TUs, by `clang-format --dry-run` over all new sources, and by a per-header self-sufficiency compile.

## 0. Standard and toolchain (what "idiomatic" is measured against)

- `CMakeLists.txt:268` sets `DELUGE_CXX_STANDARD 26`; every new target sets `CXX_STANDARD 26`
  (`src/gfx/CMakeLists.txt:6,9`, `src/notify/CMakeLists.txt:4`, `src/ui/CMakeLists.txt:16,33,50`),
  and `tests/spec/CMakeLists.txt:34,45` matches. So C++23 features are all fair game and C++26 is
  nominally available. Host toolchain here is clang 22; device is `arm-none-eabi-gcc` (toolchain
  v25).
- `std::format` is **not** used on any device path, correctly: the only `#include <format>` in the
  tree is `tests/qemu/spec/fixedpoint_spec.cpp:3`. `ui/oled/detail/int_to_string.h:26` uses
  `std::to_chars` instead, which is the right call for a no-heap, no-locale target.
- `-fno-rtti` is global (`CMakeLists.txt:~253`); exceptions are *not* disabled anywhere I could find,
  yet nothing in the three libraries throws or uses `noexcept` (0 occurrences). Consistent enough,
  but see §4.
- Style is enforced by `.clang-format` (LLVM base, tabs, 120 cols) and `.clang-tidy` (a broad
  `readability-*`/`modernize-*`/`bugprone-*`/`cppcoreguidelines-*` set,
  `readability-identifier-naming` with `FunctionCase/VariableCase = lower_case`,
  `GlobalConstantCase = camelBack`). `scripts/tasks/task-format.py:203` formats `src` and `tests`
  recursively, so the new directories are covered; there is no `.clang-format-ignore`.

## 1. Language use

### What is done well

- **Views over ownership, throughout.** `std::span` appears 48 times and `std::string_view` is the
  universal string type. `Canvas` (`gfx/oled/canvas.h:60`) is a span over caller storage;
  `WrappedLines::lines` (`ui/core/text_wrap.h:23`) are views into the caller's string with the
  lifetime spelled out in the doc comment; `ListRow`, `Column`, `ClipRowData` all take spans.
- **Geometry as a value, not a macro.** `Geometry` (`gfx/oled/geometry.h:10-17`) with `constexpr`
  accessors replaces `OLED_MAIN_WIDTH_PIXELS`/`OLED_MAIN_TOPMOST_PIXEL`; `OwnedCanvas<G>`
  (`canvas.h:304-313`) is a non-type-template-parameter-of-class-type — a genuine C++20 idiom used
  for exactly the right reason (compile-time-sized aligned storage). `Rig48`/`Rig64`
  (`tests/spec/ui/support/frames.h:64-65`) then test both panels from one template.
- **Designated initialisers** are used where they earn their keep: `ui/core/style.h:46-70`,
  `ui/grid/sidebar.h:50-61`, `ui/grid/frame.cpp:84-89`, `gfx/grid/rgb.h:75`.
- **`static_assert`s that pin cross-layer invariants**, not decoration: `ui/core/style.h:75-78` ties
  each shipped profile field to the legacy metric it reproduces so a profile edit fails to compile
  rather than moving pixels; `ui/oled/values.h:35-38` bounds the stack window against every
  profile's `menu_rows`; `notify/dirty_set.h:24` bounds the enum against the 32-bit mask;
  `gfx/grid/rgb.h:357` pins trivial copyability.
- **`enum class` with a fixed underlying type** for every new enum (`Dirty`, `Notice::Kind`,
  `FontId`, `Align`, `Hint`, `FilterKind`, `PopupStyle`, `NoteFill`, `NoteSquare`, `MacroKind`,
  `TickColour`, `LaunchStyle`, `GestureEvent`, `KeyboardLayoutKind`, `EncoderRole`, `RingId`) —
  with one exception, §1.2 below.
- `std::optional` used correctly for genuinely-absent values (`RepaintGate::deadline_`,
  `GridFrame::first_press_in`, `text_keyboard_key_colour`, `waveform_full`'s `rgb`).
- Structured-binding-free but `auto` discipline is tight: `auto` appears mostly as
  `const auto px = static_cast<uint8_t>(...)` at cast sites, which is the good use.
- `std::clamp`/`std::min`/`std::max`/`std::fill` used rather than hand loops in 37 places.

### Where it is not idiomatic

1. **Type punning and a C cast in the hot blit.** `gfx/oled/canvas.cpp:606`:
   `uint32_t data = *(uint16_t*)graphic_pos;` — a C-style cast, a strict-aliasing violation and a
   potentially unaligned 16-bit load. `std::memcpy` into a `uint16_t` (or `std::bit_cast` over a
   `std::array<uint8_t,2>`) compiles to the same instruction on ARM and is defined. Legacy code,
   but it now lives in a brand-new leaf library that claims to be portable.
2. **One unscoped enum survives.** `gfx/oled/canvas.h:34-37`:
   `enum BorderRadius : uint8_t { SMALL = 0, BIG = 1 };` leaks `SMALL`/`BIG` into
   `deluge::gfx::oled`, and `ui/oled/frame.h:96` has to spell `gfx::oled::SMALL` as a default
   argument. Every other enum in the new code is `enum class`.
3. **`std::to_underlying` (C++23) is never used** — 0 occurrences — although the repo has its own
   `util::to_underlying` (`src/deluge/gui/l10n/language.h:33`) and the new code hand-casts in at
   least eight places: `notify/dirty_set.h:61,66`, `notify/dirty_set.cpp:30,36,62,65`,
   `ui/core/led_frame.h:33,34,38,46`.
4. **`std::function` inside a library that forbids the heap.**
   `gfx/oled/canvas.h:43`: `std::optional<std::function<void(Point)>> point_callback`. `src/gfx/README.md`
   says "No heap allocation in drawing paths"; `std::function` allocates whenever the callable
   exceeds the SBO. The single call site
   (`src/deluge/gui/menu_item/filter/filter_container.h:90-101`) captures three references by
   reference, which happens to fit libstdc++'s 16-byte SBO — but nothing enforces that, and the
   `std::optional` wrapper is redundant since `std::function` already has an empty state. The rest
   of the toolkit already uses the right pattern: `Column::render` is
   `void (*)(OledFrame&, bool, void*)` plus a `void* ctx` (`ui/oled/columns.h:47-48`).
5. **`std::function` as a concept.** `gfx/grid/rgb.h:103-104` and `:278-279`:
   `requires std::convertible_to<UnaryOp, std::function<channel_type(channel_type)>>`. This drags
   `<functional>` into every TU that sees `RGB` (i.e. all of `ui/grid`), and rejects move-only
   callables for no reason. `std::invocable<UnaryOp, RGB::channel_type>` is the C++20 spelling and
   needs only `<concepts>`. (This is a moved legacy file, not new code — but it is now the
   foundation header of the grid library.)
6. **Raw C arrays where `std::array` is a drop-in.** `notify/notice_queue.h:20,40`,
   `ui/core/text_wrap.h:23-24`, `ui/core/input.h:35,52`, `ui/core/led_frame.h:18,48,50,51`,
   `gfx/grid/pad_grid.h:65`, `ui/grid/frame.h:155`, `ui/grid/rows.cpp:70-71`,
   `ui/oled/columns.cpp:38,109,328`. Several are uninitialised (`char buf[64];`,
   `Notice ring_[16];`). The same code already prefers `std::array` where someone thought about it
   (`ui/oled/detail/int_to_string.h:25`, `ui/oled/misc.cpp:68`, `ui/oled/values.cpp:109,179`), so
   this is inconsistency rather than policy. `LegacyImage` (`ui/grid/keyboard.h:143`) is a genuine
   exception: it must be a raw `RGB[8][18]` to bind to legacy's virtuals.
7. **`goto` into a `do`-while** in `gfx/oled/canvas.cpp:170` (`goto draw_solid;`) — flagged by the
   repo's own `cppcoreguidelines-avoid-goto`. Legacy, and the only one in the libraries; every
   other legacy `goto` was successfully restructured (`ui/core/text_wrap.cpp:90`,
   `ui/grid/effects.cpp:64`, `ui/grid/sidebar.cpp:143` all carry comments explaining the
   replacement).
8. **A function-like macro in a header-adjacent TU.** `gfx/oled/icons.cpp:3` `DEFINE_ICON(...)`.
   It is confined to one .cpp and does something templates cannot do neatly (deduce `width` from
   `sizeof` of a brace list), so this one is defensible — but `cppcoreguidelines-macro-usage`
   flags it, and a `template <size_t N> consteval Icon make_icon(const uint8_t (&)[N], uint8_t)`
   would do the job. `#define DO_CHARACTER_SCALING 0` (`gfx/oled/canvas.cpp:472`) guarding dead
   code is the weaker case: `if constexpr (kCharacterScaling)` would keep the branch compiled.
9. **`constexpr` vs `inline constexpr` at namespace scope in headers is mixed** — 17 `inline
   constexpr` against ~45 plain `constexpr` (`gfx/oled/font.h:9-16`, `gfx/oled/geometry.h:19,28,29`,
   `gfx/oled/canvas.h:301`, `ui/core/metrics.h:17-59`, all of `gfx/grid/palette.h`,
   `ui/grid/columns.h:41,54,76-78,396`, `ui/grid/rows.h:20,166`, `ui/grid/sidebar.h:50`,
   `ui/grid/keyboard.h:30,45,50,60,61,141,142`). Harmless for scalars; for the object constants
   (`kDelugeOled`, `kDefaultStatusColours`, the whole palette, `kPlayheadColour`, `kVuGreen`) each
   TU that ODR-uses one gets its own `.rodata` copy on a flash-constrained target. Pick one rule.
10. **No `noexcept` anywhere** (0 occurrences) and only 10 `[[nodiscard]]`, all of them in the moved
    `rgb.h`. Pure query functions that should carry both: `Geometry::pages/bytes/visible_height`,
    `Rect::right/bottom/contains/centre/inset/intersect`, `Canvas::geometry/bytes`,
    `GridFrame::bounds/peek/first_press_in`, `OledFrame::bounds`, `DirtySet::has/any/clips`,
    `NoticeQueue::size`, `RepaintGate::next_deadline`, `measure`, `paging_of`,
    `list_visible_rows`, `list_window_start`, `text_keyboard_char_at`, `macro_colour`,
    `velocity_colour`, `automation_colour`, `status_colour`, `audition_colour`, `tick_colour`,
    `waveform_col_colour`. `NoticeQueue::push` returning a dropped-oldest bool is the one that
    most wants `[[nodiscard]]` (§4).
11. **No `[[assume]]`, no `consteval`, no `constinit`, no `std::bit_cast`, no `std::byteswap`, no
    ranges/views.** The absence of ranges is defensible (the loops are over `int32_t` pixel
    coordinates, not containers), but a few would read better as algorithms — e.g.
    `ui/core/led_frame.cpp:6-11` is `std::ranges::find_if` written out, and
    `notify/dirty_set.h:31-36` `ClipBits::any()` is `std::ranges::any_of`. `text_height_of`
    (`ui/core/metrics.h:76`) is `constexpr` and could be `consteval`-adjacent; `font_for_text_height`
    (`gfx/oled/font.cpp:21`) returns a reference to a `constexpr` table and could itself be
    `constexpr` (making `char_width_px`/`string_width_px` usable in `static_assert`s, which the
    style pinning would then benefit from).
12. **Signed/unsigned and narrowing.** A scratch compile of the 24 new TUs with
    `-Wall -Wextra -Wconversion -Wsign-conversion -Wshadow` yields **378 warnings**: 264
    `-Wimplicit-int-conversion`, 73 `-Wsign-conversion`, 10 `-Wdeprecated-copy`, 9+7+6 float
    conversions, 2 `-Wsign-compare`, 2 `-Wshadow`, 1 `-Wunused-parameter`. 259 of them are in the
    moved `gfx/grid/rgb.h` (the `transform` lambdas return `int`/`uint32_t` into a `uint8_t`
    channel); the rest are new code. The ones that are real hazards rather than noise:
    - `ui/oled/meters.cpp:103-104,111,120-121,142,148,213,…` — slot geometry computed into
      `uint8_t` locals from `int32_t` `Rect` fields: `const uint8_t bar_start_x = slot.x + 5;`,
      `const uint8_t fill_width = norm * bar_width;`. A slot at x ≥ 251, or any negative x,
      silently wraps. 27 warnings in this file alone.
    - `gfx/grid/tween.cpp:20-40` (12 warnings) — `int32_t` loop variables passed to
      `PadGrid::at(uint8_t, uint8_t)`, which is **unchecked** (§2).
    - `ui/grid/columns.h:50` — `inline constexpr float kVuDbfsForRow[…] = {-30.8, -26.4, …}`: the
      header's own comment argues at length that these must be `float` and not `double`, then
      writes double literals (7 `-Wimplicit-float-conversion`). The narrowing is constant-folded so
      the values are right, but the code contradicts its own comment; `-30.8f` is the fix.
    - `gfx/grid/rgb.cpp:11,46` — `-Wshadow` (a local shadowing a static data member) plus
      `-Wsign-compare`.
    - `gfx/oled/canvas.cpp:385` — `draw_string_centered` never reads its `text_spacing_x`
      parameter (`-Wunused-parameter`); `ui/oled/text.cpp:90-99` documents that quirk but the gfx
      signature does not mark it `[[maybe_unused]]`.
    - `gfx/grid/rgb.h:66` — `constexpr RGB& operator=(const RGB&) = default;` makes the *implicit
      copy constructor* deprecated (`-Wdeprecated-copy`, 10×). Deleting that line restores both.
13. **Hand-rolled where the standard library has it.** `ui/oled/columns.cpp:38-46` re-implements
    "remove spaces" with `std::isspace` (locale-dependent, and `<cctype>` on `char` needs the
    `unsigned char` cast it correctly does); `std::ranges::remove_copy_if` or an explicit `c != ' '`
    would be clearer and matches what `deluge::string::removeSpaces` actually did.

## 2. Ownership and lifetime

The no-heap rule is met, and mostly without contortions.

- `Canvas` borrows a `std::span<uint8_t>` and asserts the size in the constructor body (with a
  comment explaining why it cannot be a member initialiser — `canvas.h:60-65`). That is exactly
  right.
- `OwnedCanvas<G>` (`canvas.h:304-313`) owns aligned storage, deletes copy and assignment, and
  documents the 32-byte DMA alignment. Good. It does not delete *move*, which for a class with an
  internal `std::span<uint8_t>` pointing at its own `store_` would be a dangling-pointer factory —
  the implicit move is suppressed by the deleted copy ctor, so it is safe today, but
  `OwnedCanvas(OwnedCanvas&&) = delete;` would say so.
- `OledFrame` (`ui/oled/frame.h:33-135`) and `GridFrame` (`ui/grid/frame.h:64-159`) are aggregates
  of references built per tick on the stack. Both carry a comment forbidding a private section /
  extra members so `region()`'s aggregate initialisation keeps working — brittle but documented,
  and the alternative (a constructor) would cost nothing; `modernize-use-designated-initializers`
  already fires on `ui/grid/frame.cpp:14`.
- `GridFrame`'s `local_events()` cache is the one real lifetime/correctness hazard, and it is not a
  lifetime bug but a **staleness bug**: `ui/grid/frame.cpp:73` keys the cache on
  `input.pads.data()` and `input.pads.size()`. A shell that refills one fixed `PadEvent` buffer
  each tick — the obvious implementation, and the one the header's own "the shell installs the
  events on the snapshot afterwards" note describes — produces the same `data()` and, whenever two
  consecutive ticks carry the same number of events, the same `size()`. The frame then answers with
  the **previous tick's** events. Tests do not catch it because each spec builds a fresh span
  (`tests/spec/ui/support/frames.h:74-81`). Add `input.now_ms` (or an explicit tick counter) to the
  key, or drop the cache entirely — filtering ≤ 32 events is cheap.
- `WrappedLines` returning views into the caller's string, and `local_events()` returning a span
  into the frame, are both borrowed-view returns; both are documented at the declaration
  (`ui/core/text_wrap.h:14-16`, `ui/grid/frame.h:114-116`). Acceptable, and the alternative would
  cost a copy.
- `Column::ctx` is a `void*` the caller must keep alive across the call (`ui/oled/columns.h:47-48`,
  documented at `:166-168`). This is the right no-heap answer and is used consistently
  (`meter_column`, `text_column`, `toggle_column`). The type-unsafety is real but contained, and
  the `keyboard`/`control_column` templates (`ui/grid/keyboard.h:172-203`) show the alternative
  where the type can be a template parameter.
- `notify`'s two globals (`dirty_set.cpp:6`, `notice_queue.cpp:7`) are function-local-free
  namespace-scope singletons reached through `dirty()` / `notices()`. Fine for the domain; they are
  trivially destructible and `-fno-threadsafe-statics` is on.
- `notify_drain.cpp` uses two *function-local* `static`s for edge detection
  (`:43 static bool was_playing`, `:97 static int32_t shown_count_in`). They are hidden mutable
  state in what reads as a pure dispatcher, and they make the function untestable — the
  `drain_mapping_spec` only covers `legacy_redraw_for`. Lifting them into a small
  `DrainState` struct the caller owns would match the toolkit's own "state lives in a POD the
  caller passes" rule (`ui/core/state.h`).
- No dangling risk found in `region()`: children are built and destroyed inside the body, and
  `std::forward<F>(body)(child)` is correct.

## 3. Naming and consistency

- Namespaces are clean and layered: `deluge::gfx::oled`, `deluge::gfx::grid`,
  `deluge::gfx::grid::detail`, `deluge::notify`, `deluge::ui`, `deluge::ui::oled`,
  `deluge::ui::grid`, `deluge::ui::oled::detail`, `deluge::ui::test`. Anonymous namespaces are used
  for TU-local helpers everywhere (and `gfx/grid/rgb.h:16` explains at length why the *named*
  `detail` namespace was required there instead).
- `k`-prefixed constants are used consistently for new constants. `.clang-tidy` says
  `GlobalConstantCase = camelBack`, which `kFoo` technically satisfies.
- **snake_case is followed for all new function and member names** — `draw_horizontal_line`,
  `char_width_px`, `list_window_start`, `first_press_in`, `mark_row_dirty`, etc. Surviving
  camelCase, all of it inherited:
  - `gfx/grid/rgb.h` — `adjustFractional`, `forTail`, `forBlur`, `greyOut`, `fromHue`,
    `fromHuePastel`, `blendChannel2`, `transform2`, plus camelCase locals throughout. Called from
    new code (`ui/grid/rows.cpp:110`, `ui/grid/effects.cpp:28`, `ui/grid/waveform.cpp:42`). The
    README (`src/gfx/README.md`, "Deferred") already books this as a follow-up and estimates ~600
    call sites — a reasonable deferral, but it means the grid library's most-used vocabulary is the
    one that does not match the house rule.
  - `gfx/oled/icons.h:5-47` — `syncTypeTriplets`, `knobArc`, `submenuArrowBold`, … These are *new*
    declarations of moved data; they are `extern const Icon` at namespace scope, so `camelBack`
    matches `.clang-tidy`'s `GlobalConstantCase` and this is arguably correct as written. Worth an
    explicit decision either way, because `ui/grid/rows.h:166 kPlayheadColour` is the same category
    of thing spelled the other way.
  - `gfx/grid/palette.h` — `deluge::gui::colours` inside the gfx library, referenced from
    `ui/grid/*` as `gui::colours::green`. A `gui::` namespace inside `gfx/` is a layering wart the
    README also books as deferred.
  - `ui/grid/waveform.cpp:19,20,23,26,33,37,52,54` — **new** camelCase locals (`yStart`, `yStop`,
    `colourAmount`, `howMuchThisSquare`, `valueHere`, `colourValue`). Confirmed flagged by the
    repo's own config: `clang-tidy` reports 8 × `readability-identifier-naming` here. Same in
    `gfx/oled/font.cpp:60 charIndex` and `gfx/oled/canvas.cpp` throughout (moved code).
- Header hygiene is good: `#pragma once` on all 37 headers; **all 37 compile standalone** (verified
  by a per-header `-fsyntax-only` TU); include order follows `llvm-include-order` (own header,
  project headers alphabetised, then `<system>`); `clang-format --dry-run` is clean on every new
  file *except* `gfx/grid/rgb.h:320-322` (the `RMat` table, a moved file).
- No `static` functions or variables at header scope; `inline` used correctly for the header-defined
  helpers (`notify/dirty_set.h:73,76`, `gfx/grid/sine_table.h:14,32`, `util/notify.h`).

## 4. Error handling and contracts

The dominant style is **silent clamp / silent no-op**, with the contract written into the doc
comment. That is a coherent choice for a render path, and it is documented far better than most
firmware. The problems are the exceptions to it.

- **Two contracts on one class.** `PadGrid::set` (`pad_grid.cpp:23-29`) and `clear_row`/`fill_rect`
  range-check and no-op, but `at()` and `row()` (`pad_grid.h:39-42`) are unchecked — out-of-range is
  UB. `gfx/grid/tween.cpp` writes exclusively through `out.at(x, y)` with `int32_t` indices that
  narrow implicitly (12 `-Wimplicit-int-conversion`), and never checks the documented precondition
  that `out` shares `dims()` with the inputs (`tween.h:5`). `GridFrame::pad`/`peek` carefully guard
  the same narrowing (`ui/grid/frame.cpp:26,59`) — the low-level class should too.
- **Asserts vanish in Release.** The only four runtime asserts (`canvas.h:63`,
  `pad_grid.cpp:13,14,15`) are `<cassert>`, so CMake's Release `-DNDEBUG` removes them. The
  preconditions they guard (a short span, an oversized grid) are exactly the ones that corrupt
  memory when violated. Either use the firmware's own failure path, or at minimum add
  `[[assume(...)]]` alongside so the optimiser is told, or keep them unconditional — a
  three-instruction check per canvas construction is not a real cost.
- **Dropped values with no signal.** `NoticeQueue::push` returns `false` when it overwrote the
  oldest (`notice_queue.cpp:24`), and all four helpers discard it (`:44,52,60,67`). `LedFrame::set`
  silently drops past `kMaxEntries` (`led_frame.cpp:12-14`). `GridFrame::local_events()` drops
  events past the 32nd (`frame.cpp:81-83`). `wrap_lines` truncates silently at 8 lines and the
  header says outright that "the caller cannot tell truncation from a string that happened to fit"
  (`text_wrap.h:46-49`). Each is individually defensible; collectively there is no counter, no
  `[[nodiscard]]`, and no debug trap anywhere, so a capacity that turns out to be wrong on real
  hardware will present as missing UI rather than as a diagnosable fault.
- **Sentinels.** `kEdge`/`kCentre` (`geometry.h:28-29`) are `INT32_MIN`/`INT32_MIN+1` with an
  excellent rationale comment for why not `-1`; that is a good sentinel. `popup_type == 0` meaning
  "unspecified" is *not*: it collides with the app's `PopupType::NONE == 0` and needs a boundary
  fixup (`notify_drain.cpp:21-23`), which is a bug waiting to be re-introduced by any other
  consumer. `Blink::period_ms` doubling as an "armed" flag (`ui/oled/values.h:119-126`,
  `popup.h:109-112`) is clever but means two widgets sharing one `Blink` silently fight; the header
  says so. `ScrollText::next_step_ms == 0` is disambiguated by the added `text_w_last`/`rect_w_last`
  keys (`ui/core/state.h:30-31`) — a good fix, honestly recorded as a spec deviation.
- **`now_ms` wrap is handled correctly** where it matters: `RepaintGate::request_repaint_after` and
  `needs_frame` compare via `static_cast<int32_t>(a - b)` (`repaint_gate.cpp:7,13`), and
  `gesture()` uses unsigned subtraction with a comment (`effects.cpp:78`). `scroll_text.cpp:53`
  uses a plain `now >= state.next_step_ms`, which is *not* wrap-safe — after ~49.7 days the
  scroller stalls until `now` catches up. Same pattern at `:64`
  (`request_repaint_after(state.next_step_ms - now, now)`), where a wrapped `next_step_ms < now`
  would request a ~49-day deadline. Low severity, one-line fix, and inconsistent with the two
  places that got it right.
- `DirtySet::operator=` does not guard self-assignment (`dirty_set.cpp:18`, flagged by
  `bugprone-unhandled-self-assignment`). Benign as written, but the class is the one place in the
  tree doing hand-rolled atomics, so it should be beyond reproach.
- `raise_clip`'s parameter is `session_clip_index` in the header and `i` in the definition
  (`dirty_set.h:51` vs `.cpp:33`) — `readability-inconsistent-declaration-parameter-name`.
- The memory-ordering reasoning in `dirty_set.cpp:41-68` and the COOP-WRITE caveat in
  `notice_queue.h:26-31` are genuinely excellent: the ordering argument is written out, and the
  known tearing hazard under real preemption is named with the replacement plan. This is the model
  the rest of the error handling should follow.

## 5. Tests

- **Coverage is strong**: 33 spec files, ~7 900 lines, against ~10 700 lines of library. Widgets
  are pinned three ways — against a transcription of the legacy routine, against individually
  named legacy coordinates, and against golden PBM/`.state` dumps — and every OLED widget is
  exercised at both shipped geometries via `Rig48`/`Rig64`. `tests/spec/ui/support/frames.h` is
  reused 274 times; `expect_golden` 64 times. Very little duplication of setup.
- **`expect_golden(self, …)`** (`tests/spec/gfx/support/golden.h:25`) takes `self` as a templated
  first parameter because CppSpec's `expect` macro expands to `self.expect`. The workaround is
  documented and minimal, but it means every call site must remember to pass `self`, and the
  `GFX_GOLDEN_UPDATE == "1"` guard (with its good rationale comment at `:14-17`) rewrites the
  golden *before* comparing, so an update run always passes. That is intentional; a warning printed
  on rewrite would make it harder to leave on by accident.
- **The 28-file exporter boilerplate is the clearest maintainability win available.** Every spec
  under `gfx/`, `ui/` and `notify/` ends with the same 5-line comment plus hand-written
  `int <dir>_<name>_spec(int argc, char** const argv) { return CppSpec::parse(argc, argv)
  .add_spec(x).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE; }`
  (`gfx/canvas_golden_spec.cpp:108-115`, `notify/dirty_set_spec.cpp:62-70`,
  `ui/repaint_gate_spec.cpp:51-58`, and 25 more). One macro in a shared
  `tests/spec/support/spec_main.h` — `#define DELUGE_SPEC(fn_name, spec_var)` — collapses each to
  a single line and puts the explanation in one place. (The alternative, teaching
  `create_specs_driver` to pass the bare stem, is a CMake change with the same effect;
  `tests/spec/CMakeLists.txt:52-66` already computes both forms.)
- **The legacy-transcription oracles** (`namespace legacy` in `ui/meters_spec.cpp:81-428`,
  `ui/popup_spec.cpp:74-359`, `ui/columns_spec.cpp:79-…`, `ui/misc_oled_spec.cpp:60-119`) are the
  right answer given the constraint — the real `Number::`/`HorizontalMenu::` renderers cannot be
  linked into the host spec build because they pull in the model — and they are honest about it:
  the header comments enumerate every deviation from the task brief with a ruling. The cost is
  real: several hundred lines of duplicated legacy arithmetic that will silently drift if the
  legacy source changes, and a transcription error common to oracle and port is invisible. Two
  cheap mitigations: (a) note in each `namespace legacy` block the exact legacy commit the
  transcription was taken from, so a future reader can diff; (b) where the legacy routine *can* be
  linked, link it — `gfx/canvas_golden_spec.cpp` already does exactly that against the real
  `hid/display/oled_canvas/canvas.cpp` (which `tests/spec/CMakeLists.txt:24` puts in
  `deluge_spec`), and that is the strongest test in the set.
- **Vacuity**: I found essentially none. The closest is `notify/dirty_set_spec.cpp:52,58` — `last`
  is drained and then `(void)last;`, and the "no raise is lost" claim is asserted only as "one more
  drain after the raiser stops is empty", which the comment itself concedes is the weak form. The
  concurrency spec is otherwise good (it asserts the real invariant — a clip word never appears
  without its coarse flag — on every one of 20 000 iterations).
- `tests/spec/gfx/support/pbm.h:13` has a brace-less `if` inside the loop, which
  `readability-braces-around-statements` (enabled) would flag; the support headers are not
  namespaced (`page_major_to_pbm`, `grid_to_state` are at global scope) while `frames.h` correctly
  uses `deluge::ui::test`.

## 6. Tooling

- **`.clang-format`**: covers the new directories (`task-format.py:203` formats `src` and `tests`
  recursively; no `.clang-format-ignore`). Verified: **every new file is clean** except
  `src/gfx/grid/rgb.h:320` (a moved file, the `RMat` table).
- **`.clang-tidy`**: `HeaderFilterRegex: "^((?!lib|RZA1|NE10|fatfs).)*$"` includes the new
  directories, and the CI job (`.github/workflows/clang-tidy.yml`) runs over the compile database
  from `./dbt configure`, which includes these targets. Running it locally on the new TUs produces
  real findings, all listed above (`readability-identifier-naming` × 8 in `waveform.cpp`,
  `bugprone-unhandled-self-assignment`, `readability-inconsistent-declaration-parameter-name`,
  `modernize-use-designated-initializers`, `readability-math-missing-parentheses` ×25,
  `modernize-use-std-numbers` in `meters.cpp:84`, `bugprone-narrowing-conversions`,
  `cppcoreguidelines-avoid-goto`, `cppcoreguidelines-macro-usage`). `WarningsAsErrors: false`, and
  the CI action sets `fail_on_problems: false` and `only_affected_lines: true`, so none of this
  blocks.
- **Warning flags do not reach the new libraries.** `DELUGE_COMMON_COMPILE_OPTIONS`
  (`CMakeLists.txt:269-296`) — which carries `-Wall`/`-Wextra`/`-Wpedantic` under `MAINTAINER_MODE`,
  plus `-Werror=switch`, `-Warray-bounds`, `-Wnull-dereference` and `-Wstack-usage=2000`
  unconditionally — is applied at exactly one place: `src/deluge/CMakeLists.txt:70`
  (`target_compile_options(deluge_app PUBLIC ...)`). None of `deluge_gfx_oled`, `deluge_gfx_grid`,
  `deluge_ui_core`, `deluge_ui_oled`, `deluge_ui_grid`, `deluge_notify` gets it. So the newest,
  most-reviewed code in the tree is compiled with the *fewest* warnings of anything in it, and
  `-Werror=switch` — which would be genuinely valuable across the ~15 new `enum class` dispatch
  switches — is not enforced there.
- Layer rules *are* enforced, and well: `scripts/check_gfx_layer.py` is a ctest
  (`src/gfx/README.md`), alongside `check_layer_rules.py` and `check_platform_boundary.py`. That is
  a better-than-average discipline for a firmware repo, and it is why the include graph is as clean
  as it is.

## 7. Top 15 concrete fixes

| # | Where | Fix | Cost |
|---|---|---|---|
| 1 | `src/ui/grid/frame.cpp:73` | `local_events()` cache keyed on `input.pads.data()`+`size()` returns the previous tick's events when the shell refills one fixed buffer with the same event count. Add `input.now_ms` (or a tick counter) to the key, or delete the cache — filtering ≤32 events is free. Add a spec that reuses one buffer across two ticks. | S |
| 2 | `src/gfx/oled/canvas.cpp:606` | `uint32_t data = *(uint16_t*)graphic_pos;` → `uint16_t data; std::memcpy(&data, graphic_pos, sizeof data);` (or `std::bit_cast` over a 2-byte array). Removes a C cast, a strict-aliasing violation and an unaligned load; same codegen on ARM. | S |
| 3 | `src/gfx/oled/canvas.h:43` | Replace `std::optional<std::function<void(Point)>> point_callback` with `void (*point_callback)(Point, void*) = nullptr; void* callback_ctx = nullptr;` — the pattern `ui/oled/columns.h:47-48` already uses. Removes the heap-allocation hazard from a library whose README forbids it and drops `<functional>` from the gfx include graph. One call site: `src/deluge/gui/menu_item/filter/filter_container.h:101`. | M |
| 4 | `src/ui/oled/meters.cpp:103-104,111,120-121,142,148,213,…` | Stop computing slot geometry into `uint8_t` locals from `int32_t` `Rect` fields (`const uint8_t bar_start_x = slot.x + 5;`, `const uint8_t fill_width = norm * bar_width;`). Use `int32_t`, with an explicit `static_cast<int32_t>` where legacy's truncation is the intended behaviour. Clears 27 `-Wconversion`/`-Wfloat-conversion` warnings and a real wrap for slots at x ≥ 251. | M |
| 5 | `src/gfx/CMakeLists.txt`, `src/ui/CMakeLists.txt`, `src/notify/CMakeLists.txt` | Add `target_compile_options(<tgt> PRIVATE ${DELUGE_COMMON_COMPILE_OPTIONS})` to all six new targets (`src/deluge/CMakeLists.txt:70` is the only user today), so `-Werror=switch`, `-Warray-bounds`, `-Wstack-usage` and the `MAINTAINER_MODE` `-Wall -Wextra` apply. Consider `-Wconversion` for these leaf libraries specifically once #4 and #6 land. | S |
| 6 | `src/gfx/grid/rgb.h:66` | Delete `constexpr RGB& operator=(const RGB&) = default;`. It is redundant *and* it deprecates the implicit copy constructor — 10 `-Wdeprecated-copy` warnings across the grid library, from one line. | S |
| 7 | `src/ui/grid/waveform.cpp:19,20,23,26,33,37,52,54` | Rename the camelCase locals (`yStart`→`y_start`, `colourAmount`→`colour_amount`, `howMuchThisSquare`→`how_much_this_square`, `valueHere`→`value_here`, `colourValue`→`colour_value`). These are new code and the repo's own `.clang-tidy` flags all 8. Also initialise `colourAmount` at declaration (`cppcoreguidelines-init-variables`). | S |
| 8 | `src/gfx/grid/pad_grid.h:39-42` + `src/gfx/grid/tween.cpp:8-40` | Make `at()`/`row()` honour the same contract as `set()`: either assert, or keep them unchecked and rename to `at_unchecked()` with the checked pair added. In `tween.cpp`, assert `out.dims()` matches the inputs (the documented precondition at `tween.h:5`, currently unenforced) and use `uint8_t` loop variables so the 12 narrowings go away. | S–M |
| 9 | `src/gfx/oled/canvas.h:34-37` | `enum BorderRadius : uint8_t { SMALL, BIG }` → `enum class BorderRadius : uint8_t { Small, Big }`. Stops `SMALL`/`BIG` leaking into `deluge::gfx::oled` and matches every other enum in the new code. Call sites: `canvas.h:142,281`, `canvas.cpp:241,688`, `ui/oled/frame.h:96,97`, `ui/oled/frame.cpp:147`. | S |
| 10 | `src/notify/dirty_set.h:61,66`, `dirty_set.cpp:30,36,62,65`, `src/ui/core/led_frame.h:33,34,38,46` | Replace `static_cast<unsigned>(d)` / `static_cast<size_t>(r)` with `std::to_underlying` (C++23, `<utility>`). Says what is meant, and cannot silently follow an underlying-type change. | S |
| 11 | `src/notify/notice_queue.h:34` (+ `.cpp:44,52,60,67`), `src/ui/core/led_frame.h:25` | Mark `NoticeQueue::push` `[[nodiscard]]` and either propagate or count the drop in the four `notice_*` helpers; give `LedFrame::set` a `bool` return for the same reason. Silent capacity overflow is currently indistinguishable from "the model never raised it". | S |
| 12 | `src/ui/grid/columns.h:50` | `{-30.8, -26.4, …}` → `{-30.8f, -26.4f, …}` (and `std::array<float, kColumnRows>`). The header spends a paragraph arguing these must be `float`, then writes `double` literals; 7 `-Wimplicit-float-conversion`. Same treatment for the `int32_t[]` tables at `:211,216,299,300` and the `RGB[]` ramps at `:224-229,276-293`. | S |
| 13 | `src/ui/oled/scroll_text.cpp:53,64` | `now >= state.next_step_ms` → `static_cast<int32_t>(now - state.next_step_ms) >= 0`, matching `repaint_gate.cpp:7,13` and `effects.cpp:78`. Removes a ~49-day stall and makes wrap handling uniform across the toolkit. | S |
| 14 | `src/gfx/oled/canvas.h:63`, `src/gfx/grid/pad_grid.cpp:13-15` | These four `assert`s are the only runtime contract checks in the libraries and Release compiles them out (`-DNDEBUG`). Route them through the firmware's own failure path, or keep them unconditional, or pair each with `[[assume(...)]]` so the optimiser at least benefits. A short-span `Canvas` writes off the end of caller memory on the first draw. | S–M |
| 15 | 28 spec files (`tests/spec/gfx/canvas_golden_spec.cpp:108-115`, `tests/spec/notify/dirty_set_spec.cpp:62-70`, `tests/spec/ui/repaint_gate_spec.cpp:51-58`, …) | Replace the copy-pasted exporter + 5-line comment with one macro in a new `tests/spec/support/spec_main.h`: `#define DELUGE_SPEC(fn, spec) int fn(int argc, char** const argv) { return CppSpec::parse(argc, argv).add_spec(spec).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE; }`, and keep the explanation in that one header. ~150 lines deleted, one place to fix when CppSpec changes. | S |

Runners-up, in rough order: `notify_drain.cpp:43,97` function-local `static`s → a caller-owned
state struct (makes the drain testable); `std::function`→`std::invocable` in the `rgb.h` `requires`
clauses (`:103,278`); `[[nodiscard]]`/`noexcept` on the ~25 pure query functions listed in §1.11;
`std::array` for the raw member arrays (§1.6); `inline constexpr` for the header object constants
(§1.9); `DirtySet::operator=` self-assignment guard (`dirty_set.cpp:18`); the `popup_type == 0`
sentinel replaced by a typed enum shared with the app (removing `notify_drain.cpp:21-23`);
`std::ranges::find_if`/`any_of` in `led_frame.cpp:6` and `dirty_set.h:31`; `constexpr`-ifying
`font_for_text_height`/`char_width_px` so text metrics can be `static_assert`ed;
`(void)last` in `dirty_set_spec.cpp:58`.

## 8. Verdict

**Yes — this is idiomatic modern C++, and unusually disciplined firmware C++ at that.** The
architecture is the part that matters most and it is right: geometry and style arrive as values
instead of macros, every surface is a borrowed view (`std::span`, `std::string_view`) over caller
storage, the no-heap rule is met without a single custom allocator or contortion, `enum class` with
fixed underlying types is used throughout, designated initialisers and `static_assert`s tie the
board profiles to the pixel constants they must reproduce, all 37 headers compile standalone, the
layering is machine-checked by a ctest, and the documentation-per-line ratio is the highest in the
repository — including, in `notify/dirty_set.cpp`, a written-out memory-ordering argument and, in
`notice_queue.h`, an honest statement of the tearing hazard that remains. The specs match: 33 files
pinning every widget against a legacy oracle, named legacy coordinates *and* a golden, at two panel
geometries. The gaps are real but shallow and almost all local. Two are worth fixing before this
lands: the `local_events()` cache key, which will hand a component the previous tick's events under
the obvious shell implementation (#1), and the fact that none of the six new CMake targets receives
the project's warning flags (#5) — which is precisely why 378 `-Wconversion`-class warnings, 27 of
them in `meters.cpp` where `int32_t` rect coordinates are silently squeezed into `uint8_t` locals,
have gone unnoticed. Beyond those, the idiom debt is concentrated in the two *moved* legacy files
(`gfx/grid/rgb.h`, `gfx/oled/canvas.cpp`) — the type-punned blit, the `std::function` callback, the
`goto`, and 259 of the 378 warnings — and the README already books the biggest piece of it as a
tracked follow-up. C++23 is used where it pays (`to_chars` instead of `std::format`, concepts,
`std::optional`, NTTP class types); the two conspicuous C++23 features left on the table are
`std::to_underlying` and `[[assume]]`, both cheap. Nothing here needs redesigning.
