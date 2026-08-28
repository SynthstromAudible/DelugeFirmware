# UI stack redesign — target architecture and migration

**Date:** 2026-08-26
**Branch target:** `next`
**Status:** design, pending review; supersedes `target_architecture.md` §6 (UI
architecture) where the two differ. §2–§5 and §7–§11 of that document stand.

## 1. Why

The UI is 97k lines — 46% of `src/deluge` — and has four pieces with a real
boundary (`oled_canvas::Canvas`, `RGB`, `KeyboardLayout`/`ControlColumn`, the
pad row-bitmask dirty protocol). Everything else is a namespace or a global:

- The 269-class `MenuItem` hierarchy (24k lines, 40+ virtuals, three parallel
  render paths) is hard-bound to `OLED::main` and cannot render off-screen or
  into a test buffer. `UI::renderOLED(Canvas&)` does injection right; it never
  propagated downward.
- There is no `Rect`, `Font`, `Style`, layout or component type. Pixel math is
  duplicated per menu item; `menu_item.cpp` contains a compile-time
  screen-height conditional inside layout code. Screen geometry is a macro in
  `RZA1/cpu_specific.h`, included by `canvas.h`.
- `currentSong` is referenced 1,021 times from `gui/`; `PadLEDs` holds a `UI*`;
  `RootUI` receives engine callbacks. Nothing in the UI can be instantiated
  without the whole engine, which is why UI rendering has zero tests.
- 233 `extern`s in `gui/` headers, 402 menu globals, `currentUIMode` as a
  hand-partitioned `uint32_t`, 29 fixed timer slots (so every animation is a
  singleton), three static canvases composited by an if-chain in
  `OLED::sendMainImage()`.
- The five largest view files are 22.7k lines (26% of `gui/`), each mixing
  input, model mutation, pad rendering and OLED rendering.
- `ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE` threads SD-card reentrancy
  through every input handler; `selectEncoderAction` returns `void` and
  cannot bubble.

A second device is coming (§3). Nothing in the current stack can be reused for
it without first being extracted from these globals, so the extraction and the
redesign are the same job.

## 2. Goals and non-goals

Goals:

1. A drawing toolkit and component library with **no globals, no engine
   dependency, host-buildable**, golden-tested byte-for-byte.
2. A shell that turns board capabilities + a board profile into surfaces,
   routes input, composes scenes, and flushes through `libdeluge` — so a new
   frontend is a BSP + a descriptor + tables, with **no scene changes**.
3. Components shared between devices and between the OLED and grid halves of a
   scene without either half knowing about the other.
4. The menu system as data plus one interpreter.
5. A migration that never has a flag day and keeps community PRs mergeable.

Non-goals:

- 7-segment support (removed on `next`; `2026-07-12-remove-7seg-support-design.md`).
- A semantic screen-state vocabulary between scenes and pixels (§6.3 of the
  target architecture). With every target a 1-bpp OLED it buys nothing; scenes
  draw.
- A retained widget tree, partial-redraw invalidation, or a constraint/flex
  layout engine.
- A query/snapshot layer between UI and model (target architecture §2.5:
  views read the model directly and mutate via commands).
- Colour raster or touch input.

## 3. Decisions recorded

