# UI toolkit: `ui-core`, `ui-oled`, `ui-grid`

**Date:** 2026-08-26
**Branch target:** `next`
**Status:** design, pending review. Spec B of the UI stack redesign
(`2026-08-26-ui-stack-redesign-design.md` §7); sits on `deluge-gfx-oled` /
`deluge-gfx-grid` (§6, plan `2026-08-26-gfx-extraction.md`).

## 1. Why

Every OLED screen and pad view in the firmware draws by hand against one global canvas
and one global pad image, computes pad→meaning separately in its render and input paths,
and keeps its animation state in a 29-slot global timer table. The toolkit gives scenes
(spec D/E) a small set of **immediate-mode** widgets and components that:

- draw into a *frame* — a borrowed, clipped, translated view of one surface for one tick
  — so the same code renders on a 128×48 panel with a bezel, a 128×64 panel, or a test
  buffer;
- **paint and hit-test in one call** on the grid, so pad→meaning is computed once;
- take **plain data** (string views, ints, spans) and **POD state owned by the scene**, so
  they are model-free, reusable across devices and across the two halves of a scene, and
  golden-testable with literals;
- request repaints through a per-surface gate instead of a timer table.

Decisions carried in from the umbrella spec: immediate mode (D4); OLED and grid
libraries that cannot include each other (D5); components model-free (D6); widgets are
**free functions over a frame**, not frame methods or widget objects.

## 2. Library layout and rules

```
deluge-ui-core   InputSnapshot, Response, RepaintGate, POD state structs, Style, LedFrame
                 deps: gfx-oled (Rect, FontId), gfx-grid (RGB) — types only
deluge-ui-oled   OledFrame + widgets (namespace deluge::ui::oled)      deps: ui-core, gfx-oled
deluge-ui-grid   GridFrame + components (namespace deluge::ui::grid)   deps: ui-core, gfx-grid
```

Rules (CMake `PRIVATE` deps + the layer checker from the gfx plan, extended):

1. `ui-oled` and `ui-grid` never include each other.
2. None of the three includes `notify`, `model`, `libdeluge`, `l10n`, `gui/`, `hid/`.
3. No mutable globals; no heap in any widget; widgets are not templates (only `region`
   bodies are).
4. Every widget takes its state by reference from the caller and reads input only from the
   frame.

## 3. `deluge-ui-core`

```cpp
namespace deluge::ui {

enum class EncoderRole : uint8_t { Select, Horizontal, Vertical, GoldUpper, GoldLower, Tempo, Count };

struct PadEvent { uint8_t x, y; uint8_t velocity; bool pressed; };   // velocity 0 = release

/// Input as the toolkit sees it for one tick. Lowered by the shell; the only input a widget can read.
struct InputSnapshot {
	int8_t turn;                        // Select encoder detents: navigate / move focus
	int8_t edit[2];                     // GoldUpper, GoldLower detents: edit the focused value
	int8_t horizontal, vertical;
	bool select_pressed, back_pressed, shift;
	std::span<const PadEvent> pads;     // this tick's pad events; grid frames filter by region
	uint32_t now_ms;
};

struct Response {
	bool changed = false, focused = false, entered = false;   // OLED widgets
	bool pressed = false, released = false, held = false;      // grid components
	uint8_t velocity = 0;
};

/// One per surface. Widgets with motion call request_repaint_after; navigation calls request_repaint.
class RepaintGate {
public:
	void request_repaint();
	void request_repaint_after(uint32_t ms, uint32_t now_ms);
	bool needs_frame(uint32_t now_ms, bool had_input, bool model_dirty);   // consumes the request
	std::optional<uint32_t> next_deadline() const;
};

// POD state the scene owns and passes by reference.
struct ListState    { int16_t cursor = 0, scroll = 0, rows_last = 0; bool editing = false; };
struct ColumnsState { int8_t focused = 0; uint8_t page = 0; };
struct ScrollText   { int16_t offset = 0; int8_t dir = 1; uint32_t next_step_ms = 0; };
struct Blink        { uint16_t period_ms = 500; uint32_t until_ms = 0; };
struct Hold         { uint32_t down_ms = 0; bool held = false; uint8_t x = 0, y = 0; };
struct Timer        { uint32_t due_ms = 0; bool armed = false; };
struct WaveformState{ int32_t scroll_cols = 0; uint8_t zoom = 0; };

/// Passed by const& into every frame. Chosen per board profile; "menu line count" and
/// "rounded corners" are values here, not globals on the rasteriser.
struct Style {
	int32_t title_height, row_height, inset_x, scrollbar_w, menu_rows;
	bool rounded, invert_highlight;
	gfx::oled::FontId title_font, row_font, value_font, big_font;
};

// LED declarations (surface-agnostic data; composed by the shell).
enum class LedId : uint8_t;   // defined by the board profile (spec D)
enum class RingId : uint8_t { Upper, Lower };
struct LedState { enum class Kind : uint8_t { Off, On, Blink } kind; uint16_t period_ms; uint32_t until_ms; };
struct KnobRing { uint8_t levels[4]; };
class LedFrame {
public:
	void set(LedId, LedState);
	void ring(RingId, KnobRing);
};

} // namespace deluge::ui
```

