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
                 deps: gfx-oled (Rect, FontId) — types only
deluge-ui-oled   OledFrame + widgets (namespace deluge::ui::oled)      deps: ui-core, gfx-oled
deluge-ui-grid   GridFrame + components (namespace deluge::ui::grid)   deps: ui-core, gfx-grid
```

Rules (CMake deps + the layer checker from the gfx plan, extended):

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

/// Which pads are down *right now*, as a bitmask -- the level to `PadEvent`'s edge (Task 10).
/// One `uint32_t` per column, bit `y` per row; a press and its release are usually many ticks
/// apart, so `Response::held` cannot be answered from one tick's `PadEvent`s alone.
struct PadMask {
	static constexpr uint8_t kCols = 32, kRows = 32;   // bits per word
	uint32_t cols[kCols];
	constexpr bool test(uint8_t x, uint8_t y) const;   // out-of-range reads as "not held"
	constexpr void set(uint8_t x, uint8_t y, bool down);
};

/// Input as the toolkit sees it for one tick. Lowered by the shell; the only input a widget can read.
struct InputSnapshot {
	int8_t turn;                        // Select encoder detents: navigate / move focus
	int8_t edit[2];                     // GoldUpper, GoldLower detents: edit the focused value
	int8_t horizontal, vertical;
	bool select_pressed, back_pressed, shift;   // the two presses are EDGES; shift is a level
	std::span<const PadEvent> pads;     // this tick's pad events; grid frames filter by region
	PadMask held_pads{};                 // pads down right now, absolute coords; level to `pads`' edges
	uint32_t now_ms;
};

struct Response {
	bool changed = false, focused = false, entered = false;   // OLED widgets
	bool pressed = false, released = false, held = false;      // grid components
	uint8_t velocity = 0;   // the pad's velocity, and nothing else
	int16_t index = -1;     // widget-defined selection index; -1 = none
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
struct ScrollText   { int16_t offset = 0; int8_t dir = 1; uint32_t next_step_ms = 0;
                      uint16_t text_w_last = 0; uint16_t rect_w_last = 0; };  // change detection
struct Blink        { uint16_t period_ms = 500; uint32_t until_ms = 0; };   // until_ms is the phase anchor, not a deadline
struct Hold         { uint32_t down_ms = 0; bool held = false; uint8_t x = 0, y = 0;
                      bool hold_reported = false; };                        // one-shot latch for gesture()'s Hold event
struct Timer        { uint32_t due_ms = 0; bool armed = false; };
struct WaveformState{ int32_t scroll_cols = 0; uint8_t zoom = 0; };

/// Passed by const& into every frame. Chosen per board profile; "menu line count" and
/// "rounded corners" are values here, not globals on the rasteriser.
struct Style {
	int32_t title_height, row_height, inset_x, scrollbar_w, menu_rows, menu_base_y;
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

Button semantics: `select_pressed` and `back_pressed` are **edges** — true only on the tick the
button went down, like `turn`/`edit[]`'s detents and `pads`' events. `shift` is a **level**, like
`held_pads`. The shell lowers the edges and the widgets deliberately do not latch: `list`,
`columns` and `toggle` set `Response::entered` straight from `select_pressed`, so a level here
would enter a submenu once per tick for as long as the encoder was held.

`Response` carries **two** payload fields, with one meaning each. `velocity` is the pad's own
press velocity, everywhere, and nothing else. `index` is a widget-defined selection index, −1 for
none: `row`/`clip_row` report the pressed column x, `text_keyboard` the typed character code.
Each widget names its own meaning in its header.

`FontId::Apple7` is the legacy text height **9** (`kTextSpacingY`), not 7: legacy's
`font_for_text_height` returns a 7-high face only for heights 7-8, and height 9 -- the
dominant call shape in the tree -- selects the `y`-adjusted `kApple7Up` shape. A 7-high id is
added only if a caller is found that needs it. `core/metrics.h` also carries two constants not
in the section 3 code block above: `kTextBigSpacingX = 11` (the 13px font's legacy
`textWidth`) and `kHorizontalMenuBaseY = 14` (the horizontal menu's box top on every panel --
deliberately distinct from `kMenuBaseY48`/`kMenuBaseY64`, which are `list`'s menu-item base row
and differ from it by one on a 64-row panel).

## 4. Frames

```cpp
// ui-core (ui/core/frame_context.h) — landed by the toolkit-fixes tasks (task-9/10 reconciliation
// replaces this section's original draft with the shape that actually shipped)
namespace deluge::ui {
struct InputClaims {         // one flag per widget-owned input, written by claim_*(), read by the shell
	bool turn = false, edit0 = false, edit1 = false, horizontal = false, vertical = false, select = false, back = false;
	bool double_claim = false;   // diagnostic: set when an already-claimed input is claimed again
	bool any() const;
	bool operator==(const InputClaims&) const = default;
};
struct InputMask {            // which inputs a frame may see: focus, per input, not all-or-nothing
	bool turn = true, edit = true, horizontal = true, vertical = true, select = true, back = true;
	static constexpr InputMask all();
	static constexpr InputMask none();
	constexpr InputMask operator&(InputMask o) const;   // intersection: the only way a child mask is built -- narrows, never widens
	constexpr bool is_all() const; constexpr bool is_none() const;
	constexpr bool operator==(const InputMask&) const = default;
};
struct FrameContext {          // one per tick, shared by every frame built over it (spec D §3/§7)
	const InputSnapshot& input; RepaintGate& gate;
	InputClaims claims{}; uint32_t tick = 0; uint32_t tick_ms = 0;
	void begin_tick(uint32_t now_ms);   // clears claims, bumps tick/tick_ms -- called exactly once per tick, before any frame is built
};
struct FrameInput {            // shared base of OledFrame and GridFrame: one copy of the contract, not one per surface
	FrameContext& ctx; InputMask mask = InputMask::all();
	bool focused() const;                                      // mask.is_all() -- the old boolean "focused", recovered
	int8_t turn() const; int8_t edit(int i) const; int8_t horizontal() const; int8_t vertical() const;
	bool select_pressed() const; bool back_pressed() const;      // masked; edges (rule 5)
	uint32_t now_ms() const; bool shift() const;                  // NOT masked (rule 3's clock/modifier exemption)
	void claim_turn() const; void claim_edit(int i) const; void claim_horizontal() const; void claim_vertical() const;
	void claim_select() const; void claim_back() const;            // no-ops when the corresponding mask bit is off
	RepaintGate& gate() const;                                       // unmasked -- an unfocused widget still has to be redrawn
	InputMask child_mask(InputMask m) const; InputMask child_mask(bool granted) const;   // the one place a child mask is built
};
}
// ui-oled
namespace deluge::ui::oled {
struct OledFrame : FrameInput {
	gfx::oled::Canvas& canvas;
	gfx::oled::Rect clip;        // absolute, on the canvas
	gfx::oled::Point origin;     // local (0,0) in canvas coordinates
	gfx::oled::Size size;        // the region's own (unclipped) extent, for bounds()
	const Style& style;
	template <class F> void region(gfx::oled::Rect local, InputMask granted, F&& body);   // clips + translates; body(OledFrame&)
	template <class F> void region(gfx::oled::Rect local, bool granted, F&& body);          // granted ? all() : none()
	template <class F> void region(gfx::oled::Rect local, F&& body);                          // inherits this frame's mask
	gfx::oled::Rect bounds() const;    // local {0,0,w,h}, unclipped size (the region's *requested* extent)
	gfx::oled::Rect visible() const;   // the part of bounds() that survives the parent's clip, same local coordinates
};
}
// ui-grid
namespace deluge::ui::grid {
struct GridFrame : FrameInput {
	gfx::grid::PadGrid& grid;
	gfx::grid::Rect rect;                       // pad units, absolute; the region asked for -- also the local origin
	gfx::grid::Rect clip;                       // rect intersected with the parent's clip
	Response pad(int32_t x, int32_t y, RGB colour);   // paint + hit-test, local coords
	RGB peek(int32_t x, int32_t y) const;             // read back without painting (for greyout)
	std::span<const PadEvent> local_events() const;   // region-filtered, translated; cached per frame
	std::optional<PadEvent> first_press_in(gfx::grid::Rect local) const;
	Response interact(gfx::grid::Rect local) const;   // hit-test without painting, no side effect on pad()'s paint
	const PadMask& held_pads() const;                 // never masked -- positional (rule 4)
	template <class F> void region(gfx::grid::Rect local, InputMask granted, F&& body);
	template <class F> void region(gfx::grid::Rect local, bool granted, F&& body);
	template <class F> void region(gfx::grid::Rect local, F&& body);
	gfx::grid::Rect bounds() const;
	gfx::grid::Rect visible() const;
};
}
```

`Frames` — the `ui-core` aggregate this section originally gave a scene (`std::span<OledFrame>
oled; GridFrame& grid; LedFrame& leds;`) — never shipped and is dropped (task-9/10
reconciliation): it does not exist anywhere in `src/ui/core`, the shell plan defined an
incompatible shape of its own, and a scene invoked once per surface with only that surface's slot
populated (spec D §2.6's audit finding) was worse than one call per surface. What a scene receives
instead is `Scene::render_oled(SurfaceRole, OledFrame&)` / `Scene::render_grid(GridFrame&)` — see
spec D §3, §7.

- A region's body sees a frame whose `(0,0)` is the region's corner and whose input is
  filtered to it — an `InputMask`, not a bare bool (§5) — so panes route their own input with
  no routing code.
- The shell builds the root `OledFrame` with origin `(0, geometry.visible_top)`; **no widget
  knows about the bezel**.
- `gfx::grid::Rect` is the same shape as `gfx::oled::Rect` in pad units (added to
  `gfx-grid` with the toolkit; a `Rect` per library keeps the two gfx libraries
  independent).
- `GridFrame` carries two rectangles, not one: `rect` (the region's *requested* absolute extent,
  and the local origin) and `clip` (`rect` intersected with the parent's clip). `bounds()` reports
  the former, `visible()` the latter translated to local coordinates -- so a region hanging off
  its parent's edge does not shift its own coordinate origin, and a widget that *measures* ("how
  many rows can be seen here" versus "how tall was I asked to be") can tell the two questions
  apart. The rectangle is named `clip`, not `region` as originally drafted: C++ forbids a data
  member and a member function sharing a name, and `region()` is the name components call. There
  is likewise no copied `events` span -- a span captured when the frame is built cannot see events
  the shell installs on the snapshot later that same tick -- so every frame instead reads
  `ctx.input.pads` through the reference it already holds, and `local_events()` filters and
  translates on demand, cached per frame.
- One `FrameContext` per tick carries the `InputSnapshot`, the `RepaintGate` and this tick's
  `InputClaims`; both frame types get their input handling from one shared `FrameInput` base
  (`ctx`, `mask`, the accessors, the `claim_*()` methods, `gate()`) instead of duplicating ~45
  lines of it per surface. **A widget that renders a control consumes and claims the inputs that
  control owns, in the same call** (`f.turn()` then `f.claim_turn()`): the claim records ownership
  for the tick, not that a value arrived -- a focused `list` claims the turn on every frame it
  draws, whether or not a detent came in. A claim is not arbitration: two sibling regions both
  granted an input each still see it, and `InputClaims::double_claim` is the diagnostic for that,
  not a defence -- narrowing which region gets an input at all is `InputMask`'s job (§5).
- The clock (`now_ms()`) and `shift()` are **not masked**: an unfocused region still animates and
  still arms its gate with a real deadline; only the six navigational accessors (`turn`, `edit`,
  `horizontal`, `vertical`, `select_pressed`, `back_pressed`) are. Pad input (`pad()`,
  `local_events()`, `first_press_in()`, `held_pads()`, `interact()`) is never masked either -- it
  is positional, named by where it landed, not by focus.
- `OledFrame`'s clip is now exact for **every** primitive, glyphs and bitmaps included
  (toolkit-fixes task-4 closed the gap this section originally left open as a follow-up):
  `pixel`/`hline`/`vline`/`line`/`rect`/`rect_rounded`/`invert`/`invert_rounded`/`clear`/`glyphs`/
  `glyph`/`graphic`/`icon`/`icon_centered` each open a `ClipScope` that narrows the canvas's own
  clip rect for the call, so a shape that straddles the region boundary draws the part that is
  inside and nothing else, on every edge.
- `GridFrame::interact(rect)` hit-tests a rectangle without painting -- every event inside it in
  one call, `pressed`/`released` accumulating independently and `velocity`/`index` holding the
  last matching event's, mirroring `pad()`'s own last-write-wins loop (toolkit-fixes task-6,
  ported from `deluge-sdk`'s `Frame::interact`).

## 5. Focus and navigation

- **No global focus tree, and now a mechanism to enforce it: `InputMask` (§4).** Focus is per
  container, same as drafted -- `ListState::cursor` for vertical lists, `ColumnsState::focused`
  for the four-slot page -- but "forwards turn to it" is no longer only a convention a scene has
  to get right: a container grants its current child exactly the inputs it may consume
  (`region(rect, InputMask{...}, body)`), and everything else reads zero/false and its claims are
  no-ops. `columns` is the worked example (toolkit-fixes task-5 fix round 2): it keeps the turn
  for itself (`InputMask{.turn = false}` handed to the focused slot) to walk the column focus, and
  grants that slot everything else, so one detent either walks the column focus or edits the
  value, never both -- `InputClaims::double_claim` would flag it if a scene got the split wrong.
- `list()` clamps `turn` against **this frame's own row count**, counted before drawing, not
  last frame's (toolkit-fixes task-8): `rows_last` is published every frame but is now documented
  as a pure cache for a caller that windows against it (`enum_value`), never read back by `list`
  itself, so a list that shrinks or grows under the cursor is correct on the same frame it changes
  rather than one frame late.
- Any navigation (cursor moved, submenu entered, page changed) calls `gate.request_repaint()`
  so the new state renders before the shell blocks on input.
- Motion widgets call `gate.request_repaint_after(ms, now)`; nothing touches a timer table. The
  clock is never masked (§4), so this holds even for a widget drawn in an unfocused region.

## 6. `ui-oled` widget catalogue

Shape: `Response` for interactive widgets; `void` for pure draws (meters, misc, grid columns).
So `Response name(OledFrame&, Rect, <data>, <state&>)` for `scroll_text`, `list`, the four value
widgets and `columns`, and `void name(...)` for `text`, `title`, `scrollbar` and every entry in
`meters.h`, `misc.h` and `popup.h`. Data is plain; never a model
type. `TextStyle{FontId font; Align align; bool shrink; bool truncate;}` replaces the
`textWidth/textHeight` pairs; `truncate` is the `drawStringCentered(startX…)` port (trims from
the end, capped at 24 chars, and centres unconditionally like `shrink`). `measure(text, font)
-> Size` returns the legacy **advance** width -- the sum of per-character spacing, not ink
bounds -- because every legacy centring/right-align call site was laid out against that metric
and ink bounds would shift all of them.

| Widget | Replaces | Behaviour |
|---|---|---|
| `title(f, text, underline)` | `drawScreenTitle` | `style.title_font` |
| `text(f, rect, text, TextStyle)` | the `drawString*` family (20+ call shapes) | `align ∈ {Left, Centre, Right}`, `shrink` = shrink-to-fit |
| `scroll_text(f, rect, s, FontId font, ScrollText&, allow_motion=true, highlight=false, clear_pad_x=0)` | `setupSideScroller` + `scrollingAndBlinkingTimerEvent` | ping-pong when wider than `rect`; the first arm is a `kScrollTime` pause, not a step; `clear_pad_x` reproduces legacy's clear-only `endX += 4` (submenu icon rows); `allow_motion=false` freezes and resets; `request_repaint_after` |
| `list(f, rect, ListState&, span<ListRow>, ScrollText& scroll_label, allow_motion=true)`, `ListRow{label, value, const Icon*, selectable}` | `drawItemsForOled`, submenu rendering, browser lists, `invertLeftEdgeForMenuHighlighting` | applies `turn`; keeps cursor visible; highlight per `style.invert_highlight`; **draws no scrollbar** (neither legacy menu renderer has one -- the caller composes `scrollbar` below); `entered` on select press; `menu_rect(f)` gives the legacy menu rect |
| `scrollbar(f, rect, first, visible, total)` | ad hoc | for non-list views |
| `int_value(f, rect, int32_t&, Range{min,max,step=1}, ValueStyle, focused=true)` / `decimal_value(f, rect, int32_t&, Range, places, int8_t& edit_pos, Blink&, focused=true)` | `integer.cpp`, `decimal.cpp`, `number.h` | applies `edit[]` when focused; `changed`; `decimal_value` keeps its canvas-absolute y (legacy quirk: its digits and cursor are literal canvas rows, not offset by the frame's origin like every other widget -- `f.bounds()` is the rect that reproduces legacy) |
| `enum_value(f, rect, int32_t& index, span<string_view> names, ListState&, ScrollText& scroll_label, wrap=false)` | `enumeration`, `selection`, `sync_level`, `type.h`… | wraps only when `wrap`; **no arrows** -- `Selection::drawPixelsForOled` hands its options straight to the menu-item renderer, which draws labels and the highlight and nothing else |
| `toggle(f, rect, bool& value, off_label, on_label, ScrollText& scroll_label)` | `toggle.cpp` | a two-row `list` (off/on); `turn` flips the value (`setValue(offset > 0)`); a select press reports `entered` and never flips it -- this screen's press goes up a menu level, not into the value |
| `unipolar_bar(f, rect, norm)` / `pan(f, rect, value)` | `param.cpp` variants, `patch_cable_strength` | `pan` draws `unipolar_bar`'s outline plus a centre tick and notches |
| `knob_arc(f, rect, norm)` / `slider(f, rect, norm)` / `length_slider(f, rect, norm, min_pos=true)` | horizontal-menu slot renderers | `length_slider`'s `min_pos` defaults **true**, matching legacy's `= 3` (a near-certain legacy typo, kept verbatim rather than corrected since every call site relied on it) |
| `meter(f, rect, Hint hint, norm, raw, raw_max=50)` | `Number::renderInHorizontalMenu` | dispatches by `Hint` to the widgets above (or `percent`/`sidechain_ducking`/`filter_curve`); `Hint::Ducking` callers must pass `raw_max` |
| `envelope(f, rect, a, d, s, r)` / `envelope_stage(f, rect, norm, StageKind)` | `envelope/`, `Number::drawAttack`/`drawRelease` | `envelope` is spec-driven, with no combined legacy renderer to match (golden only); `envelope_stage` is the single-stage horizontal-menu port |
| `filter_curve(f, rect, norm, FilterKind kind)` | `filter/`, `Number::drawLpf`/`drawHpf` | |
| `waveform(f, rect, WaveformData{peaks, markers}, WaveformState&)` | `sample_marker_editor`, slicer, overview | **deferred to spec E's sample-view scene; `WaveformState` retained for it** — its data shape depends on `WaveformRenderData`, which is model-side. Not implemented in this toolkit |
| `columns(f, rect, ColumnsState&, span<Column>, title_text={})`, `Column{label, slots=1, show_label=true, relevant=true, is_submenu=false, render(OledFrame&, bool focused)}` | `horizontal_menu.cpp` (`Paging`, `SlotPosition`, labels, page dots) | four slots, focus by `turn` over *relevant* columns, paging via `paging_of`; base y is `kHorizontalMenuBaseY` (14) on **both** panels -- `renderMenuItems`'s literal has no panel-height branch, unlike `list`'s; an empty span still draws the title plus four placeholder frames; `title_paged` draws the (possibly abbreviated) title; ready-made `Column::render`s: `meter_column`, `text_column`, `submenu_column`, `toggle_column` |
| `popup_frame(f, rect, PopupStyle)` / `popup_rect_for(f, content, style)` | `setupPopup`, `drawPermanentPopupLookingText` | border + cleared interior; caller draws content in `f.region`; **centres on the whole canvas geometry height** (legacy divides `OLED_MAIN_HEIGHT_PIXELS`, i.e. 48 including the 5 bezel-hidden rows), not on the frame's visible bounds |
| `progress(f, rect, num, den)` | none -- spec-driven | no legacy progress bar exists anywhere in the tree; this is `Number::drawHorizontalBar`'s geometry with `min=0` |
| `working(f, rect, word, Blink&)` | `displayWorkingAnimation` | **no word or dots are drawn** -- legacy only tests `word` to pick a direction, and prints nothing; corner-placed against `rect`'s top-right, legacy's `kWorkingStepMs`/`kWorkingPauseMs` (70 / 350 ms) timing |
| `vu(f, rect, l, r)` | none -- spec-driven | no legacy OLED VU exists (the grid sidebar has one; this does not port it); eight segments per channel with a one-pixel gap between them |
| `output_name(f, rect, OutputNameData{type, slot, subslot, name, edited})` | `View::drawOutputNameFromDetails` | |
| `segment_digits(f, rect, span<uint8_t,4>)` | `renderEmulated7Seg` | emulated-display feature only |
| `graphic(f, rect, data, w, h, bytesTall)` / `icon(f, pos, const Icon&)` | `drawGraphicMultiLine`, `drawIcon` | pass-throughs; scenes never touch `Canvas` directly |

## 7. `ui-grid` component catalogue

Shape: `Response name(GridFrame&, <data>, <state&>)` for the components below a caller presses
directly; the per-column components (`macros`, `velocity_column`, `automation_column`,
`tick_squares`, `vu_meter`, `sidebar_columns`) return `void` instead -- see the open question
after the table. Pad units, region-local.

| Component | Replaces | Notes |
|---|---|---|
| `pad(f, x, y, colour)` | direct `PadLEDs::image[y][x]` writes (58) | the primitive; `{pressed, released, held, velocity}` |
| `row(f, y, span<RGB>)` | `renderRow` ×10 | returns the pressed x in `Response::index` (-1 = none) and that pad's velocity in `Response::velocity` |
| `clip_row(f, y, ClipRowData{colour, tail_colour, blur_colour, notes, playhead_col, overwrite_existing, fill_mode_active, note_editor, blue, red, yellow})`, `NoteCell{start_col (int16_t, may be negative), len_cols, velocity, starts_within, tail, NoteFill fill, non_default_params}` | note-row rendering in `instrument_clip_view` | column units; the caller windows the model; fill-mode/note-editor recolouring lives in `clip_row` itself; the playhead's muted/recording colour variants belong to `tick_squares`, not here (this takes only a column) |
| `status_pad(f, x, y, StatusPad, StatusColours)` / `audition_pad(f, x, y, AuditionPad)` | `drawMuteSquare`, `getClipMuteSquareColour`, `drawAuditionSquare` ×5 | `status_colour`/`audition_colour` are pure functions of the pad structs (no globals read); `StatusPad`/`AuditionPad` carry every field legacy's branches actually read, well beyond the brief's shortlist (see task-11-12-report.md's tables) |
| `sidebar_columns(f, span<RGB> left, span<RGB> right)` | `renderSidebar` ×31 | opens `region{16, 0, 2, rows}` -- the two rightmost columns of the root frame, same coordinate space as everything else |
| `velocity_column(f, x, VelocityData{velocity, NoteSquare square})` / `automation_column(f, x, AutomationData{knob_pos, patch_cable, bipolar, automated, pad_selected})` | automation bars, velocity pads | take PODs, not a `colour`/`min`/`max` the brief sketched: legacy picks from three (or six) fixed ramps by row and square/automation state, and the bounds come from fixed lookup tables, not a caller-supplied range |
| `macros(f, x, span<MacroCell>, selected)` / `vu_meter(f, x, l, r)` | `View::renderMacros`, `View::renderVUMeter` | |
| `tick_squares(f, span<TickSquare{col, TickColour}>, muted_colour)` | `PadLEDs::setTickSquares` ×8 | playhead overlay, drawn last; the `Muted` kind is the plain muted colour -- legacy's `channel >= 64` brightening transform on it is dead code (written but never read on any path that reaches a pixel) |
| `waveform_row(f, y, span<int32_t> brightness /*0..256*/, base, reversed)` / `waveform_full(f, span<PeakCol>, brightness, optional<RGB>)` | `WaveformRenderer::renderAsSingleRow/FullScreen` | rendering only; peak scanning (reads `Sample`) stays in `gui/waveform` and moves to `deluge-scenes`; the brightness span is `int32_t` (legacy's range is 0..256 inclusive, one past a `uint8_t`), not the brief's `uint8_t` |
| `keyboard<Layout>(f, Layout&)` / `control_column<Column>(f, x, Column&, void*)` | `renderPads`, `renderColumn` | templates, not free functions -- the only way to bridge the existing `RGB image[][]` interfaces without `ui/grid` including `gui/`; legacy bridges, deleted in phase 6 |
| `text_keyboard(f, TextKeyboardState&, KeyboardLayoutKind, home_row)` | on-pad QWERTY | `KeyboardLayoutKind` adds `Qwertz` (legacy's table has three rows, not the brief's two); `text_keyboard_char_at` returns `'\b'`/`'\r'` for the backspace/enter pads, which have no table entry of their own |
| `greyout(f, GreyoutMask{cols, rows}, amount)` | `reassessGreyout` ×25, `getGreyoutColsAndRows` | applied after painting; the tween drives `amount`; `cols`' bits run **right to left** (bit 0 selects the last column), matching legacy's own mirrored numbering -- not the brief's "bit 0 dims column 0" |
| `gesture(f, Hold&, HoldPolicy{short_press_ms}, int32_t& second_x, int32_t& second_y) -> GestureEvent{None, Tap, Hold, Release, SecondPad}` | per-view tap/hold FSMs | `Hold` gained a `hold_reported` one-shot latch (ui/core/state.h) so an immediate-mode `Hold` fires its `Hold` event exactly once per press, the way legacy's one-shot timer does |

Pad→meaning is computed once: the call that paints a pad reports its press. Multi-pad
state (chords, drags) reads `f.local_events()` and keeps a `Hold`; "which pad in this rectangle
was hit" is `f.first_press_in(rect)`.

**Closed (final branch review):** `row`, `clip_row`, `status_pad`, `audition_pad` and
`text_keyboard` return a `Response`; `macros`, `velocity_column`, `automation_column`,
`tick_squares`, `vu_meter` and `sidebar_columns` return **`void`, and stay that way**. What those
six carry is *which row*, which a response collapsed over a whole column cannot say; and two of
them (`sidebar_columns`, `tick_squares`) have no interaction of their own to report at all. A
caller that needs the press reads `GridFrame::first_press_in(rect)` -- the one-line scan added
alongside this decision -- or `local_events()` for the full tick.

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

## 12. Amendments during implementation (2026-08-27)

Recorded against the real headers under `src/ui/core`, `src/ui/oled` and `src/ui/grid` as the
toolkit was built. Each line names the change and why it deviates from the draft above.

**§3 core types**
- `ScrollText` gained `text_w_last`/`rect_w_last` — legacy parity: the change-detection keys a
  fresh vs. continuing scroll off the string's pixel width and the rect's width, the way
  `setupSideScroller` detects a new call, rather than off string identity (which POD state
  can't carry).
- `Blink::until_ms` is the blink's phase *anchor*, not a deadline — legacy parity: it is the
  only two-word encoding of legacy's free-running square wave (`OLED::performBlink`) that both
  `decimal_value`'s cursor and `working`'s animation could share.
- `InputSnapshot` gained `PadMask held_pads` — needed state: `Response::held` (a level) cannot
  be derived from one tick's edge-triggered `PadEvent`s alone; the shell already tracks the
  down set and now lowers it here (Task 10).
- `FontId::Apple7` is documented as legacy text height 9, not 7 — legacy parity: height 9 is
  the dominant call shape in the tree and is what `kApple7Up`'s `y_adjust` means; a 7-high id
  is deferred until a caller needs it.
- `core/metrics.h` gained `kTextBigSpacingX` and `kHorizontalMenuBaseY` — legacy parity: values
  the gfx font tables don't carry, needed once `text.cpp`/`columns.cpp` had to name every arm
  instead of spelling a literal.
- `Response` gained `int16_t index = -1`, and `velocity` is now the pad's velocity **only** —
  needed state: `velocity` had been carrying three incompatible meanings (a real velocity from
  `GridFrame::pad`, `row`/`clip_row`'s pressed column as `x + 1`, and `text_keyboard`'s typed
  character), all in one `uint8_t`, so nothing caught a mix-up between them. The two encodings
  moved to `index`, whose −1 sentinel needs no bias and cannot collide with column 0 or a NUL.
- `select_pressed`/`back_pressed` are documented as **edges**, `shift` as a level — needed
  contract, not a code change: three widgets derive `Response::entered` straight from
  `select_pressed` and none of them latches, so a shell lowering a level would enter a submenu
  once per tick. `list_spec` pins the un-defended behaviour (two ticks with the flag set →
  `entered` twice) so the requirement is visible to whoever writes that shell.
- `Style` gained `int32_t menu_base_y` (14 / 15), and every previously unread field
  (`title_height`, `row_height`, `inset_x`, `scrollbar_w`, `big_font`) is now read by the widget
  it describes — needed state: `list`/`menu_rect` and `output_name` were branching on
  `Canvas::geometry().height` instead, which let a mismatched `(Style, Geometry)` pair take the
  64-row branch with the 48-row style. Where a field's value coincides with a `core/metrics.h`
  constant, `core/style.h` carries a `static_assert` tying the two, so a profile edit that would
  move pixels fails to compile. `kHorizontalMenuBaseY` deliberately stays a free constant: it is
  14 on every panel and follows the horizontal menu's rule, not the menu item's.
- `Timer` and `EncoderRole` are documented as spec-declared placeholders that no widget on this
  branch reads, as is `ListState::editing` — noted rather than deleted: each has a named caller
  waiting in spec D/E, and each header now says so plus "delete it if none appears".

**§2 layout and rules**
- The CMake deps are `PUBLIC`, not `PRIVATE` as §2 said — spec wording corrected: `Style` exposes
  `gfx::oled::FontId` and `GridFrame` exposes `RGB`, so both are part of the interface and
  `PRIVATE` would not compile for consumers. `deluge_ui_core` no longer links `deluge_gfx_grid`
  at all: nothing under `core/**` includes it (`PadMask` names `kMaxGridCols` in a comment), and
  the dep was dragging gfx-grid into every ui-core consumer's link closure.
- The `ui-includes-outside` ratchet also matches the slash-free root headers the Global
  Constraints name (`definitions*`, `board_config.h`, `board_layout.hpp`) — its `.*/` alternative
  could not see any of them, which was a hole in exactly the named spot. Baseline stays 0; a
  `#include <cstdint>` is neither slashed nor named, so std headers still never match.