| # | Decision | Rationale |
|---|---|---|
| D1 | Build on `next`, above the `libdeluge` C ABI. | The board descriptor (`DelugeBoard`), `deluge_display_blit_oled`, `deluge_control_*` and `deluge_control_poll_event` already exist and are the right seam. `deluge_host` builds from `sim/`. |
| D2 | The future device: several 128×64 mono OLEDs, the same 8×(16+2) pad grid, more buttons, encoders. | Everything stays 1-bpp; "display" becomes a *set* of surfaces; the Deluge bezel offset becomes a per-surface parameter; button identity must be board-provided. |
| D3 | 7SEG stays removed; no presenter/vocabulary layer. | The new device is a better forcing function for multi-frontend design than 7SEG was, and cheaper. |
| D4 | Immediate-mode rendering inside retained scene objects. | Both deluge-sdk toolkits converged on this independently: bindings reference the real fields, so the view cannot desync from the model — the failure mode of the current menu tree. One pass paints *and* hit-tests. Retained state is tiny and app-owned. |
| D5 | OLED and grid components live in separate libraries that cannot include each other. | Enforces reuse across devices and across the two halves of a scene. **Attribution corrected (task-10 reconciliation):** the split, and the immediate-mode frame model `OledFrame`/`GridFrame` port, are `deluge-sdk`'s own — its `deluge-ui-toolkit` (OLED) / `deluge-grid-toolkit` (grid) crates — not spark's. spark has the same two-crate shape (`ui/grid`, `ui/display-oled`) but its grid crate is a thin re-export of `deluge-grid-toolkit` (`ui/grid/src/lib.rs:55`, pinned to `github.com/FirestormAudio/deluge-sdk`) and its OLED crate has no reusable widget library at all — spark is a consumer that has only partly adopted the SDK's split, not its origin. See `docs/superpowers/audits/2026-08-27-abc-audit-reports.md` §0 (provenance correction) and §13 below, which already attributed this correctly. |
| D6 | Components are model-free; views bind model to components; scenes compose views. | Lets the same component serve a different scene arrangement on the new device and be golden-tested with literal data. |
| D7 | LED state is declared per tick and re-derived, not commanded. | Removes the "forgot to clear the LED on exit" bug class; makes flush drops safe. |
| D8 | Scope of this document: umbrella architecture + migration. Each layer gets its own spec and plan; the first is `deluge-gfx-*`. | Too large for one plan. |

## 4. Vocabulary

- **Surface** — one physical output: each OLED, the pad grid, the LED set.
  OLED surfaces are addressed by **role** (`Main`, `Param(n)`, …), assigned by
  the board profile.
- **Scene** — the unit on the navigation stack. Owns one POD state struct,
  handles input, and contributes to *every* surface. `SessionScene`,
  `NoteClipScene`, `MenuScene`, `PopupScene`. Overlays are scenes with
  transparency flags.
- **View** — the part of a scene rendered on one surface: `SessionScene` is
  composed of `SessionGridView` and `SessionOledView`, both reading the scene's
  state. Views are model-aware and surface-specific. A scene on the new device
  adds a `SessionParamOledView` for the extra displays without touching the
  grid view.
- **Component / widget** — model-free, surface-specific building blocks in
  `ui-grid` / `ui-oled`. "Widget" for OLED, "component" for grid, by habit.
- **Frame** — a scene's borrowed, clipped drawing-and-input handle on one
  surface for one tick (`OledFrame`, `GridFrame`, `LedFrame`).
- **Board profile** — the app-side tables for one device: button map, encoder
  roles, surface roles, shortcut-pad map.

The existing global `View view` (`gui/views/view.cpp`, 2,994 lines) is not a
view in this sense; §9.4 maps its contents to shell services and components.

## 5. Library map and dependency rules

```
deluge-gfx-oled    Canvas, geometry, fonts, icons                   deps: util
deluge-gfx-grid    PadGrid, RGB, palette, tweens                    deps: util
deluge-ui-core     UiEvent, UiContext, Response, RepaintGate,       deps: util
                   POD state helpers, Style base, l10n lookup
deluge-ui-oled     OledFrame + OLED widgets                         deps: gfx-oled, ui-core, ui-tokens
deluge-ui-grid     GridFrame + grid components                      deps: gfx-grid, ui-core, ui-tokens
deluge-ui-tokens   palette constants, font data, style constants,   deps: none
                   l10n string tables (today's gui/l10n)
deluge-ui-shell    SurfaceSet, SceneStack, input lowering,          deps: ui-oled, ui-grid, libdeluge,
                   GlobalControls, ModKnobs, MidiLearn, LedFrame,         deluge-commands, notifications
                   tick loop, flush
deluge-scenes      the Deluge's scenes, views, menu tables,         deps: shell, model (read), commands
                   board profiles
```

Rules, enforced by CMake `PRIVATE` dependencies and the layer-rule ratchet
(target architecture §9):

1. `ui-oled` and `ui-grid` never include each other.
2. Nothing below `deluge-scenes` includes `model/`, `playback/`, `processing/`.
3. Nothing below `deluge-ui-shell` includes `libdeluge` or touches hardware.
4. `gfx-*`, `ui-core`, `ui-oled`, `ui-grid` have no mutable globals at all.
5. Every component has exactly one home. The shell and scenes never carry
   private copies of a widget.