Encoder semantics: `turn` moves focus/cursor; `edit[]` changes the focused value with **no
modal edit state**. A widget that needs an explicit edit mode (text entry) uses
`ListState::editing`.

## 4. Frames

```cpp
// ui-oled
namespace deluge::ui::oled {
struct OledFrame {
	gfx::oled::Canvas& canvas;
	gfx::oled::Rect clip;        // absolute, on the canvas
	gfx::oled::Point origin;     // local (0,0) in canvas coordinates
	const Style& style;
	const InputSnapshot& input;
	RepaintGate& gate;
	template <class F> void region(gfx::oled::Rect local, F&& body);   // clips + translates; body(OledFrame&)
	gfx::oled::Rect bounds() const;                                    // local {0,0,w,h}
};
}
// ui-grid
namespace deluge::ui::grid {
struct GridFrame {
	gfx::grid::PadGrid& grid;
	gfx::grid::Rect region;                    // pad units, absolute
	std::span<const PadEvent> events;          // all events this tick
	const InputSnapshot& input;
	RepaintGate& gate;
	Response pad(int x, int y, RGB colour);    // paint + hit-test, local coords
	std::span<const PadEvent> local_events() const;   // region-filtered, translated
	template <class F> void region(gfx::grid::Rect local, F&& body);
	gfx::grid::Rect bounds() const;
};
}
// what a scene receives (ui-core)
struct Frames { std::span<oled::OledFrame> oled;   // indexed by surface role
                grid::GridFrame& grid;  LedFrame& leds; };
```

- A region's body sees a frame whose `(0,0)` is the region's corner and whose input is
  filtered to it; panes route their own input with no routing code.
- The shell builds the root `OledFrame` with origin `(0, geometry.visible_top)`; **no widget
  knows about the bezel**.
- `gfx::grid::Rect` is the same shape as `gfx::oled::Rect` in pad units (added to
  `gfx-grid` with the toolkit; a `Rect` per library keeps the two gfx libraries
  independent).

## 5. Focus and navigation

- **No global focus tree.** Focus is per container: `ListState::cursor` for vertical lists,
  `ColumnsState::focused` for the four-slot page. A scene has one current container and
  forwards `turn` to it.