**§4 frames**
- `GridFrame`'s clip rectangle is named `clip`, not `region` — brief drift: the draft names a
  member and a method `region` in the same struct, which C++ forbids; `region()` is the name
  components call.
- `GridFrame` carries no copied `events` span; frames read `input.pads` live and
  `local_events()` filters on demand — needed state: a span captured at frame-build time can't
  see events the shell installs on the snapshot later that tick.
- A `GridFrame` is a **per-tick object** (the header said the shell would build frames once and
  refresh the snapshot) and `local_events()`' cache is keyed on the snapshot's `pads` span, not on
  a "have I run" latch — needed state: the latched cache made a reused frame answer with the first
  tick's events forever, which is exactly the shape `frame.h` was telling the shell to adopt.
  `grid_frame_spec` pins the swap; `GridFrame::first_press_in(Rect)` was added beside it as the
  read-back the `void`-returning column components ask their callers for.
- `OledFrame` gained a `size` member (the region's *unclipped* extent) so `bounds()` can report
  it — needed state: without it, a region hanging off its parent's edge would report a bounds
  rect with a shifted origin.
- `glyphs` clips only to the frame's right edge and the canvas; `invert_rounded`/`graphic`/
  `icon`/`icon_centered` are all-or-nothing; `rect_rounded` is fully clippable — legacy parity
  plus a documented gap: the outline/fill primitives decompose into clipped spans exactly, the
  blits and the XOR area do not (silently overdrawing a sibling region would be worse than
  drawing nothing), and glyph clipping needs a `Canvas::draw_char` change tracked as a
  follow-up, not shipped with this task. **Closed by toolkit-fixes task-4** (below): every
  primitive, blits and glyphs included, is now exactly clipped.

**Toolkit-fixes tasks 4–8 (2026-08-27) — the follow-ups this section tracked, closed**

- **Task 4 — the clip-rect gap above.** `gfx::oled::Canvas` gained `set_clip(Rect)`/
  `clear_clip()`/`clip() -> optional<Rect>`, resolved once into a private `ClipBox` of four
  inclusive ints so no drawing loop reads an `optional`; every routine (pixel/line/rect/circle,
  the string family, icons, `draw_graphic_multi_line`'s byte blitter via a per-page bit mask)
  clips through it. `ui::oled::ClipScope` (`ui/oled/frame.h`) opens one over `clip` for every
  `OledFrame` drawing method; it **intersects**, never replaces (`saved->intersect(region)`), so
  a clip set from outside the frame tree still bounds what a widget draws, and restores (not
  clears) on exit so scopes nest. An empty intersection is set as an empty clip, the canvas's own
  spelling of "draw nothing" — never cleared, which would be exactly the widening this type must
  not do. No golden changed (the port was already drawing inside its bounds); oracled against a
  second, unclipped canvas with the same calls masked to the region by hand.
- **Task 5 — `FrameContext`/`InputClaims`/`InputMask`/`FrameInput`.** Landed as §4 above now
  describes; superseded this section's original `const InputSnapshot& input; RepaintGate& gate;`
  pair on each frame type. `FrameContext::begin_tick(now_ms)` must be called exactly once per
  tick, before any frame is built over it, or claims from the previous tick latch forever.
- **Task 6 — `gfx::grid::Rect::split_rows(at)`/`split_cols(at)`, `inset(d)`; `interact`;
  `composite`.** `split_rows`/`split_cols` are the **two-way** `(before, after)` split at a single
  point `at` — matching `gfx::oled::Rect`'s siblings (`geometry.h:50-51`) and `deluge-sdk`'s
  `Frame::split_rows`/`split_cols` (`imode.rs:367-398`) exactly — not an n-way `(n, i)` form an
  earlier round of this task tried and reverted (no caller existed for it). `GridFrame::interact`
  is documented in §4. `gfx::grid::composite(base, overlay, BlendMode, out)` (new,
  `gfx/grid/composite.h`) ports `deluge-grid-toolkit::layer::BlendMode` (`Replace`, `Overlay`,
  `Tint`); since `PadGrid` has no per-cell transparency channel, black stands in for the SDK's
  `None` everywhere — exact for `Overlay`/`Tint`, and a deliberate adaptation for `Replace`
  (documented on the enumerator: the SDK's `Replace` is gated by transparency alone, this port's
  is gated by "the overlay pad is non-black").
- **Task 7 — the four missing tweens.** `gfx/grid/tween.h` gained `smear_scroll_horizontal`/
  `_vertical` (software sub-pixel scroll, since the device does this in hardware via the PIC and
  the host/second-display path had nothing — ported from `deluge-grid-toolkit`'s
  `animations/smear_scroll.rs`, with its `blend_static` argument order swapped because the SDK's
  own code is a whole pixel out of place at a zero offset), `zoom` (`renderZoom` + its
  `renderZoomWithProgress`/`renderZoomedSquare` helpers, `ZoomParams` incl. `kMaxZoomMagnitude`
  contract — the shift arithmetic is UB past magnitude 5), `explode` (`renderExplodeAnimation`,
  `ExplodeParams` incl. `OccupancyMask` and per-row destinations are not this one's shape),
  `expand_collapse` (`renderInstrumentClipCollapseAnimation`'s shared core, `ExpandCollapseParams`
  incl. `row_to_by_row` — per-row destinations, for `renderNoteRowExpandOrCollapse`'s per-note-row
  `animatedRowGoingTo[]` — and `from_occupancy`/`to_occupancy`, both `OccupancyMask` spans since
  `PadGrid` carries no occupancy channel of its own and "source pad is non-black" is not always
  the right substitute: `View::renderMacros` paints an armed clip's macro pad black on the
  flash-off frame and marks it occupied anyway). An empty `OccupancyMask` keeps the non-black
  default (every existing golden is unaffected); a non-empty one must be exactly `cols()` long
  (`DELUGE_GFX_CONTRACT`, `gfx/contract.h` — see below). Five legacy quirks are reproduced rather
  than fixed in these four functions; `src/ui/README.md`'s "Legacy quirks reproduced on purpose"
  section lists them alongside the six from tasks 6/8/audit.
- **Task 8 — `list`'s two-pass count-then-draw fix.** Documented in §5 above.
- **`DELUGE_GFX_CONTRACT(cond)`** (`gfx/contract.h`, new) — the leaf libraries' runtime
  precondition check: `assert()` compiles out under `-DNDEBUG`, which every shipped build
  defines, so a memory-safety precondition guarded only by `assert()` did not exist on the device.
  This macro is an unconditional `__builtin_trap()` that survives `-DNDEBUG` by construction (no
  `NDEBUG` appears in the file). Used by every precondition these tasks added: `Rect::split_*`'s
  `0 <= i < n`, `composite`'s dimension match, the tween functions' row/column/magnitude bounds.
  Not for a condition a caller can legitimately produce at runtime (clipping, an out-of-range
  model value) — those stay return values.

**§6 OLED widgets**
- `list` takes a `ScrollText&`/`allow_motion` and **draws no scrollbar** — legacy parity:
  neither `drawItemsForOled` nor `drawSubmenuItemsForOled` has one; the draft's catalogue row
  would have broken pixel parity, so the caller composes `scrollbar` itself.
- `measure()` returns the legacy advance width, not ink bounds — legacy parity: every existing
  centring/right-align call site is laid out against the advance metric.
- `TextStyle::truncate` is the `drawStringCentered(startX…)` port (horizontal-menu slot
  labels) — needed state: the draft's `shrink` alone can't express legacy's "trim from the end,
  cap at 24 chars" behaviour, which is a different answer to the same question.
- `toggle` takes `off_label`/`on_label`/`ScrollText&`; a turn flips the value
  (`setValue(offset > 0)`), a select press reports `entered` and never flips it — legacy
  parity: this screen's `selectButtonPress` returns null without touching the value; the
  toggling belongs to the *submenu row* checkbox, not this screen.
- `scroll_text` takes a `FontId`, `allow_motion`, `highlight` and `clear_pad_x`; its first arm
  is a `kScrollTime` pause, not a step — legacy parity: `clear_pad_x` reproduces
  `endX += 4`'s clear-only widening for submenu icon rows without reading the accessibility
  global that drives it in legacy (this layer may not include `notify`).
- `length_slider`'s `min_pos` defaults to **true** — legacy parity: every call site inherited
  legacy's `= 3` (a near-certain typo), so parity wins over "correcting" it.
- `columns`'s base y is `kHorizontalMenuBaseY` (14) on **both** panels, not `list`'s
  `kMenuBaseY48`/`64` pair — legacy parity: `renderMenuItems`'s base-y is a literal with no
  panel-height branch, unlike the menu-item renderer `list` ports.
- Popups (`popup_rect_for`) centre on the whole canvas geometry height, not the visible bounds
  — legacy parity: `setupPopup` divides `OLED_MAIN_HEIGHT_PIXELS` (48, bezel-hidden rows
  included), so a legacy popup sits above the visible centre; reproduced exactly.
- `working` draws no word or dots, corner-placed with legacy's 70/350 ms timing — legacy
  parity: `displayWorkingAnimation` only tests `word` to pick a direction and prints nothing;
  the brief's "'Loading' + animated dots" matches no code in this tree.
- `vu`/`envelope`/`progress` are spec-driven with no legacy OLED equivalent to match — brief
  drift, noted rather than silently invented: golden-tested only. `vu` keeps a one-pixel gap
  between its eight segments per channel.
- **The OLED `waveform` widget is not implemented**; it is deferred to spec E's sample-view scene,
  and `WaveformState` (`core/state.h`) is retained for it — brief drift: its `WaveformData` shape
  depends on `WaveformRenderData`, which is model-side and therefore outside a leaf toolkit that
  may include only `ui/**`, `gfx/**` and the standard library. §6's catalogue row is marked.
  (The *grid* `waveform_row`/`waveform_full` components are unrelated and are implemented.)
- §6's shape line reads "`Response` for interactive widgets; `void` for pure draws" — brief
  drift: the majority of the shipped OLED widgets (`text`, `title`, `scrollbar`, all of
  `meters.h`, `misc.h` and `popup.h`) have nothing to report, and only `scroll_text`, `list`, the
  four value widgets and `columns` return a `Response`. §7 had already documented the same split
  for the grid; this is the OLED half of it, and the two now say the same thing.

**§5/§7 grid components**
- `NoteCell`/`ClipRowData` carry the fields above (signed `start_col`, the three colour fields,
  `fill`/`non_default_params`, etc.) — needed state: legacy's per-square search has already
  happened by the time a `NoteCell` exists, so the outcomes are what survive, not the search
  inputs the draft sketched.
- `status_colour`/`audition_colour` are pure functions, and `StatusPad`/`AuditionPad` carry
  every field legacy's branches read (well past the brief's shortlist) — needed state:
  legacy reaches five-plus globals per pad; parity requires them as fields (task-11-12-report.md
  has the full tables).
- `tick_squares`'s `Muted` kind is the plain muted colour — legacy parity: the brief's
  brightening transform is written in legacy (`setTickSquares`'s fast-cursor branch) but never
  read on any path that reaches a pixel; porting it would be a visible colour change, not
  parity.
- `velocity_column`/`automation_column` take PODs, not a `colour`/range pair — legacy parity:
  the colours come from fixed ramps chosen by row and note/automation state, and the bounds
  from fixed lookup tables, neither of which the brief's signature had anywhere to bind.
- Grid column components (`macros`, `velocity_column`, `automation_column`, `tick_squares`,
  `vu_meter`, `sidebar_columns`) return `void`, not `Response` — brief drift, **closed by the
  final branch review**: `void` stands. Their interaction is *which row*, which a response
  collapsed over a column cannot express, and `sidebar_columns`/`tick_squares` have none of their
  own at all. The read-back is `GridFrame::first_press_in({x, 0, 1, rows})`.
- `sidebar_columns` opens `region{16, 0, 2, rows}` — needed state: the concrete geometry the
  draft's prose implied but didn't spell out.
- `keyboard<Layout>`/`control_column<Column>` are templates — needed state: the only way to
  bridge the existing `gui/ui/keyboard/` classes' `RGB image[][]` interface without `ui/grid`
  including `gui/`; both are deleted in phase 6.
- `Hold` gained `hold_reported`; `KeyboardLayoutKind` gained `Qwertz`;
  `text_keyboard_char_at` returns `'\b'`/`'\r'` for the action pads; `waveform_row`'s
  brightness span is `int32_t`, not `uint8_t`; `GreyoutMask::cols`' bits run right to left —
  all legacy parity: a one-shot hold latch matching legacy's one-shot timer, a third keyboard
  layout legacy's table actually has, two action pads with no table entry of their own, a
  brightness range that is 0..256 inclusive in legacy (one past a `uint8_t`), and legacy's own
  mirrored column numbering in `prepareColour`, respectively.