6. One `SceneId` type. No parallel enums for per-surface halves.

`hid/display`, `hid/led`, `gui/ui/ui.cpp`, `ui_timer_manager`, `gui/views/view.cpp`
and `gui/menu_item/` dissolve into these targets over the migration (§12).

## 6. `deluge-gfx-oled` and `deluge-gfx-grid`

### 6.1 Canvas

Today's `oled_canvas::Canvas` API is kept in shape (same operations, same parameter order; every name snake_case, per the codebase-wide naming migration) — pixels, lines
(Bresenham with options), rectangles (plain/rounded), circles, strings
(left/centred/right/shrink-to-fit), chars, glyph metrics, multi-line
graphics, icons, `clearAreaExact`, `invertArea`, `invertAreaRounded`. Changes:

- Storage is a caller-provided `std::span<uint8_t>` plus a runtime
  `Geometry{width, height, visible_top}` taken from `DelugeBoard`. The same
  code renders 128×48-with-5-hidden-rows and 128×64. Stays **1-bpp
  page-major**, which is the `deluge_display_blit_oled` wire format — no
  transcode, no second buffer.
- No geometry defaults in signatures (`endX = OLED_MAIN_WIDTH_PIXELS` and
  friends go); callers pass a `Rect`.
- **Naming**: functions, methods, members and locals are `snake_case` in every new
  library (the codebase is migrating from camelCase); types `PascalCase`; constants keep
  the `k` prefix. Legacy camelCase survives only in compatibility façades.
- `hackGetImageStore()` becomes `bytes()`; `roundedCornersEnabled` moves to
  `Style`; the `RZA1/cpu_specific.h` include is removed.