- `list()` clamps `turn` against `rows_last` (last frame's row count) and writes `rows_last`
  before returning — a turn past the end cannot skip on reverse.
- Any navigation (cursor moved, submenu entered, page changed) calls `gate.request_repaint()`
  so the new state renders before the shell blocks on input.
- Motion widgets call `gate.request_repaint_after(ms, now)`; nothing touches a timer table.

## 6. `ui-oled` widget catalogue

Shape: `Response name(OledFrame&, Rect, <data>, <state&>)`. Data is plain; never a model
type. `TextStyle{FontId font; Align align; bool shrink;}` replaces the `textWidth/textHeight`
pairs; `measure(text, font) -> Size` uses ink bounds so centring is visually correct.

| Widget | Replaces | Behaviour |
|---|---|---|
| `title(f, text, underline)` | `drawScreenTitle` | `style.title_font` |
| `text(f, rect, text, TextStyle)` | the `drawString*` family (20+ call shapes) | `align ∈ {Left, Centre, Right}`, `shrink` = shrink-to-fit |
| `scroll_text(f, rect, text, ScrollText&)` | `setupSideScroller` + `scrollingAndBlinkingTimerEvent` | ping-pong when wider than `rect`; `request_repaint_after` |
| `list(f, rect, ListState&, span<ListRow>)`, `ListRow{label, value, const Icon*, selectable}` | `drawItemsForOled`, submenu rendering, browser lists, `invertLeftEdgeForMenuHighlighting` | applies `turn`; keeps cursor visible; highlight per `style.invert_highlight`; draws `scrollbar` when rows > visible; `entered` on select press |
| `scrollbar(f, rect, first, visible, total)` | ad hoc | for non-list views |
| `int_value(f, rect, int32_t&, Range{min,max,step}, ValueStyle)` / `decimal_value(…, places)` | `integer.cpp`, `decimal.cpp`, `number.h` | applies `edit[]` when focused; `changed` |
| `enum_value(f, rect, int32_t& index, span<string_view> names)` | `enumeration`, `selection`, `sync_level`, `type.h`… | wraps; name + arrows |
| `toggle(f, rect, bool&, label)` | `toggle.cpp` | checkbox glyph |
| `bipolar_bar(f, rect, value, range)` / `unipolar_bar(…)` | `param.cpp` variants, `patch_cable_strength` | centre tick for bipolar |
| `knob_arc(f, rect, value, range, bipolar)` / `pan(f, rect, value)` | horizontal-menu slot renderers | |
| `envelope(f, rect, a, d, s, r)` | `envelope/` | ADSR outline |
| `filter_curve(f, rect, cutoff, resonance, kind)` | `filter/` | |
| `waveform(f, rect, WaveformData{peaks, markers}, WaveformState&)` | `sample_marker_editor`, slicer, overview | draws precomputed peak columns |
| `columns(f, rect, ColumnsState&, span<Column>)`, `Column{label, render(OledFrame&, bool focused)}` | `horizontal_menu.cpp` (`Paging`, `SlotPosition`, labels, page dots) | four slots, focus by `turn`, paging |
| `popup_frame(f, rect, PopupStyle)` | `setupPopup`, `drawPermanentPopupLookingText` | border + cleared interior; caller draws content in `f.region` |
| `progress(f, rect, num, den)` / `working(f, rect, word, Blink&)` | loading/working animations | |
| `vu(f, rect, l, r)` | OLED VU | |
| `output_name(f, rect, OutputNameData{type, slot, subslot, name, edited})` | `View::drawOutputNameFromDetails` | |
| `segment_digits(f, rect, span<uint8_t,4>)` | `renderEmulated7Seg` | emulated-display feature only |
| `graphic(f, rect, data, w, h, bytesTall)` / `icon(f, pos, const Icon&)` | `drawGraphicMultiLine`, `drawIcon` | pass-throughs; scenes never touch `Canvas` directly |

## 7. `ui-grid` component catalogue

Shape: `Response name(GridFrame&, <data>, <state&>)`; pad units, region-local.

| Component | Replaces | Notes |
|---|---|---|
| `pad(f, x, y, colour)` | direct `PadLEDs::image[y][x]` writes (58) | the primitive; `{pressed, released, held, velocity}` |
| `row(f, y, span<RGB>)` | `renderRow` ×10 | returns the pressed x if any |
| `clip_row(f, y, ClipRowData{colour, length, playhead, span<NoteCell>})`, `NoteCell{start, len, velocity, flags}` | note-row rendering in `instrument_clip_view` | column units; the caller windows the model |
| `status_pad(f, x, y, StatusPad{colour, muted, armed, soloed, Blink&})` | `drawMuteSquare`, `getClipMuteSquareColour`, clip status | |
| `audition_pad(f, x, y, colour, active)` | `drawAuditionSquare` ×5 | |
| `sidebar_columns(f, span<StatusPad> left, span<StatusPad> right)` | `renderSidebar` ×31 | |
| `velocity_column(f, x, VelocityData)` / `automation_column(f, x, AutomationData{value, bipolar})` | automation bars, velocity pads | |
| `macros(f, x, MacroData)` / `vu_meter(f, x, l, r)` | `View::renderMacros`, `View::renderVUMeter` | |
| `tick_squares(f, span<TickSquare>)` | `PadLEDs::setTickSquares` ×8 | playhead overlay, drawn last |
| `waveform_row(f, y, peaks)` / `waveform_full(f, PeakGrid)` | `WaveformRenderer::renderAsSingleRow/FullScreen` | rendering only; peak scanning (reads `Sample`) stays in `gui/waveform` and moves to `deluge-scenes` |
| `keyboard(f, KeyboardLayout&)` / `control_column(f, x, ControlColumn&)` | `renderPads`, `renderColumn` | adapters over the existing interfaces, which keep their `RGB image[][]` signatures until phase 6 |
| `text_keyboard(f, TextKeyboardState&)` | on-pad QWERTY | |
| `greyout(f, GreyoutMask{cols, rows}, amount)` | `reassessGreyout` ×25, `getGreyoutColsAndRows` | applied after painting; the tween drives `amount` |
| `gesture(f, Hold&, HoldPolicy{short_press_ms}) -> GestureEvent{Tap, Hold, Release, SecondPad{x,y}}` | per-view tap/hold FSMs | clone/copy gestures |

Pad→meaning is computed once: the call that paints a pad reports its press. Multi-pad
state (chords, drags) reads `f.local_events()` and keeps a `Hold`.

## 8. Text and l10n

- Widgets take `std::string_view`. **Scenes resolve `l10n::String`** before calling, so
  `ui-oled` has no l10n dependency and goldens use literals.
- Fonts are `FontId`s chosen by `Style` per role; a 128×64 profile picks larger faces
  without touching widgets.
- Case folding stays in `gfx-oled`.

## 9. Budget and testing

- A frame is ~40 bytes; regions nest by value; no heap anywhere.
- Tests (host, CppSpec, same harness as the gfx plan): for every widget, (a) golden
  PBM/state frames from literal data at 128×48 and 128×64; (b) state-transition tests through
  a null-canvas frame — cursor clamp, scroll-into-view, `edit[]` applied only when focused,
  gate deadlines; (c) `GridFrame::region` routes one pad event to exactly one pane.
- Migration hook: `list`/`int_value`/`enum_value`/`toggle`/`columns` are what `MenuScene`
  (spec E) is built from; their goldens are compared with the legacy `MenuItem` renderers
  per item kind before that kind's table ships.

## 10. Where existing code goes

| Today | Target |
|---|---|
| `OLED::setupSideScroller`, `setupBlink`, `scrollingAndBlinkingTimerEvent` | `scroll_text`, `Blink` + gate |
| `OLED::setupPopup`, `popupText`, `drawPermanentPopupLookingText`, `displayWorkingAnimation` | `popup_frame`, `working`; the popup *scene* lives in the shell |
| `MenuItem::drawItemsForOled`, `renderSubmenuItemTypeForOled`, `drawPixelsForOled` implementations (~50 files) | `list`, `int_value`, `enum_value`, `toggle`, bars, arcs — driven by `MenuNode` tables (spec E) |
| `HorizontalMenu` (`Paging`, `SlotPosition`, `renderColumnLabel`, `renderPageCounters`) | `columns` |
| `View::drawOutputNameFromDetails`, `renderVUMeter`, `renderMacros`, `getClipMuteSquareColour` | `output_name`, `vu_meter`, `macros`, `status_pad` |
| `PadLEDs::setTickSquares`, `reassessGreyout` | `tick_squares`, `greyout` |
| `WaveformRenderer` drawing half | `waveform_row`/`waveform_full` |
| `gui/ui/keyboard/layout/*`, `column_controls/*` | wrapped by `keyboard`/`control_column`; moved as-is |

## 11. Open questions

1. `ListRow::value` as text versus a small variant (`{text | int | toggle}`) so `list` can
   draw inline editors for the settings menu without a submenu — decide when `MenuNode`
   kinds are finalised (spec E).
2. Whether `columns` should host arbitrary widgets (`Column::render` callback, as drafted) or
   a closed set of slot kinds — the callback is more general; a closed set is golden-testable
   per kind. Start with the callback; revisit if slot kinds stay under ~8.