- `visible_top` is applied in exactly one place (the frame's origin), never by
  widgets. The bezel offset and the SSD1309 page offset are different numbers
  and are documented as such.

### 6.2 Geometry, text, icons

- `Point`, `Size`, `Rect` (`split_h`, `split_v`, `inset`, `contains`,
  `centre`), `Insets`. Layout is rect splitting; that is the whole layout
  system.
- `Font` is a handle over the existing five LVGL-shaped bitmap fonts;
  `TextStyle{font, align}`; `measure()` and `draw()`; ink-bounds measurement so
  centring uses lit pixels, not the padded box.
- One `Icon{data, w, h}` type; icons are `constexpr` tables in a registry, not
  statics on the display driver.

### 6.3 PadGrid, RGB, tweens

- `PadGrid`: `RGB` cells, compile-time maximum capacity, runtime
  `{main_cols, sidebar_cols, rows}` from the board. Row dirty bitmask kept.
- `RGB` and `palette` as they are (`constexpr`, functional combinators,
  already host-tested).
- Tweens — fade, scroll, smear-scroll, explode, zoom, expand/collapse — as pure
  `(from, to, t) -> PadGrid` functions replacing the ~20 globals in
  `pad_leds.cpp`. The shell owns timing. Where the surface can animate itself
  (`deluge_control_scroll_*`), the frontend may substitute; the call is the
  same.

### 6.4 Not in gfx

No timers, no input, no l10n, no `display->`, nothing that cannot run in a
unit test.

## 7. Immediate-mode toolkit: `ui-core`, `ui-oled`, `ui-grid`

### 7.1 Frames

```cpp
struct OledFrame {                 // ui-oled
    Canvas& canvas; Rect clip; Point origin;
    const Style& style; const InputSnapshot& input;
    template <class F> void region(Rect local, F&& body);   // clips AND translates
    Rect split_rows(int at); Rect split_cols(int at);
    // widgets are member functions returning Response
};

struct GridFrame {                 // ui-grid
    PadGrid& grid; Rect region; std::span<const PadEvent> events;
    Response pad(int x, int y, RGB colour);                  // paint + hit-test in one call
    std::span<const PadEvent> local_events() const;         // region-filtered, region-local
    template <class F> void region(Rect local, F&& body);
};

struct Response { bool changed, focused, entered; bool pressed, released, held; };
```

A child inside `region()` draws at local (0,0) and only sees input inside its
rect, so panes route their own input with no routing code.

### 7.2 Widgets and components

Widgets are functions on the frame. OLED (`ui-oled`): `title`, `text`,
`scroll_text`, `list` (rows + selection + scrollbar), `int_value`,
`decimal_value`, `enum_value`, `toggle`, `bipolar_bar`, `unipolar_bar`,
`knob_arc`, `pan`, `envelope`, `waveform`, `popup_frame`, `vu`, `progress`,
`output_name`, `segment_digits` (the 7-seg look, for the emulated-display
feature). Grid (`ui-grid`): `clip_row`, `clip_status_pad`, `sidebar_column`,
`velocity_column`, `note_lane`, `automation_column`, `macros_column`,
`vu_meter`, `text_keyboard`, `keyboard` (wrapping the existing
`KeyboardLayout`s and `ControlColumn`s, moved in as-is).

Components take plain data (spans, small structs, callbacks) and POD state.
They never take `Song*`, `Clip*`, or any model type.

### 7.3 Retained state

Widget state is small, POD, owned by the scene, passed by reference:
`ListState{cursor, scroll, rows_last}`, `ScrollTextState{offset, dir,
next_step_ms}`, `BlinkState`, `HoldState` (tap/hold FSM), `MenuState{path[8],
cursor, scroll, editing}`. No widget owns memory; nothing allocates. A test or
script may set `cursor` directly.

Two correctness rules from the SDK are baked in: a turn is clamped against
*last frame's* row count (`rows_last`), and any navigation calls
`request_repaint()` so the new screen renders before the shell blocks on
input.

### 7.4 Style

`Style{fonts, insets, menu_rows, rounded, scrollbar_w, …}` is a struct passed
into the frame. "Menu line count" and "rounded corners" arrive as values, not
globals.

### 7.5 Encoder semantics

Inputs distinguish **Turn** (select encoder: move focus / navigate) from
**Edit(delta)** (gold knobs: change the focused value). There is no modal edit
state unless a widget asks for one; this matches the hardware feel and the SDK
menus.

### 7.6 RepaintGate

One per surface, in `ui-core`:

```cpp
struct RepaintGate {
    void request_repaint();
    void request_repaint_after(uint32_t ms);
    bool needs_frame(uint32_t now, bool had_input, bool model_dirty);
    std::optional<uint32_t> next_deadline() const;
};
```

Widgets with motion (scroll text, blink, tweens) call `request_repaint_after`
instead of touching a timer table. A clean frame is skipped entirely; the
shell may sleep until the earliest deadline.

## 8. `deluge-ui-shell`

### 8.1 Surfaces

`SurfaceSet` is built from `DelugeBoard` (§10) and the board profile:

- N × `OledSurface{framebuffer, Canvas, RepaintGate, role}`.
- One `GridSurface{PadGrid, RepaintGate, last_sent column pairs}`.
- One `LedSurface{bitset, KnobIndicator indicators[], last_sent}`.

Popups, the console, the screensaver and the loading animation are **scenes
on the stack** with transparency flags, not separate canvases.
`OLED::sendMainImage()`'s if-chain does not survive.

### 8.2 Scenes

```cpp
class Scene {
public:
    virtual void render(Frames&)                                         = 0;
    virtual void leds(LedFrame&) {}
    virtual Result handle(const UiEvent&, const UiContext&)              = 0;
    virtual void tick(uint32_t dt_ms) {}
    virtual void on_enter() {}  virtual void on_exit() {}
    virtual void on_model_changed(const DirtySet&) {}
    virtual bool is_grid_overlay() const { return false; }   // dim what's beneath, composite over it
    virtual bool occludes_grid()   const { return false; }   // stop ticking scenes beneath
    virtual bool oled_transparent(SurfaceRole) const { return false; }
};
```

Scenes are statically allocated instances in a registry; the stack is a fixed
`std::array<Scene*, 8>`. Two navigations: **push** keeps the previous scene
live (its animations continue, pop is instant); **replace** saves the scene's
POD state into a per-`SceneId` slot and restores it on return. `SceneId` is a
tagged union carrying arguments (`NoteClip{clip}`, `Kit{track}`,
`Browser{kind}`) — it is the navigation target and the state-cache key. This
reifies `currentUIMode`, `uiNavigationHierarchy`, `getUIType()` and the
`toClipMinder()`/`toTimelineView()` downcasts as one owned object.

A small virtual base with static instances was chosen over `std::variant`:
scenes are few, large and long-lived; virtual dispatch costs nothing
measurable at this rate; and it keeps the `LegacySceneAdapter` (§12) trivial.

### 8.3 Input

`deluge_control_poll_event` → **one** lowering step →

```cpp
struct UiEvent { variant<Pad{x,y,velocity}, Button{ButtonId,on}, Encoder{EncoderRole,delta},
                         EncoderButton{EncoderRole,on}, NoPresses> };
struct UiContext { ButtonSet held_buttons; PadMask held_pads; bool shift, sticky; uint32_t now_ms; };
struct Result   { bool handled; fixed_vector<Action, 8> actions; };
using Action = variant<Push{SceneId}, Replace{SceneId}, Pop, Popup{…}, Command{…}, Repaint{SurfaceRole}>;
```

`ButtonId` and `EncoderRole` are semantic and come from the board profile's
tables, not from matrix coordinates. Routing: modifiers → top scene →
`GlobalControls` (transport, tempo, volume, save/load, learn, sync scaling,
triplets…) → dropped. Actions are applied whether or not the event was
`handled`. Every handler returns `Result`; nothing returns `void`.

SD-card reentrancy: the shell defers input while `deluge::sync::sd_busy()`;
`REMIND_ME_OUTSIDE_CARD_ROUTINE` and `inCardRoutine` never reach a scene.

### 8.4 Services (what `View view` becomes)

- **`GlobalControls`** — the fallthrough handler and a global `LedFrame`
  contributor: Play/Record/Save/Load/Learn/Tap/SyncScaling/Triplets,
  play-enable flashing, time-base LED.
- **`ModKnobs`** — owns the active mod-controllable `ModelStack`, gold-knob
  state, mod region, knob indicators, MIDI-follow feedback; emits param
  commands; pushes a transient `ParamPopupScene`.
- **`MidiLearn`** — the learn mode and its state; scenes report "this pad is
  this learnable thing"; blink via `LedFrame`.
- Preset/output navigation, output-type change, reverb presets → commands in
  `deluge-commands`.
- VU meter, macros, clip-status colours → `ui-grid` components fed by scenes.

### 8.5 Buttons and LEDs

The board profile owns `(matrix x,y) → ButtonId` and `ButtonId →
optional<LedId>`. A button with no LED, an LED with no button (rear sync LED),
or a button absent on one device are table entries. New device with more
buttons = new `ButtonId` values + a profile table; scenes that do not know them
ignore them.

LED state is a value, declared per tick:

```cpp
struct LedState { enum { Off, On, Blink } kind; uint16_t period_ms; uint32_t until_ms; };
struct KnobIndicator { uint8_t levels[4]; };
void SessionScene::leds(LedFrame& f) {
    f.set(Led::Song, On);
    if (state.section_pending) f.set(Led::Launch, Blink{200});
    f.indicator(IndicatorId::Lower, knob_levels_for(param));
}
```

The shell composes global contributors first, then the stack bottom→top (top
wins for LEDs it mentions; deeper scenes supply the rest), evaluates `Blink`
against one clock so same-period LEDs are in phase, folds "blink N times" and
alerts into `until_ms`, diffs against `last_sent`, and emits only changes via
`deluge_control_set_led` / `deluge_control_set_indicator`. Because it is
re-derived, a dropped flush is harmless and navigating away cannot leave a
stale LED lit. The five blink timers and six `LedBlinker` slots go away.

### 8.6 Tick

1. Drain the notification dirty-set (target architecture §4.3) →
   `on_model_changed` on the stack + repaint requests on affected surfaces.
2. Poll and lower input; route; apply actions.
3. `tick(dt)` the stack top-down, stopping below the first `occludes_grid()`.
4. For each OLED surface whose gate fires: clear, render the stack bottom-up
   honouring `oled_transparent`, `deluge_display_blit_oled(index, …)`.
5. Grid: if its gate fires, render bottom-up (dim beneath `is_grid_overlay`
   by right-shift, then composite), diff column pairs against `last_sent`,
   emit `deluge_control_set_pad_columns` for changed pairs, checking
   `deluge_control_pad_output_space` **here** rather than in the compositor.
6. LEDs (§8.5). `deluge_control_flush`.
7. Sleep until `min(next_deadline)` across gates and scene timers.

Timers are per-scene deadlines in POD state (`Timer{due_ms}`), checked in
`tick`. The 29-slot `TimerName` enum and `UITimerManager` are deleted.

### 8.7 Frontends and board profiles

A frontend is: a `libdeluge` BSP + its `DelugeBoard` + an app-side
`BoardProfile{button_map, led_map, encoder_roles, surface_roles,
shortcut_pads}`. Shell, toolkit and components are generic; scenes address
surfaces by role and split rects. The host simulator, configured with the new
board's descriptor and profile, validates the new device's UI before hardware
exists.

## 9. `deluge-scenes`

### 9.1 Composition

A scene is a POD state struct + views + `handle`. Views bind model → component
data and call components; the fused interactions (hold a pad, turn an encoder,
show the value on the OLED) are scene concerns: `handle` updates state,
`render` calls a grid component and an OLED widget from the same state.
Components never learn that the other surface exists.

Rules: read the model directly; never mutate in `render`; mutate only via
`deluge-commands`. The five god-views are split by concern — grid view, OLED
view, gestures, commands — as each is migrated, not before.

### 9.2 Menus as data

```cpp
struct MenuNode {
    l10n::String title;
    MenuKind kind;                          // Submenu, Toggle, Enum, Int, Decimal, Bipolar, Range,
                                            // MultiRange, Colour, Action, Dynamic
    std::span<const MenuNode> children;     // Submenu
    Binding binding;                        // {read, write, range, l10n names} over a param/setting handle
    Relevance relevant;                     // fn(const MenuContext&) -> bool
    Hint hint;                              // Slider, Pan, AttackCurve, LpfCurve, Percent, BipolarBar, Enum …
};
```

One `MenuScene` with `MenuState{path[8], cursor, scroll, editing}` walks the
tables. The horizontal four-column menu is the same tables rendered by a
second layout. `Dynamic` nodes (browser-populated lists, DX7 parameters)
supply children through a callback. Bindings target today's
`ModControllable`/settings through getter/setter pairs first and the params
registry (`target_architecture.md` §7) later; the table shape does not change. Shortcut pads are a
`constexpr ParamMatcher grid[16][8]` with a second layer.

Side effects: the backwards `song.h → gui/menu_item/reverb/model.h` include is
gone (the model keeps its own enum); MIDI-learn, patch-cable blink and
pad-lighting concerns leave the item and become shell services keyed by the
focused binding; the 7-seg abbreviation table is not needed.

### 9.3 Scene inventory (migration targets)

Session, arranger, note clip (piano roll / folded), kit, audio clip, automation,
performance, keyboard (with the seven layouts), sound editor (menus), browsers
(sample, preset, song, load/save), sample-marker editor, slicer, waveform,
settings, context menus, popups/console/screensaver/loading as overlay scenes.

### 9.4 Where existing code goes

| Today | Target |
|---|---|
| `hid/display/oled_canvas/` | `gfx-oled::Canvas` |
| `hid/display/oled.{h,cpp}` (layers, scrollers, blink, console, icons) | scroll/blink → `ui-oled` widgets + state; console/screensaver/loading → overlay scenes; icons → `gfx-oled` registry; DMA → already in libdeluge |
| `gui/fonts/` | `ui-tokens` (data) + `gfx-oled::Font` |
| `gui/colour/` | `gfx-grid` |
| `hid/led/pad_leds.*` | `gfx-grid::PadGrid` + tweens; flush → shell |
| `hid/led/indicator_leds.*` | `LedFrame` + board profile |
| `gui/ui/ui.{h,cpp}`, `root_ui`, `ui_timer_manager` | `SceneStack`, tick loop, deadlines |
| `gui/views/view.{h,cpp}` | `GlobalControls`, `ModKnobs`, `MidiLearn`, commands, components (§8.4) |
| `gui/ui/keyboard/layout/`, `column_controls/` | `ui-grid` components, moved as-is |
| `gui/menu_item/` (269 classes), `gui/ui/menus.cpp` | `MenuNode` tables + `MenuScene` |
| `gui/views/*_view.cpp` | scenes + views |
| `gui/l10n/` | `ui-tokens` |
| `hid/buttons`, `hid/encoders`, `hid/matrix` | input lowering in the shell + board profile |

## 10. Required `libdeluge` extension

Multi-display support, additive and backward compatible:

```c
typedef struct DelugeDisplayDesc { uint16_t width, height; uint8_t visible_top; } DelugeDisplayDesc;
/* DelugeBoard gains: */ uint8_t display_count; const DelugeDisplayDesc* displays;
DelugeStatus deluge_display_blit_oled_n(uint8_t index, const uint8_t* pixels, uint16_t w, uint16_t h);
bool deluge_display_busy_n(uint8_t index);
```

The existing single-display calls remain as index 0. The host BSP implements
the extension first (any count, any size) so the shell can be tested against
the new device's descriptor before its BSP exists. `button_rows/cols` and
`encoder_count` already describe the surface; button *identity* is the app's
board profile, not the ABI's.

**Status (task-10 reconciliation):** the multi-display wire opcode already exists on the host
link — `HOST_LINK_MSG_UPDATE_DISPLAY_N` (`0x2B`, `[index][page-major pixels]`,
`src/bsp/host/host_link.h:49`) is a clean additive extension in a gap the upstream (spark) wire
format left free, and `src/bsp/host/host_bsp.c:413` already sends it per display. But
`src/bsp/host/host_link.c` — the file that reads and dispatches incoming/outgoing host-link
messages — has **no reader** for it: nothing on the receiving side interprets `0x2B` yet, so today
it is a reservation, not a working extension. This is a harness-plan item (`2026-08-27-ui-test-harness.md`
or its successor), not a shell-plan one: either implement the reader in `host_link.c` or mark the
opcode explicitly `// reserved, no reader yet` until the harness plan wires one up.

## 11. Testing

| layer | test | asserts on |
|---|---|---|
| gfx | golden bytes: render → CRC32 + PNG of the 768/1024-byte buffer; `PadGrid` → `.state` text | exact pixels |
| toolkit | state-transition tests against a null canvas (cursor clamp, scroll, gate timing) + golden widgets | POD state, gate decisions |
| shell | event scripts — `<ms> button <id> down`, `encoder <role> +1`, `pad <x> <y> down`, `snapshot <name>` — producing one `.png` per OLED surface + a `.state` dump (pads, LEDs, rings). Same script and output format as deluge-sdk's headless harness so spark's simulator and golden tooling apply unchanged | scene stack, all surfaces |
| scenes | same scripts against a fake model, plus the emitted command list | frames + commands |
| whole app | `deluge_host` replay; the determinism invariant (`target_architecture.md` §4.5) covers framebuffers | end to end |

`diff -r got/ golden/` in CI. Lit-pixel-count tests are not accepted as
snapshots. Extraction PRs (§12, phase 2) are accepted on **byte-identical
frames** against the pre-extraction renderer.

## 12. Migration

Every phase ships green on `next` as a sequence of small PRs; forwarding
headers with removal dates; mechanical steps as committed codemods; the
layer-rule ratchet gains rows for the new targets and counts
`LegacySceneAdapter` instances. Phase numbers are this document's, not those of `target_architecture.md` §11.

1. **libdeluge extension** (§10). Host BSP implements it. Unblocks everything.
2. **`deluge-gfx-oled` / `deluge-gfx-grid` extraction.** Canvas over
   span+geometry, `Rect`/`Font`/`Icon`, `PadGrid`, tweens; old paths become
   façades; golden tests land. Acceptance: byte-identical frames.
3. **`ui-core`, `ui-oled`, `ui-grid`.** Frames, widgets, components, gate,
   state structs. Unit- and golden-tested; not yet used by the firmware.
   Parallel with 2.
4. **Shell as guest.** `ShellHostUI : UI` — one legacy `UI` subclass — runs
   the new shell inside the old stack. `OLED::main`'s and `PadLEDs::image`'s
   storage *is* the guest's surface backing (identical format, zero copy).
   First migrated scene: **`MenuScene` + the sound-editor/settings tables**,
   golden-compared against the legacy renderer per menu before each table
   ships. This burns `gui/menu_item/`.
5. **Invert.** The shell becomes the host; every remaining legacy `UI` runs
   through `LegacySceneAdapter` (forwards `UiEvent` → `padAction`/
   `buttonAction`/…, renders through the old calls into the same surfaces,
   bridges `indicator_leds` into `LedFrame`). `ui.cpp`, `currentUIMode`,
   `uiNavigationHierarchy`, `ui_timer_manager`, `View view` go here; timers
   become deadlines.
6. **Scene by scene.** Browsers and load UIs, keyboard, performance, session,
   arranger, automation, note clip last (8k lines, split by concern as it
   goes). Ratchet: adapter count → 0.
7. **Delete.** Adapter, `hid/display`, `hid/led`, `indicator_leds`,
   `gui/menu_item/`, seven-segment l10n table.
8. **Second frontend.** New BSP + `DelugeBoard` + board profile + surface
   roles. No scene changes by construction; validated first in `deluge_host`
   with the new descriptor.

Phases 2–3 run in parallel with 1; 4 gates 5; 5 gates 6. `next` and `main`
diverged on 2026-08-10; since then only 21 UI files (16 commits) changed on
`main`, so UI reconciliation is cheap — `next` merges `main` at each release
tag (spec F §6) and UI conflicts are resolved by rerunning the codemods.

## 13. Harvested from deluge-sdk and spark

Adopted: immediate mode with app-owned state; the structural repaint gate;
paint-and-hit-test in one pass; region-based layout that routes input;
`Turn`/`Edit` split; dirty tracking at the transport (column pairs), not the
widget; tweens as pure `from→to` functions with the driver outside; the
headless golden-frame script format; one `SceneId` carrying arguments; push
vs replace navigation; overlay compositing through capability flags; LEDs
re-derived per tick; menus and shortcut pads as static tables; `Rect`
splitting instead of a layout engine; bezel offset as a parameter; geometry
defined once.

Avoided: `Box<dyn>` heap trait objects (static instances + one virtual base);
per-event `Vec` returns (fixed-capacity `Result`); per-frame hash maps (flat
arrays by enum ordinal); byte-per-pixel framebuffers transcoded on flush (draw
into the packed page-major buffer); two parallel scene enums; duplicated
component sets; lit-pixel-count "snapshot" tests; panel geometry stated three
ways.

## 14. Sub-specs

This document is the umbrella; each layer has its own design (all 2026-08-26, this
directory):

| Spec | File | Plan |
|---|---|---|
| A — gfx | §6 here | `../plans/2026-08-26-gfx-extraction.md` |
| B — toolkit (`ui-core`, `ui-oled`, `ui-grid`) | `2026-08-26-ui-toolkit-design.md` | — |
| C — notifications (`DirtySet`, `NoticeQueue`) | `2026-08-26-ui-notifications-design.md` | — |
| D — shell | `2026-08-26-ui-shell-design.md` | — |
| E — scenes and menus | `2026-08-26-ui-scenes-menus-design.md` | — |
| F — migration and coexistence | `2026-08-26-ui-migration-design.md` | — |
| G — test harness | `2026-08-26-ui-test-harness-design.md` | — |
| H — second frontend | `2026-08-26-ui-second-frontend-design.md` | — |

Decisions taken while writing them, in addition to §3: the `deluge-commands` gate is
staged out (scenes mutate the model directly under a direction rule; spec I is a later
track); menus are declarative C++ `Node` literals, not generated; all new-library
identifiers are `snake_case`; the second device has a two-column sidebar and ships as a
**separate firmware image** selected at build time.

## 15. Open questions

1. **Surface roles on the new device.** Whether the extra OLEDs are
   per-encoder parameter displays or general-purpose determines which scenes
   grow `Param` views. Decide when the board profile is written; the shell
   does not care (spec H §3 supports both).
2. **Menu bindings before the params registry.** Getter/setter pairs over
   `ModControllable` are an interim; the switch to registry handles (§7 of the
   target architecture) is a table edit but must be scheduled against that
   track.
3. **Static-instance scenes and RAM.** ~40 scenes with POD state are small,
   but the state-cache slots for replace-navigation are per `SceneId`; cap
   and measure in phase 5.
4. **`next` ↔ `main` reconciliation cadence.** See §12 and spec F §6.
