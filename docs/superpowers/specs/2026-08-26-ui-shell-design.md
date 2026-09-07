# UI shell: surfaces, scenes, input, services, tick

**Date:** 2026-08-26
**Branch target:** `next`
**Status:** design, pending review. Spec D of the UI stack redesign
(`2026-08-26-ui-stack-redesign-design.md` §8). Depends on spec B (toolkit), spec C
(`notify`), and the libdeluge multi-display extension (gfx plan, Task 1). Does **not**
depend on `deluge-commands` (staged out: scenes mutate the model directly under a
direction rule) or on the params registry.

Naming: functions, methods, members and locals are `snake_case`; types `PascalCase`.

## 1. Why

Today `gui/ui/ui.cpp`, `hid/buttons.cpp`, `hid/matrix/matrix_driver.cpp`,
`hid/encoder_input.cpp`, `hid/led/*`, `gui/ui_timer_manager.cpp`, `hid/display/oled.cpp` and
the global `View view` together *are* the shell — as ~30 globals and free functions with no
owner. The stack is a raw array plus `numUIsOpen`; mode is a hand-partitioned `uint32_t`
(47 values); every animation is a slot in a 29-entry timer enum; input routing is spread
over four files and threads SD-card reentrancy through every handler; LEDs are commanded
imperatively from 243 sites and must be undone by hand. None of it can be instantiated
twice, tested off-device, or pointed at a different panel set.

The shell is one object that owns all of it, built from the board descriptor and a board
profile, driving surfaces through `libdeluge`.

## 2. Board profile and surfaces

```cpp
namespace deluge::ui::shell {

enum class ButtonId : uint8_t;    // app-semantic: Play, Record, Shift, Song, Clip, Synth, Kit, Midi, Cv,
                                  // Keyboard, Scale, CrossScreen, Back, Load, Save, Learn, TapTempo,
                                  // SyncScaling, Triplets, AffectEntire, Mod0..Mod7, SelectEnc, TempoEnc,
                                  // XEnc, YEnc, ModEnc0, ModEnc1, ... None
enum class SurfaceRole : uint8_t { Main, Param0, Param1, Param2, Param3, Aux, None };
enum class EncoderKind : uint8_t { Detented, Accelerating };

struct BoardProfile {                                   // one constexpr instance per device, in deluge-scenes
	std::span<const ButtonId> button_by_matrix;         // [y * cols + x] -> ButtonId
	std::span<const int8_t> led_by_button;              // ButtonId -> board LED index, -1 = none
	std::span<const EncoderRole> encoder_by_index;      // libdeluge encoder index -> role
	std::span<const EncoderKind> encoder_kind;          // per role
	std::span<const SurfaceRole> oled_role_by_index;    // libdeluge display index -> role
	std::span<const Style* const> style_by_index;       // libdeluge display index -> toolkit Style; one per OLED surface
	ShortcutTable shortcuts;                            // pad (x,y) -> ParamMatcher, two layers
};

struct OledSurface { gfx::oled::Canvas canvas; gfx::oled::Geometry geometry; RepaintGate gate; SurfaceRole role; uint8_t index; const Style* style; };
struct GridSurface { gfx::grid::PadGrid grid; RepaintGate gate; ColumnPairCache last_sent; std::optional<ActiveTween> tween; };
struct LedSurface  { LedBits bits; KnobIndicator indicators[2]; LedBits last_bits; KnobIndicator last_indicators[2]; };

class SurfaceSet {                                      // built at init from deluge_board() + profile
public:
	std::span<OledSurface> oled();                      // display_count entries; storage from a fixed pool
	OledSurface* oled(SurfaceRole);
	GridSurface& grid();
	LedSurface& leds();
};

} // namespace deluge::ui::shell
```

`ButtonId` keeps today's `deluge::hid::button::*` vocabulary without the coordinate
derivation. The Deluge profile reproduces `fromCartesian()`/`fromXY()` as a table; the new
device adds ids and rows. Scenes that do not know an id ignore it.

**Amendment (task-9 reconciliation, toolkit-fixes):** `BoardProfile::style` was a single
`const Style*` for the whole board; it is now `style_by_index`, one entry per OLED surface,
and `OledSurface` carries its own `style` (copied in from `style_by_index[index]` at build
time, never re-indexed from the profile per frame). A board with more than one OLED — the
`Param0..3` open question below, or a second-frontend panel at a different height — may need
a different `Style` per panel (different `menu_rows`, different `rounded`); a single board-wide
style could not express that.

## 3. Scenes, registry, stack

```cpp
class Scene {
public:
	// task-9 reconciliation (toolkit-fixes): the draft's single `render(Frames&)` is gone — a
	// sparse, partially-populated `Frames` invoked once per surface per tick (§2.6's audit
	// finding) is worse than one call per surface a scene actually draws to. Both default to
	// nothing, so a scene that only paints one surface kind does not stub out the other.
	virtual void render_oled(SurfaceRole, ui::oled::OledFrame&) {}
	virtual void render_grid(ui::grid::GridFrame&) {}
	// Navigation derived from what render_oled/render_grid observed this tick (a widget's
	// Response, stashed in the scene's own state) — see §7's post-render step. Runs once per
	// scene per tick that scene was rendered, after every surface for that tick.
	virtual Result after_render() { return {}; }
	virtual void leds(LedFrame&) {}
	virtual Result handle(const UiEvent&, const UiContext&) = 0;
	virtual void tick(uint32_t dt_ms) {}
	virtual bool on_enter(const SceneId&) { return true; }     // may refuse (today's opened() == false)
	virtual void on_exit() {}
	virtual void on_resume() {}                                 // today's focusRegained()
	virtual void on_model_changed(const notify::DirtySet&) {}
	virtual bool is_grid_overlay() const { return false; }      // dim what is beneath, composite over it
	virtual bool occludes_grid() const { return false; }        // stop ticking scenes beneath
	virtual bool oled_transparent(SurfaceRole) const { return false; }
	virtual GreyoutMask greyout_mask() const { return {}; }     // today's getGreyoutColsAndRows
	virtual void save_state(SceneState&) const {}
	virtual void restore_state(const SceneState&) {}
};

struct SceneId { SceneKind kind; union { uint16_t clip; uint16_t output; BrowserKind browser; /*...*/ } arg; };
```

`render(Frames&)` — one call per tick handed every surface at once — and the `Frames` aggregate
it took are gone (task-9 reconciliation). A scene now gets one call per surface it is not
`oled_transparent` for: `render_oled(role, frame)` per OLED surface, `render_grid(frame)` for the
pad grid. `Scene::handle()` no longer sees the six widget-owned inputs (§4) while a widget the
scene renders could still claim them this tick — §7 explains the mechanism.

`after_render()` is new: navigation that a scene used to derive inline in `handle()` from a raw
`turn`/`select`/`back` event now happens here, once render has produced a widget `Response` and
the scene has stashed it in its own state. `MenuScene` (spec E) is the motivating case: its
`render_oled` calls `list`/`columns`, which claim the turn and the select and hand back a
`Response`; `after_render()` reads that `Response` and returns the navigation `Result` — a
`Push`/`Pop`, or nothing — instead of `handle()` re-deriving it from the event. The shell applies
`after_render()`'s `Result` through the router exactly as it applies a `handle()` result (§7).

Every frame built this tick — every OLED surface's root frame and the grid's — shares one
`ui::FrameContext{lowering_.snapshot(), frame_gate_}` (spec B §4), built and owned by
`InputLowering` (§4, exposed as `lowering_.ctx()`; `ctx_` below is shorthand for that call) and
read by `Shell::tick` (§7). There is exactly **one** per tick, not one per surface: a claim is
inherently shell-wide (one turn detent, one pair of gold knobs, one select encoder, one back
button per tick, however many OLED surfaces exist), so a claim made deep in one surface's frame
tree must be visible from every other surface's frame tree and from §7's post-render step, which
is only possible if all of them share the one context. `frame_gate_` is a `RepaintGate` the shell
owns in addition to each surface's own blit-scheduling gate (§2): a widget's `f.gate()` call arms
it, and its firing is folded into every surface's own `needs_frame()` check (§7) rather than
trying to attribute one widget's animation to the one surface it happened to be drawn on.

- **Registry**: one static instance per `SceneKind`; `SceneId` is a POD tagged union carrying
  arguments — the navigation target and the `SceneState` cache key.
- **Stack**: `std::array<Scene*, 8>` (today's deepest path is 5: root → clip minder → sound
  editor → context menu → rename).
- **Operations** and what they replace:

| Op | Replaces | Semantics |
|---|---|---|
| `push(id)` | `openUI` | `on_enter` may refuse → roll back, `on_resume` on the previous top (today's exact behaviour) |
| `pop(kind = top)` | `closeUI` | pops down to and including the target; new top gets `on_resume` |
| `replace_root(id)` | `changeRootUI` | old root's state saved to its `SceneState` slot; stack becomes `[new]` |
| `replace_top(id)` | `changeUISideways` | |
| `reset()` | `nullifyUIs` | stack empty; `current()` is a `NullScene` that swallows input (no "last UI before nullifying" hack) |

- After any stack change: `greyout_mask()` re-queried (today's `reassessGreyout` ×25), all
  gates `request_repaint()`, timers of the departed scene dropped (today's
  `unsetTimer(UI_SPECIFIC)`).
- **Context menus** become one `ContextMenuScene` fed `{title, options, accept_button}` by the
  pusher; the 13 `ContextMenu` subclasses become option tables + accept callbacks in
  `deluge-scenes`.

## 4. Input lowering and routing

One `InputLowering` object turns `DelugeInputEvent`s into `UiEvent`s with the profile, and
owns every piece of state that `Buttons::`, `MatrixDriver::` and `encoder_input.cpp` keep
today:

- **Held state**: `ButtonSet held` (177 view reads of `Buttons::isButtonPressed` become
  `ctx.held.has(...)`), `PadMask held_pads` with velocities (14 reads).
- **Shift and sticky**: `commandToggleShift`, `considerShiftReleaseForSticky`, the
  "another button while shift held" rule, the short/long-press distinction and the
  `LightShiftLed` feature → `ctx.shift`, `ctx.sticky`, a `Shift` LED contribution.
- **Cross-screen sticky release**, **select-press used-up**, **record-press used-up + short
  window** → shell state, exposed as `ctx.press_used_up(ButtonId)` / `ctx.held_since(ButtonId)`.
- **Encoders**: detent reduction per `EncoderKind`; gold-knob acceleration
  (`calcNextKnobSpeed`) and the initial-turn-direction debounce live here; the SD-test
  hook (`nextSDTestDirection`) stays as a debug feature.
- **`DELUGE_EVENT_NO_PRESSES`** → `UiEvent::AllReleased` to the top scene and the held-state
  tracker (the stuck-note safety net of issue #3168 is preserved, including its deferral).
- **SD-routine deferral**: events arriving while `deluge::sync::sd_busy()` are queued
  (bounded, 8, overwrite-oldest with a diagnostic) and replayed when it clears.
  `heldInputEvent`, `waitingForSDRoutineToEnd`, `encodersWaitingForCardRoutineEnd`,
  `inCardRoutine` and `ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE` all disappear; scenes
  never see the card.
- **Input lock**: `stemExport.processStarted` and `song.load_state` become
  `input_lock` reasons checked once in `pump_input`, not inside handlers.
- **Screensaver**: every event calls `screensaver_.note_activity()`.

```cpp
struct UiEvent { std::variant<Pad{x,y,velocity}, Button{ButtonId,on}, Encoder{EncoderRole,delta},
                              EncoderButton{EncoderRole,on}, AllReleased> v; };
struct UiContext { const ButtonSet& held; const PadMask& held_pads; bool shift, sticky; uint32_t now_ms;
                   const BoardProfile& profile; bool press_used_up(ButtonId) const; uint32_t held_since(ButtonId) const; };
struct Result { bool handled = false; FixedVector<Action, 8> actions; };
using Action = std::variant<Push{SceneId}, Replace{SceneId}, Pop{SceneKind}, Popup{...}, Repaint{SurfaceRole}, LockInput{bool}>;
```

**Routing chain** — each step returns `Result`; actions are applied whether or not the
event was handled; unhandled events are dropped, never an error:

1. modifier bookkeeping (above);
2. `ModKnobs` — `Mod0..7`, gold-knob buttons, gold-knob turns (today: always
   `getCurrentUI()->modButtonAction` → `View::…`, the same code for every UI);
3. top scene `handle()`;
4. `GlobalControls` — `Play`, `Record` (incl. output-recording combos), `Shift`, `TempoEnc`
   (clear automation / swing / tempo popup), `Back` long-press exit timer, `AffectEntire`
   cancels popup, tempo encoder (clip-view tempo vs playback tempo), select+`Clip` →
   pad refresh time, select+`Record` → threshold recording mode, shift+`Learn`+vertical;
5. dropped.

Order 2→3→4 is today's order (mod buttons → current UI → global buttons).

**Exception (task-9 reconciliation, toolkit-fixes):** `Encoder{Select, GoldUpper, GoldLower,
Horizontal, Vertical}`, `EncoderButton{Select}` and `Button{Back}` edges — the six widget-owned
inputs of spec B §4/§5 — do not take this synchronous path. `InputLowering` still updates its own
bookkeeping for them (held state, detent accumulation) and they still reach the `InputSnapshot`
every widget's frame reads (`f.turn()`, `f.select_pressed()`, …), but steps 2–4 do not see the raw
event during `pump_input`. They are queued and delivered after render (§7), gated by
`ctx_.claims` (spec B §4/§5's `InputClaims`): a widget the scene rendered this tick may have
claimed the input, in which case it is not delivered at all — this is the only way to know
whether a widget wanted it, since render has not happened yet at `pump_input` time. An unclaimed
one is still routed through the same 2→3→4 chain, just after render instead of during
`pump_input`; the shell plan's Task 5 amendment names this `route_post_render()`.

## 5. Services

Members of the shell, not globals. Each has `handle()`, `leds()`, `tick()` like a scene but
never renders OLED/grid.

> **Three of these six rows are now real classes, and they are not in this library.** E4
> (`2026-09-06-ui-scenes-services-and-roots-4.md`, tasks 1–5) built `GlobalControls`, `ModKnobs` and
> `MidiLearn` as `deluge::scenes::services::{GlobalControls,ModKnobs,MidiLearn}` in
> **`src/scenes/services/`**, not `src/shell/`. That is not a filing decision: all three touch the
> model — a `Song`, a `ParamManager`, a `Clip`, a `LearnedMIDI` — and `check_layer_rules.py`'s
> `shell-includes-outside` row forbids `src/shell/**` from including `model/**`. The row exists so
> the shell stays driveable by a fake surface set in a host process with no model linked at all
> (`tests/spec/shell/`), and that property is worth more than co-location. So `Service`
> (`src/shell/service.h`) is the **interface**, and a model-aware implementation lives one library
> up, exactly as `Scene` does. `Notices`, `Screensaver` and `Mirror` stay in `src/shell/` because
> none of them reads the model.
>
> Read the three rows below as the contract each class implements, not as an inventory of
> `src/shell/`. What each actually ports, with the LED sets, is in `src/scenes/README.md`'s
> "The three services". Two facts from building them that the table cannot show: the routing order
> is **top scene → `mod_knobs` → `global_controls` → `midi_learn`** and was reversed until E4 task 3
> (§15.4, §15.5); and **no application can install any of them yet**, because `Services` is fixed at
> the `Shell`'s construction and there is no `Shell::set_services` — the third spec-F wiring blocker
> (migration spec §10.2, `src/scenes/registry.h`'s boot-wiring list).

| Service | Owns | Replaces |
|---|---|---|
| `GlobalControls` | transport buttons, record combos, tempo encoder/button, tap tempo, sync scaling, triplets, back long-press, affect-entire popup cancel, select combos; LEDs for `Play`/`Record`/`Tap`/`SyncScaling`/`Triplets`/time-base | `Buttons::buttonAction` tail, `View::buttonAction`, `View::setLedStates`, `flashPlay*`, `encoder_input.cpp` special cases |
| `ModKnobs` | the `ModTarget` (model, spec C §5) + gold-knob acceleration state, mod-button mode, knob indicators, transient value popup (`ParamPopupScene`), MIDI-follow feedback trigger | `View::modEncoderAction*`, `modButtonAction`, `modEncoderButtonAction*`, `setKnobIndicatorLevel(s)`, `setModLedStates`, `displayModEncoderValuePopup`, `flushPendingModEncoderValuePopup`, `getModKnobMode` |
| `MidiLearn` | learn mode, `thing_pressed`, flash phase; scenes report `learnable_at(pad) -> LearnTarget` | `View::*MidiLearn*`, `UI_MODE_MIDI_LEARN`, `MIDI_LEARN_FLASH` |
| `Notices` | drains `NoticeQueue`; pushes/replaces `PopupScene`, appends to `ConsoleScene`, arms `WorkingScene`; popup timeouts by `popup_type` | `OLED::setupPopup/popupText/removePopup/displayWorkingAnimation/consoleText`, `Display::displayError/Popup` |
| `Screensaver` | idle timer, `note_activity()`, pushes `ScreensaverScene` (`rainfall`/`starfield` move to `deluge-scenes`) | `hid/display/screensaver.*` |
| `Mirror` | after each main-surface blit, hands the bytes to `HIDSysex::sendDisplayIfChanged` | `SYSEX_DISPLAY` timer reading `oledCurrentImage` |

## 6. Modes: what the 47 `UI_MODE_*` values become

| Bucket | Values | Becomes |
|---|---|---|
| Scene-local hold states (36) | `HOLDING_*`, `NOTES_PRESSED`, `CLIP_PRESSED_IN_SONG_VIEW`, `DRAGGING_KIT_NOTEROW`, `ADDING_DRUM_NOTEROW`, `CREATING_CLIP`, `PREDICTING_QWERTY_TEXT`, `MACRO_SETTING_UP`, `VIEWING_RECORD_ARMING`, `SCALE_MODE_BUTTON_PRESSED`, `SOLO_BUTTON_HELD`, `WAITING_FOR_NEXT_FILE_TO_LOAD`, … | fields in that scene's POD state — nobody else reads them (census: the model tests ~8) |
| Model/playback state | `RECORD_COUNT_IN`, `LOADING_SONG_*` ×4, `LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED`, `SELECTING_MIDI_CC`, `AUDITIONING`, `STEM_EXPORT` | proper fields per spec C §5; the shell mirrors them as `input_lock` |
| Shell state | `MIDI_LEARN`; `HORIZONTAL_SCROLL`, `VERTICAL_SCROLL`, `HORIZONTAL_ZOOM`, `*_EXPANDING`, `*_COLLAPSING`, `EXPLODE_ANIMATION`, `IMPLODE_ANIMATION`, `ANIMATION_FADE`; `QUANTIZE`, `STUTTERING` | `MidiLearn`; `GridSurface::tween`; `ModKnobs`/`GlobalControls` |

`currentUIMode`, `isUIModeActive()`, `isUIModeWithinRange()`, `EXCLUSIVE_UI_MODES_MASK` are
deleted.

## 7. Tick

```cpp
void Shell::tick(uint32_t now_ms) {
	const auto dirty = notify::dirty().drain();     // 1. facts → gates + on_model_changed
	apply_dirty(dirty);
	notices_.drain(now_ms);                          // 2. presentation requests → overlay scenes
	lowering_.ctx().begin_tick(now_ms);               // 3. one FrameContext for every frame this tick (§3, §4)
	pump_input(now_ms);                               //    lower + route the ordinary events + apply actions; the six
	                                                   //    widget-owned shapes (§4) are held back, not routed, here
	tick_stack(now_ms - last_ms_);                    // 4. top-down, stop below occludes_grid()
	services_.tick(now_ms - last_ms_);
	rendered_this_tick_.clear();
	// needs_frame() *consumes* the request it reports, so the shared widget gate is read exactly
	// once per tick, into a local, and that one bool is OR'd into every surface's own decision --
	// not re-queried per surface, which would starve every surface after the first (see §13
	// Amendments).
	const bool widgets_want_frame = lowering_.ctx().gate.needs_frame(now_ms, had_input_, false);
	for (auto& s : surfaces_.oled()) {                // 5. OLED surfaces whose gate (or the shared widget gate) fires
		if (s.gate.needs_frame(now_ms, had_input_, dirty.any()) || widgets_want_frame) { render_oled(s); blit(s); }
	}
	if (surfaces_.grid().gate.needs_frame(now_ms, had_input_, dirty.any()) || widgets_want_frame) { render_grid(); flush_grid(); }   // 6. incl. tween
	for (auto* scene : rendered_this_tick_) router_.apply(scene->after_render());     // 7. navigation from this tick's Responses (§3)
	deliver_post_render_input(now_ms);                // 8. held-back events, each gated by lowering_.ctx().claims
	derive_leds(now_ms);                             // 9. every tick; cheap
	flush_leds();
	next_wake_ = earliest(gates' next_deadline(), scene timers);
	last_ms_ = now_ms;
}
```

- One scheduler task replacing `doAnyPendingUIRendering`, `uiTimerManager.routine`,
  the UI half of `readButtonsAndPadsOnce`, and `GRAPHICS_ROUTINE`; same cadence (10 ms),
  sleeping to `next_wake_` when nothing is pending.
- `render_oled(s)`: clear; walk the stack bottom-up from the lowest scene that is not
  `oled_transparent(s.role)`; for each, `scene->render_oled(s.role, root_frame(s.canvas, *s.style,
  ctx_))` and append that scene to `rendered_this_tick_`; root frame origin `(0, visible_top)`;
  services never draw. (Task-9 reconciliation: this replaces the draft's single `scene->render(f)`
  over a per-surface `Frames`; §3.)
- `render_grid()`: base = topmost scene with `!is_grid_overlay()`; `base->render_grid(frame)`;
  dim by right-shift if any overlay is above; render overlays the same way
  (`overlay->render_grid(frame)`, each appended to `rendered_this_tick_`); apply the stack's
  `greyout_mask()`; if `tween` is set, evaluate it into the output grid. `PadLEDs::renderingLock`
  disappears (single-entry tick).
- **Step 7 — `after_render()`:** every scene appended to `rendered_this_tick_` this tick (there may
  be more than one: a base scene plus a grid overlay, or a scene drawn on more than one OLED role)
  gets exactly one `after_render()` call, in the order it was rendered, and its `Result` is applied
  through `router_` immediately, same as a `handle()` result.
- **Step 8 — post-render input delivery:** the six widget-owned `UiEvent`s held back from step 3
  are each offered to the same routing chain as an ordinary event (§4's exception), but only if
  `ctx_.claims` shows no widget claimed the corresponding input this tick. This is the only point
  in the tick where that is knowable — render (steps 5–6) and the navigation it can trigger (step
  7) have already happened, so a widget that wanted the input has already had its chance to say so.
- **Timers → owners** (the 29-slot enum is deleted):

| Timer | Owner |
|---|---|
| `DISPLAY`, `OLED_CONSOLE`, `OLED_SCROLLING_AND_BLINKING`, `LOADING_ANIMATION` | surface gates |
| `LED_BLINK`, `LED_BLINK_TYPE_1`, `LEVEL_INDICATOR_BLINK`, `METER_INDICATOR_BLINK`, `PLAY_ENABLE_FLASH`, `MIDI_LEARN_FLASH` | `derive_leds` against one clock |
| `SHORTCUT_BLINK`, `INTERPOLATION_SHORTCUT_BLINK`, `PAD_SELECTION_SHORTCUT_BLINK`, `NOTE_ROW_BLINK`, `SELECTED_CLIP_PULSE`, `DEFAULT_ROOT_NOTE`, `UI_SPECIFIC` | that scene's `Timer`/`Blink` state |
| `BACK_MENU_EXIT`, `TAP_TEMPO_SWITCH_OFF`, `SEND_MIDI_FEEDBACK_FOR_AUTOMATION`, `DISPLAY_AUTOMATION` | `GlobalControls` / `ModKnobs` |
| `MOD_ENCODER_POPUP_FLUSH` | `ModKnobs` |
| `MATRIX_DRIVER` | tween driver |
| `SCREENSAVER` | `Screensaver` |
| `SYSEX_DISPLAY` | `Mirror` |
| `OLED_LOW_LEVEL`, `READ_INPUTS`, `BATT_LED_BLINK` | **not UI** — stay in `deluge.cpp` on the scheduler |

## 8. Flush

- **OLED**: `deluge_display_blit_oled_n(i, bytes, w, h)` only when the gate fired; if
  `deluge_display_busy_n(i)`, skip and keep the gate armed. The main surface feeds `Mirror`.
- **Grid**: diff column pairs against `last_sent`; check `deluge_control_pad_output_space()`
  first and defer the remainder to the next tick (today's `uartGetTxBufferSpace` guard,
  moved out of the compositor); emit `deluge_control_set_pad_columns` for changed pairs;
  `deluge_control_flush()` once. The surface's scroll opcodes are used only by the tween
  driver for `scroll_*` tweens on boards that have them.
- **LEDs**: `derive_leds` composes services, then the stack bottom→top (top wins for LEDs it
  mentions; deeper scenes supply the rest) into `LedSurface`; diff against `last_*`; emit
  `deluge_control_set_led`/`set_indicator` for changes. Blink phase is `now_ms / period_ms`
  so same-period LEDs blink together; "blink N times" and alerts are `until_ms` deadlines.

## 9. Boot and song swap

- `Shell::init(board, profile)` builds surfaces and pushes nothing; the load path
  pushes `SessionScene` as root when the song is ready. (This section originally said "pushes
  `BootScene`" — see §15's §9 amendment for why there is no such scene.)
- Song swap: `reset()` then `push(LoadingScene)`; input is locked by `song.load_state`;
  `Dirty::SongSwapped` → the shell replaces the root with the new song's scene.

## 10. Testing

- Unit tests with a fake `SurfaceSet` (host canvases, no libdeluge): stack ops including a
  refused `on_enter`; routing order; deferral while `sd_busy()`; `AllReleased` clears held
  state; gates skip clean frames; LED layering and blink phase; column-pair diff emits only
  changed pairs; backpressure defers.
- Event-script tests (spec G) are the acceptance suite: boot → session → press Clip →
  snapshot every surface + `.state`.

## 11. Where existing code goes

| Today | Target |
|---|---|
| `gui/ui/ui.{h,cpp}` (stack, modes, `uiNeedsRendering`, `doAnyPending*`) | `SceneStack`, `Shell::tick`, `apply_dirty` |
| `hid/buttons.*`, `hid/matrix/matrix_driver.*`, `hid/encoder_input.*`, `deluge.cpp:dispatchInputEvent` | `InputLowering` |
| `gui/views/view.*` | `GlobalControls`, `ModKnobs`, `MidiLearn`, components (toolkit spec §7) |
| `hid/led/indicator_leds.*` | `derive_leds` + profile `led_by_button` |
| `hid/led/pad_leds.*` (flush, greyout, animations) | `flush_grid`, `greyout_mask`, `GridSurface::tween` |
| `hid/display/oled.{h,cpp}` (popups, console, layers, `sendMainImage`) | `Notices`, `PopupScene`/`ConsoleScene`/`WorkingScene`, `render_oled` |
| `hid/display/screensaver.*`, `rainfall.*`, `starfield.*` | `Screensaver` service + `ScreensaverScene` in `deluge-scenes` |
| `gui/ui_timer_manager.*` | deleted (table in §7) |
| `gui/context_menu/*` | `ContextMenuScene` + option tables |
| `hid/hid_sysex.cpp` display mirror | `Mirror` |

## 12. Open questions

1. `SceneState` slot sizing for `replace_root`: one slot per `SceneKind` (~40 × the largest
   POD) versus a small LRU of slots — measure the largest scene state once the note-clip
   scene is drafted (spec E).
2. Whether `Param0..3` surface roles are per-encoder displays or general-purpose on the new
   device (umbrella spec open question 1); the profile decides, the shell does not care.
3. `Popup` as an `Action` versus scenes calling `notify::notice()` like the model does — the
   latter keeps one path; decide in spec E when the first scene is written.
4. **(task-9)** Whether a post-render, unclaimed gold-knob turn should reach the real `ModKnobs`
   differently than "run the same 2→3→4 chain, just later" (§4's exception) once spec E implements
   `ModKnobs` — e.g. seeing the turn immediately, before any widget could have claimed it. Nothing
   observes the difference today (`ModKnobs` is a `NullService` until spec E), so the shell plan
   ships the simpler rule and spec E may revisit it.

## 13. Amendments during implementation (2026-08-27)

Recorded against `src/shell/**` as the shell was built (Tasks 1–10) and its review rounds. Each
line names the change and why it deviates from the draft above; the task-9 reconciliation already
folded into §2–§7's body (the `render_oled`/`render_grid`/`after_render` split, per-surface
`Style`, `begin_tick`) is not repeated here.

**§2 Board profile and surfaces**
- Concrete board tables live in **`src/shell/profiles/`** (`profiles/deluge.h,cpp`,
  `profiles/test_board.h,cpp`), not in a `deluge-scenes` package as the draft's comment says — no
  such package exists on this branch; the tables are board description, not application scenes,
  and have no dependency on anything a scene would need.
- `BoardProfile::encoder_by_index`'s Deluge instance is `{Vertical, Horizontal, Tempo, Select,
  GoldUpper, GoldLower}` — index 4 is the **upper** gold knob, index 5 the **lower** one
  (`hid/encoders.h:80-81`: `mod0` is commented "lower gold encoder", `mod1` "upper"; wired at board
  index 5 and 4 respectively, `hid/encoder_input.cpp:57`, `interpretEncoders`'s edge-index comment).
  A first pass had this swapped; the profile spec (`profiles_spec.cpp`) now pins both indices
  against the legacy comments directly so a future edit cannot silently re-swap them.
- `LedId` (the toolkit's opaque enum, `ui/core/led_frame.h`) has no board-independent meaning of
  its own; **this profile's contract is that its values equal `ButtonId`'s** (`profiles/deluge.h`,
  `shell/leds.h`), so `led_by_button` is the one table a button's LED is found through instead of
  a second, separately-numbered id space. Not a spec D requirement — a later board is free to
  number its LEDs however it likes, as long as its own `led_by_button` agrees with whatever it
  chooses.

**§3 Scenes, registry, stack**
- `SceneId` is `{SceneKind kind; uint16_t arg;}`, not the draft's tagged union
  (`SceneKind kind; union { uint16_t clip; uint16_t output; BrowserKind browser; } arg;`) —
  every use the shell itself has for `arg` (a clip index, an output index, a browser kind, the
  screensaver's animation seed) fits one 16-bit value, and a real union member would need its own
  discriminant anyway since `SceneKind` already *is* the discriminant that says which member is
  live. One field the target scene interprets is simpler than a union nothing here needs to be a
  union.
- `Result` gained the named constructor **`Result::unhandled()`** (`{ return {}; }`) alongside its
  two default-constructed fields — not a new capability, but `Result::unhandled()` at a `handle()`
  call site says "I looked at this and did nothing" where a bare `{}` or `{false, {}}` reads as
  though a field was forgotten; `Scene::handle()`'s own doc leans on the name to make "swallows
  everything" never the accident of a missing override.
- `Action::Kind` gained **`RepaintAll`**, alongside `Repaint{SurfaceRole}`: the draft's `Repaint`
  already used `SurfaceRole` to name one surface, so "repaint everything" needed either an
  overloaded sentinel value or its own `Kind`. `SurfaceRole::None` keeps the "no surface, never
  matches" meaning `surfaces.h` gives it everywhere else in the shell (`Repaint{None}` is
  specified as a no-op, not "all surfaces"), so the all-surfaces case is `RepaintAll` instead —
  ruled during Task 5's review (spec FAIL / cost if wrong: a rename).
- Two new `Scene` virtuals, **`dims_grid()`** and **`input_transparent()`**, neither in the draft:
  - `input_transparent()` (default `false`) answers "is this scene drawn over what's beneath but
    never taking its input" — the same three notice scenes, because legacy routes no key press,
    pad or encoder to a popup, a console line or the working animation; the UI underneath keeps
    running exactly as it was. `SceneStack::input_top()` — the topmost scene that is *not*
    `input_transparent()` — is both the scene `Router::route`/`route_post_render` call `handle()`
    on, and the one whose render frames get `InputMask::all()` (every other layer in a pass draws
    with `none()`, reads 0/false, and claims nothing). Ruled during Task 7's review (spec FAIL,
    1 Critical): without it, a console message would deafen the view drawn under it for as long
    as the line is up.
  - `dims_grid()` (default `is_grid_overlay()`), added one fix round later, answers "should
    what's beneath this scene be dimmed for it", separately from `is_grid_overlay()` answering
    "is this scene an overlay at all, so it must not be mistaken for the grid's base". The
    draft's §7 rule — "dim by right-shift if any overlay is above" — is right for an overlay
    that actually composites pads (a sidebar menu, a context menu) but wrong for one that
    composites nothing: legacy's popups, console lines and the working animation are OLED-only
    (`OLED::sendMainImage`, `hid/display/oled.cpp:364-376`) and have no grid effect whatever, so
    halving every pad's brightness to announce a "SAVED" popup would be a visible regression, not
    a port. Ruled in the same Task 7 review's fix-round-1 cycle; cost if wrong was one virtual.
- **A scene reads any one input in at most one render pass per tick.** Reading through a frame
  *is* claiming it (`ui/core/frame_context.h`), and a scene drawn on two surfaces builds two
  independent `FrameInput`s in the same tick, so reading `f.turn()` in both `render_oled` and
  `render_grid` claims the same detent twice and trips `InputClaims::double_claim`. Not stated in
  the draft because the draft had one `render(Frames&)` call, not one call per surface; now that
  task-9 split it, this rule is what keeps a two-surface scene from double-claiming — a scene that
  wants one input to affect both surfaces reads it in the pass that owns it and lets the other
  surface draw the resulting state, and a scene that needs the raw event on both leaves it
  unclaimed and reacts in `handle()` instead. Recorded here and in `src/shell/README.md` and
  `shell.h`'s class doc as the standing rule for spec E's scenes.

**§4 Input lowering and routing**
- §4's line "`held_pads` with velocities (14 reads)" is a **text error**, not a deviation: a pad's
  velocity belongs to its `UiEvent::Pad`, one-shot on press; the *level* `UiContext::held_pads`
  tracks (which pads are down right now) carries no velocity of its own, and no legacy call this
  branch ported reads one off the held set. Held pads are a `PadMask`; nothing here carries a
  velocity per held pad.
- The gold-knob **initial-turn-direction debounce** the draft's §4 bullet names is dead code on
  `main` as of the encoder overhaul (`e460593c8`, "FIX/Encoder overhaul + gold knob acceleration"):
  the overhaul dropped the `turnedRecently` half of the old mod-knob block and left the surviving
  `modEncoderInitialTurnDirection` test in `hid/encoder_input.cpp:178-205` unreachable. Ported here
  from the block's *pre-overhaul* shape at `e460593c8^` (`hid/encoders.cpp:242-292` at that
  revision) instead, since that is the last commit where the debounce actually ran and is
  therefore the legacy behaviour to reproduce; documented at length in `input_lowering.h`'s "gold-
  knob wiggle debounce" section.
- The **SD-test fuzz hook** (`nextSDTestDirection`, a debug-build-only randomised direction
  injector) stays **out of `src/shell`**, contrary to the draft's §4 bullet grouping it with the
  encoder lowering it sits beside in legacy. Spec G's script injector is the debug-input path for
  every shell test, not a feature the lowering itself needs to know about; adding it here would be
  a test-only code path inside production shell logic for no behavioural gain.
- **Routing order correction**: `ModEnc0`/`ModEnc1`'s own button presses (the gold knobs' pushes)
  are **not** `mod_knobs` events, despite being encoder-adjacent — legacy sends them to
  `getCurrentUI()->modEncoderButtonAction` from the *tail* of `Buttons::buttonAction`
  (`hid/buttons.cpp:225-231`), reached only after the scene's own `buttonAction` has already run.
  They are ordinary buttons routed scene-first, exactly like any other `Button`/`EncoderButton`
  event; only `Mod0..Mod7` and the gold-knob *turns* go to `mod_knobs` first, per the draft's
  order 2→3→4. Ruled during Task 5's review (spec FAIL): the brief had grouped all mod-encoder
  input as `mod_knobs`-first, which would have run a scene's `modEncoderButtonAction` handling one
  step later than legacy does — cost if wrong, per that review, is one case spec E's `ModKnobs`
  service would have to re-map.

  > **SUPERSEDED by §15.5 (E4 task 3, 2026-09-06). The sentence above — "only `Mod0..Mod7` and the
  > gold-knob *turns* go to `mod_knobs`" — is now false, and the ruling attached to it was right
  > about legacy and wrong about the conclusion.** Shipped `Router::is_mod_or_gold`
  > (`src/shell/router.cpp`) returns true for `ButtonId::ModEnc0`/`ModEnc1`. The observation
  > survives exactly as written; what changed underneath it is the *chain*. While `mod_knobs` ran
  > **before** the top scene, "legacy reaches the push only after the current UI" and "the push is
  > not a `mod_knobs` event" were the same statement, and excluding the two pushes was the only way
  > to spell the first. §15.4 corrected the chain to scene-first; from that point the two statements
  > come apart, and the exclusion started saying something legacy does not — because the tail's own
  > line is `getCurrentUI()->modEncoderButtonAction(0, on)` (`hid/buttons.cpp:227`, `:230`), and
  > `UI::modEncoderButtonAction` (`gui/ui/ui.cpp:43-45`) is a bare forward to `view`, overridden by
  > five UIs. It is the **third** base-class-fallback hook, not an exception to the pattern. See
  > §15.5 for the full evidence and for what the exclusion would have cost.
- The **held-back stash** (`held_back()`/`held_back_dropped()`) for the six widget-owned shapes
  lives on **`InputLowering`**, not on `Shell` as a first reading of the draft's §7 pseudocode
  might suggest (`shell.h`'s own earlier revisions carried a `held_back_` member before this was
  settled) — the lowering already owns every other piece of that event's bookkeeping (held state,
  detent accumulation), so the stash sits next to the state it is a view over, and `Shell` only
  ever reads it through `lowering_.held_back()`.

**§5 Services**
- `Notices` (`shell/notices.h`) drains `notify::Notice::Severity::Warn` into a **persistent**
  popup (`timeout_ms = 0`), matching legacy's `numFlashes = (severity == Warn) ? 0 : 3`
  (`notify_drain.cpp:137`) feeding `OLED::displayPopup`'s inline `persistent = !numFlashes`
  (`oled.h:156`) — not stated as a distinct rule in the draft's §5 row, which only names `Notices`
  as draining the queue. Ruled during Task 6's review (2 Important): a `Warn` notice with a normal
  timeout would clear itself before the condition it is warning about does.
- `Notices` also suppresses `notify::Notice`s carrying legacy's `Error::NONE` or
  `Error::ABORTED_BY_USER` (declaration order in `src/definitions_cxx.hpp`: `NONE` first = 0,
  `ABORTED_BY_USER` twentieth = 19), matching `OLED::displayError`'s own suppression
  (`oled.cpp:1173-1180`) — again a port of an existing legacy special case the draft's §5 row does
  not call out, ruled during the same review round.

**§7 Tick**
- The pseudocode's `lowering_.ctx().gate.needs_frame(...)` call, repeated once per OLED surface and
  again for the grid, is **not** what ships. `RepaintGate::needs_frame()` *consumes* the repaint
  request it reports (a one-shot "has anyone asked since I last answered yes" check, not an
  idempotent query), so calling it a second and third time in the same tick would only ever see a
  widget's request on the *first* surface asked — every other OLED surface, and the grid, would be
  starved of a repaint a widget genuinely requested. Found in the pre-flight scan across Tasks 4
  and 7 (`InputLowering` owns the shared `FrameContext` input side, `Shell::tick` owns the surface
  loop that reads it), before either task had been dispatched: `InputLowering` instead owns one
  `RepaintGate widgets_gate_` (`input_lowering.h:420 widgets_gate_`, exposed as `widgets_gate()`,
  referenced by the one shared `FrameContext` at `:421`), and `Shell::tick` calls `needs_frame()` on it **exactly
  once per tick**, into a local (`widgets_want_frame`, `shell.cpp:331`), OR'd into every surface's
  own decision (`shell.cpp:328-344`; `README.md`'s "The widgets' gate is evaluated once per tick"
  line). The rejected alternative — cache the bool across ticks instead of re-deriving it fresh
  each tick — would have widened the bug rather than fixed it: a widget's animation would then
  repaint only whichever surface happened to ask first, on every subsequent tick too, instead of
  correctly reaching every surface within the one tick the request was actually made.
- The pseudocode's `s.gate.needs_frame(now_ms, had_input_, dirty.any())` is **not** what ships:
  `dirty.any()` is never passed. Step 1 has already turned this tick's facts into gate requests
  targeted at the surfaces they actually name (`apply_dirty`, `Dirty::Oled` → OLED gates,
  `GridMain`/`GridSidebar`/`ClipContent`/`NoteRows` → the grid gate, anything unmapped → both); OR
  in `dirty.any()` besides would repaint the OLED for a note-row change and the grid for an OLED
  one, which is exactly the per-gate targeting `apply_dirty` exists to provide. Ruled during Task
  7's review (Important): `needs_frame`'s `model_dirty` argument is always `false` in the shipped
  code, and `apply_dirty`'s own gate requests carry the model-dirty signal instead.
- The **frame clock** for every frame built this tick is stamped by `InputLowering::end_tick(now_ms)`
  from the tick's own `now_ms`, not from whichever `on_board_event`/`on_encoder_edges` call happened
  to open the tick window — the draft's pseudocode calls `lowering_.ctx().begin_tick(now_ms)` at the
  top of `tick()`, which reads as though the window opens there, but it does not: input arrives
  *between* ticks, so `Shell` opens the window lazily on the first feed (or at `pump_input` if none
  arrived) and only closes it at the *bottom* of `tick()`. What *is* stamped fresh every tick, at
  the point the draft's pseudocode names, is the clock: `end_tick(now_ms)` re-stamps the snapshot,
  so `f.now_ms()` inside any widget's frame is always the tick's clock regardless of when the
  window happened to open. Ruled during Task 7's review (Important); documented at length in
  `shell.h`'s "the tick window" class note.
- Tween steps and the grid's backpressure backlog retry (§8, `flush`) both use
  `RepaintGate::request_repaint_after(ms, now_ms)` to arm a real deadline rather than merely
  requesting a repaint — so `next_deadline()`/`next_wake()` report the retry and a scheduler that
  would otherwise sleep past it wakes on time instead. Not named in the draft, which has no
  `request_repaint_after` API at all; added because "repaint eventually, when something else
  happens to wake the tick" is not equivalent to legacy's explicit retry timers
  (`setTimerForSoon`-style intervals) once the scheduler is free to sleep to `next_wake()`.
- `GreyOut` tweens ramp `RGB::greyOut`'s proportion **only inside the stack's greyout mask**, and
  **replace** (rather than stack with) the tick's ordinary full-strength greyout pass while the
  tween is running — ruled during Task 8's review (quality FAIL) to match legacy's own ramp
  (`pad_leds.cpp:983-1005`) feeding the same masked `prepareColour` (`:293-296`); stacking the two
  would double-darken the masked region.
- A **mirror frame throttled by `Mirror`'s `min_interval_ms`** is delivered late, once the interval
  elapses (`Mirror::retry`, called every `flush`), and is **never simply dropped** — the draft's
  §8 line ("throttled to at most one send per interval") could be read either way; ruled during
  Task 8's review that a dropped frame would leave the MIDI display mirror stale for longer than
  the throttle window, which legacy's own retry-and-resend timer does not do.
- `ScrollV` tweens step on a **50 ms cadence** (`UI_MS_PER_REFRESH`, `src/board_config.h:55`) —
  a port choice, not a literal one: legacy's vertical scroll has no self-timer of its own to copy
  (unlike the fade's 35 ms, `pad_leds.cpp:640/730`), so this is the same cadence the rest of the
  legacy grid animation path already uses, chosen for consistency rather than measured from a
  specific legacy call site.
- Spec D §8's **hardware grid-scroll opcodes** (device-side column-shift instructions
  some boards expose, as opposed to redrawing every pad every step) are **not implemented**: there
  is no `SurfaceSet` sink for them. `ScrollH`/`ScrollV` tweens always redraw the whole grid every
  step on this branch. Parked during Task 8's review as needing a new `SurfaceSet` sink — spec F
  (or a later amendment) is where that sink, and the board capability that decides whether to use
  it, belongs.

**§9 Boot and song swap**
- `Shell` gained **`boot()`** alongside `init(SceneId)`, not instead of it: `init()` (built in
  Task 7, before boot/song-swap existed) already covers "push an arbitrary first scene" for the
  unit specs' two-scene test registries, and `boot()` is the application's actual entry point —
  request a repaint on every surface so the first `tick()` after boot draws regardless of any
  gate's prior state. Keeping both avoids a rename the draft never specified either way.
- **`BootScene` and `SceneKind::Boot` were removed (2026-09-06), and this section is where they
  came from.** §9 above said `init` "pushes `BootScene`", and Task 9 built one: a "DELUGE" title
  plus the firmware version, shown from power-on until the application pushed its root. Nothing in
  legacy does that. The firmware draws nothing before its first root frame, and the logo a player
  sees at power-on belongs to the **bootloader** — `hid/display/rainfall.cpp` transcribes its
  `logoPixels` for the screensaver, which is the only thing in this firmware that draws it. So the
  splash was UI this redesign invented, on a stack whose whole claim is that it is
  behaviour-preserving and proven so by specs that drive legacy and port side by side; an invented
  screen has no legacy side and cannot be proven at all. `boot()` keeps its other half (the
  repaint request on every surface, which is the real port of `renderUIsForOled` marking the OLED
  dirty at `deluge.cpp:120,133,371`) and pushes nothing, so the stack is empty and the panels stay
  blank until the application's root arrives. `tests/ui-scripts/shell/boot.txt`'s golden is that
  blank panel, and it is the point rather than an absence of one. **The rule this came from, which
  binds the rest of the port: do not add scenes, UI or UX the instrument did not already have —
  whatever a spec section, this one included, happens to say.**
- The shell's half of the song-load protocol is **`Shell::song_loading(bool loading)`**, matching
  the draft's §9 prose ("`reset()` then `push(LoadingScene)`... `Dirty::SongSwapped` → the shell
  replaces the root") but naming it as one bracketing call the firmware makes around a song read,
  rather than the draft's two separate verbs: `song_loading(true)` resets the stack, pushes the
  shell's own `LoadingScene`, and takes the input lock; `song_loading(false)` pops it, releases the
  lock, and — only if `set_song_hooks` was given a real `SongHooks::root_for_song` — replaces the
  root with what that hook returns. `Dirty::SongSwapped` (apply_dirty) is the narrower, unbracketed
  path for the same hook, for a swap that was not itself started through `song_loading(true)`.
  Named during Task 9 to give the bracketing pattern one call the firmware's load path (spec F
  wires it) can call at both ends.
- `Screensaver` (§5's service) pushes **`SceneKind::Screensaver`**, but ships **no scene instance**
  of its own kind — unlike the shell's other three owned kinds (`Popup`/`Console`/`Working`/
  `Loading`), `table_[Screensaver]` is left exactly as the application's `SceneRegistry` reports
  it. `ScreensaverScene` itself (the `rainfall`/`starfield` animations) is spec E3's to write; this
  task's registry entry is a stand-in only, per the pre-ruling carried into Task 9's brief.
- The screensaver's animation **seed** reaches the pushed scene through `SceneId::arg`, which is
  16 bits — `ShellConfig::seed` is `uint32_t` (matching the draft's general-purpose config shape)
  but is truncated to the low 16 bits when handed to the scene. Noted as a minor for spec E3: a
  seed source wider than 16 bits of entropy loses bits here, though nothing on this branch supplies
  one.

**Final whole-branch review fix wave (2026-08-28)**

- **§7 step 7's "applied through `router_` immediately, same as a `handle()` result" is now true.**
  It was not: `Action::Repaint`/`RepaintAll` set a flag on the `Router` and nothing else, the flag's
  only reader is the step-5 OLED loop, and `clear_repaint_requests()` ran at the *bottom* of the
  tick — so a repaint asked for by `after_render()` (step 7) or by post-render delivery (step 8)
  was set and wiped inside one tick with nothing reading it. The clear now runs immediately after
  the render decisions (between steps 6 and 7), and what steps 7/8 request is additionally armed on
  the named surfaces' own `RepaintGate`s at a zero-millisecond deadline, so `next_wake()` reports
  `now` and the frame lands on the next tick rather than waiting for an unrelated event. A `Popup`
  from those same two steps is still one tick late by design (the bridge runs at step 3), which is
  now stated in `src/shell/README.md`'s tick walkthrough rather than left to be discovered.
- **`Screensaver` disables itself on a refused push.** §9's amendment above records that
  `SceneKind::Screensaver` names no scene until spec E3 writes one, so `SceneStack::push` refuses
  it — and the idle deadline stays past for ever, so the retry was unbounded. The first refusal now
  latches the service off (`active()` false, `next_timer()` `std::nullopt`, no further attempt,
  counted by `Screensaver::push_refused()` / `Shell::screensaver_push_refused()`). Paired with it:
  **`SceneStack` flags `changed_` only for an operation that touched something**, so a refused
  push, a `pop` for an unstacked kind, or a `replace_root`/`replace_top` of an unregistered kind no
  longer makes `Shell::tick` re-query the greyout and `request_repaint()` every gate. A push whose
  `on_enter` refused still flags, because `on_enter` and `on_resume` both ran.
- **Every absolute deadline in the shell is compared in difference form**
  (`static_cast<int32_t>(now_ms - deadline) >= 0`), matching what `RepaintGate`, `Mirror::due` and
  `InputLowering` already did: the popup timeout, the console-line lifetime, the screensaver idle
  deadline and its `next_timer()`, an LED blink's `until_ms`, and `compute_next_wake`'s choice of
  the earliest deadline. The millisecond clock wraps at ~49.7 days of uptime, and an absolute
  compare across that wrap reads a freshly armed deadline as long past.
- **The `nullptr` text hook renders the id as digits, as the pre-flight ruling said it would.**
  Every consumer now goes through `BoardProfile::text_or_digits`/`error_text_or_digits` (an
  allocation-free helper writing into a caller-supplied `TextScratch`) rather than testing the hook
  and substituting an empty string. `deluge_profile()` states `.text`/`.error_text`/`.shortcuts` as
  unset rather than leaving them to the defaults, since the l10n table is spec E's and wiring it is
  spec F's — until then a Deluge error popup shows its code, not a blank box.
- **A notice is popped by kind, not only while it is the stack top.** `Notices::tick`'s popup and
  working expiry and `Kind::CancelPopup` all used a `top()` guard, which let any scene pushed over
  a live notice strand it: invisible but stacked, reported by `state_lines()`, and back on screen
  when that scene popped. Each now takes what is above it down with it, exactly as
  `Screensaver::note_activity`'s wake already did.
- **Two latent hazards closed with the same wave**: `SurfaceSet::build`'s preconditions now state
  that a board's total column count must be **even** (`pairs` is `cols() / 2` and the send path
  works a pair at a time, so an odd total would silently drop its last column), and `Shell::flush`
  clears `GridSurface::dirty` when there is no grid storage instead of latching it.

**A new open question, not in §12 above**: there is no grid `SurfaceRole` for `Action::Repaint` —
a scene's `Result` cannot request a *grid* repaint the way it can an OLED one, because
`Action::Repaint` names a `SurfaceRole` and the grid is not one of `Main`/`Param0..3`/`Aux`. Parked
during Task 7's review fix round (`Shell::tick`'s grid-render decision has no `repaint_requested`
term for exactly this reason) and left unresolved: nothing on this branch needs it, since
model-side grid invalidation reaches the grid gate through `notify::Dirty` instead of through a
scene's own `Action`. Left for spec E to pick up if a scene ever needs to request a grid repaint
from `handle()`/`after_render()` the way it can an OLED one today.

**Spec E1 amendments (2026-08-30)**

Recorded here because the change landed against `src/shell/**` even though the plan doing the
landing is spec E1's (`2026-08-27-ui-scenes-menus-1.md`). Note on sequencing: the brief for this
plan asked that the `BoardProfile::shortcuts` removal below land in the same commit as this
amendment; that commit (`15b3e09bb`, Task 9) already shipped before this amendment was written, so
it is recorded here as planned rather than folded into that commit after the fact.

- **§2 Board profile and surfaces** — `ShortcutTable` and `BoardProfile::shortcuts` (the field this
  section's draft pseudocode still shows at line 46) were **removed** in spec E1 Task 9
  (`0a1e1cab4`). The shipped shape — two `std::span<const uint16_t>` layers, one table — could not
  express what legacy actually has: seven tables (five 16×8 grids plus two 1-D strips) selected by
  menu context (sound vs. kit vs. song vs. MIDI/CV, etc.), not one table with a second layer. Beyond
  the shape mismatch, `src/shell` never dereferenced the field at all — nothing in the shell reads a
  shortcut table, only `MenuScene` does — so the table is application data, not board description,
  and putting it in `scenes::menu::shortcuts` (`src/scenes/menu/shortcuts.{h,cpp}`) instead keeps
  `BoardProfile` to what a board actually is.
- **§5 Services, `Notices`** — `Notices::drain()`'s `Kind::Console` case never pushed
  `SceneKind::Console` onto the `SceneStack`, unlike its `Working`/`Text`/`Error` cases, which all
  push their scene as part of handling the notice. A console notice was queued into `ConsoleScene`'s
  line buffer but the scene itself was never brought on stack, so nothing ever composited a console
  line — no golden could show one, and the gap was silent because `append()` still ran without
  error. Fixed at `65d6860b3`/`76a4611a6`: `Kind::Console` now pushes `SceneId{SceneKind::Console}`
  itself (guarding against pushing twice if it is already the stack's current top), self-managing its
  own presence the way `Working`'s case already did, with the matching removal
  (`stack_.remove(SceneKind::Console)`) added to `tick()` once every line has expired
  (`ConsoleScene::any_active`).
- **§5 Services, `ConsoleScene::render_oled`** — `kLineHeight` in `notices.cpp` was a hand-picked
  `7`, but `FontId::Apple7` reports `text_height_of == 9` and draws its glyphs one row lower than a
  height-7 assumption expects, so every console line's bottom pixel row was clipped — invisibly,
  because the character shapes still resolved to *something*, just the wrong something: `E` and `F`
  became pixel-identical once the row that distinguishes them was cut off. `kLineHeight` is now
  `ui::oled::text_height_for(gfx::oled::FontId::Apple7)` rather than a literal, so a font swap cannot
  silently reopen the same clip.
- **§3 Scenes, registry, stack** — a scene removing only itself from the stack must call
  `Action::Kind::Remove` (`SceneStack::remove(pop_to)`, excising the one named kind wherever it
  sits), never `Action::Kind::Pop` with `pop_to` naming itself: `Pop` walks down from the stack top
  and takes every scene above the named one down with it, which is correct for "close everything
  down to and including X" but wrong for "close just me." `MenuScene`'s root-level `Back` (closing
  the menu at depth 0) used `Pop{.pop_to = SceneKind::FirstApp}` in a first pass, which also tore
  down a live `Working` animation or `Popup` sitting above the menu. Fixed to
  `Remove{.pop_to = SceneKind::FirstApp}` — see `menu_scene.cpp`'s own note at the fix site for why
  the target must still be named explicitly rather than defaulting to "just the top": a
  `Notices`-driven overlay (`Console` in particular, once the previous bullet's fix let it be pushed
  and left up across other scenes) can legitimately sit above `MenuScene` when Back is pressed, and
  removing "the top" would remove the notice instead of the menu.

---

## 14. Amendment from spec E2 (2026-08-30)

**The two surfaces' repaint paths are coupled, and a scene author must know it.** `Shell::tick`
renders an OLED surface only on `gate.needs_frame`, the shell-wide widgets gate, or
`router_.repaint_requested`; `after_render` then runs **only for a scene that rendered something on
either surface** (`rendered_this_tick_`). The two halves are therefore not independent: an OLED
repaint wake keeps a grid-driven behaviour alive, and losing the OLED wake silently stops the grid
one.

This was found the hard way in E2. `RenameScene`'s cursor blink and its held-backspace auto-repeat
are separate features on separate surfaces, and stubbing the *OLED* gate arm broke the *grid*
cadence examples — because the blink wake was the only thing bringing the scene back on a quiet tick,
and `after_render` is where the repeat advances. Neither the implementer nor the reviewer had traced
that before the bite-check showed it.

Two consequences for scene authors, both stated in §5's terms:

* **A render-only widget cannot keep itself alive.** Neither `ui::oled::text_edit` nor
  `ui::grid::text_keyboard` owns a clock or can arm a `RepaintGate`, so a blink they compute the
  right phase for will simply never repaint. `Scene::next_timer()` decides when the host is *woken*;
  `RepaintGate` decides whether a woken tick *redraws*. Both are needed, and only the scene can
  supply the second.

  **Amended, E3 task 10: the scene no longer has to supply the second separately.** The split above
  is still the mechanism, but it is no longer the scene author's problem, because four scenes in a
  row shipped or nearly shipped with the wake and without the redraw (E2's menu cursor blink,
  `BrowserScene`, `MarkerScene`, `RecorderScene` — the last of which lost a *recording*, not an
  animation). `Shell::tick` gained step 8b: at the bottom of every tick, each stacked scene's
  `next_timer()` value is armed with `request_repaint_after` on every surface that scene is drawn
  on. Overriding `next_timer()` is now sufficient for a blink; `Scene::timer_repaints()` is the
  per-surface mask (see §15, which corrects this from the bool it shipped as), and a scene that
  names no surface at all is the explicit opt-out for a timer with no visible consequence — the only
  case `Shell::compute_next_wake`'s scene loop still exists for. Scenes may still arm a gate from their
  own `render_*` and several do — an arm computed from the phase being drawn is tighter than one
  per-scene interval — but that is now a refinement rather than the load-bearing part.
* **A single-frame golden cannot see any of this.** A blink that computes correctly and never
  repaints, or a repeat that fires once and stalls, both pass a golden. Anything time-driven needs a
  quiet-tick example — see the harness spec's amendment on cadence.

## 15. Amendments closing E3 (2026-09-02, `2026-09-02-ui-scenes-menus-3.md`)

### 15.1 The timer fold is **per-surface**, and all-or-nothing was a regression

§14 records step 8b as arming "every surface that scene is drawn on" from one boolean opt-out. That
shipped, and it was wrong — not as a design preference but as a measurable cost on a battery device,
found by review after it had already been approved here.

**The two flushes are not alike, and that asymmetry is the whole argument.** `Shell::flush` sends the
grid only when the rendered frame differs from `last_sent` — a repaint that changed no pixel costs a
render and sends nothing. The OLED flush blits **unconditionally** once the surface has rendered;
there is no content diff. So arming too widely is *free* on the grid and a permanent cost on the
OLED.

That is not hypothetical. `MenuScene::next_timer()` returns a constant 20 ms whenever the focused
node has source shortcut pads — a blink cadence that exists purely for the *pads*. Under a blanket
fold, step 8b armed the OLED gates from it too, so **sitting on a patched-param leaf rendered and
blitted both panels at 50 Hz, forever**. Before the fold, `MenuScene` armed only the grid and the
diff swallowed it.

`Scene::timer_repaints()` therefore returns `RepaintSurfaces{bool oled, bool grid}`, defaulting to
`{true, true}`, and `MenuScene` overrides `{oled = false, grid = true}`. A scene whose timer drives
both takes the default and pays for both, correctly — `RecorderScene`'s completion poll is the
example. `{false, false}` is the wake-without-redraw opt-out §14 described.

**No test could see the regression**, which is the reason it was fixed here rather than recorded as a
cost. It is now covered at both levels: reverting the mask-honouring code in `Shell::tick` fails a
synthetic-scene example in `tick_spec`, and removing `MenuScene`'s override fails a real-fixture
example counting OLED blits across eight cycles of the fixed cadence.

The general rule, worth stating once: **a repaint request should name the surfaces it can actually
change.** Over-arming is only free where the flush diffs.

### 15.2 `Action::Kind::RepaintAll` never reached the grid

`shell/scene.h` documented `Action::Kind::RepaintAll` as "invalidate every surface for the next
tick". It did not, and could not: `SurfaceRole` is `{Main, Param0-3, Aux, None}` — **OLED panels
only, with no grid role** — so `Repaint` could never name the grid, and `Shell::tick`'s grid step
consulted the grid's own `RepaintGate` and nothing else.

The grid's own gate is not a substitute. `render_grid` is what arms it, and by the time post-render
input arrives (`Shell::tick` step 8) the render has already happened. So a scene that changed what
the **pads** show in response to a post-render event had no way to say so at all. `MarkerScene`'s
select turn is the case that exposed it: the marker moved a column on the OLED and stayed put on the
pads until the blink timer fired, up to a full 200 ms later — the one frame in the suite whose two
halves disagreed. `BrowserScene::handle` files the same action and had the same latency.

`Router::grid_repaint_requested()` is the fix: `RepaintAll` now sets a grid flag alongside the
per-role OLED flags, `Shell::tick`'s grid step ORs it into `needs_frame`, and the grid wake is armed
after step 8a so the flag is not read a tick late. Blast radius verified as zero pre-existing
goldens.

Recorded because the defect was **a doc claim the type system could not support** — a `SurfaceRole`
enum with no grid member, under a comment promising every surface. A reader had no way to find that
out except by tracing `tick`.

### 15.3 Step 8b arms only where the scene can paint, and `compute_next_wake` skips only a real arm

§15.1 fixed *which surfaces* a timer claims. It left *whose* timer claims them unexamined, and the
final review found that half wrong too.

**The render functions draw from a base up.** `Shell::render_grid` draws only from the grid base —
the topmost non-overlay scene — and `Shell::render_oled` only from `lowest`, found by walking down
while `oled_transparent(role)` and computed **per surface role**. Step 8b iterated
`stack_.scenes()` unconditionally, so a covered scene armed a gate it structurally could not paint
through. Two live consequences, both introduced by the fold itself:

* A covered `MenuScene` whose focused node has source pads — exactly the node that opens
  Browser/Marker/Recorder — kept arming the grid gate at 20 ms for the whole life of the scene above
  it. Pre-fold that arm lived inside `MenuScene::render_grid`, which stops being called when covered.
* `RecorderScene` takes the `{oled, grid}` default with a 10 ms poll, and its own clipping
  `PopupScene` is opaque, so while that popup is up step 8b armed every OLED gate every 10 ms and
  `flush` blits unconditionally: a **100 Hz two-panel blit** for the popup's lifetime. This is
  §15.1's regression one level up, and the per-surface mask cannot see it — the surface is the right
  surface, the scene is not the drawn one.

The two base computations are now `Shell::grid_base()` and `Shell::oled_lowest(SurfaceRole)`, called
by both the render functions and step 8b, so there is one copy of each walk. `tick_spec` covers both
consequences directly on the gate and on the blit count: reverting either arm test fails them (the
OLED one goes from 2 blits to 22 over ten poll periods).

**`compute_next_wake`'s skip is now conditional on the arm, not on the mask.** It skipped any scene
whose `timer_repaints().any()`, on the premise that step 8b had already armed a gate carrying that
deadline. After the change that premise is false for a covered scene — and it was already false, even
before it, for an OLED-only claim on a profile with no OLED surfaces. `Shell::timer_arms_a_gate` is
the same predicate step 8b applies, asked as one bool.

The answer is *not* "a scene that can paint nothing needs no wake". `Shell::tick_stack` calls
`Scene::tick(dt)` on every scene down to the first `occludes_grid()`, whatever the render pass
decides, so a covered scene's clock still advances and the moment it asked to be woken at is still
the moment it wants to be ticked at. (What a covered scene does not get is `after_render()`, which
runs only for `rendered_this_tick_` — which is why `RecorderScene` survives its own popup through the
*grid* pass, where `PopupScene` is an overlay and the recorder is still the base.)

### 15.4 The routing chain had `mod_knobs` and the top scene the wrong way round (found in E4)

Filed here rather than in a new section because it is the same class of defect as §15.2: a shipped
`Router` behaviour that no spec could catch, because the slot involved was empty.

**The defect.** `Router::route` (`src/shell/router.h`, `src/shell/router.cpp`'s `run_chain`) ran
`services.mod_knobs` → top scene → `services.global_controls`, and both the header doc and E4's plan
preamble stated that ordering as legacy's. It is not legacy's ordering for either of the two event
shapes `mod_knobs` is consulted for.

**The evidence.** `services.mod_knobs` is the port of `View`. Legacy never dispatches a mod button or
a gold-knob turn to `view` directly:

* `Encoders::interpretEncoders` calls `getCurrentUI()->modEncoderAction(e, offset_accelerated)`
  (`hid/encoder_input.cpp:194`).
* `Buttons::buttonAction`'s mod-button loop calls `getCurrentUI()->modButtonAction(i, on)`
  (`hid/buttons.cpp:104`), then `goto dealtWith` — so a mod button never reaches the `:143-231`
  tail either.
* `UI::modEncoderAction` and `UI::modButtonAction` (`gui/ui/ui.cpp:35-41`) are the **base-class**
  implementations, and each one's entire body is a forward to `view`.

So the current UI gets first refusal and `View` is the fallback — uniformly, for mod buttons exactly
as for gold-knob turns. Six UIs override the first (`SoundEditor`, `gui/ui/sound_editor.h:80`;
`ArrangerView`, `gui/views/arranger_view.h:49`; `AutomationView`, `gui/views/automation_view.h:116`;
`PerformanceView`, `gui/views/performance_view.h:96`; `SessionView`, `gui/views/session_view.h:100`;
`InstrumentClipView`, `gui/views/instrument_clip_view.h:215`). Two override the second
(`SessionView`, `gui/views/session_view.h:81`; `PerformanceView`, `gui/views/performance_view.h:98`).

**Why it stayed invisible for three plans.** The `mod_knobs` slot held a `NullService` until E4 task
2, and a null service declines everything, so the chain behaved identically either way round. E4
task 2 found it by reading `hid/`, not by a failing spec.

**What it would have cost.** `SoundEditor::modEncoderAction` (`gui/ui/sound_editor.cpp:1583-1607`)
intercepts a gold turn while MIDI learn is active and learns the knob to the current menu param,
falling through to `UI::modEncoderAction` otherwise. `MenuScene` is that UI's port. With the service
claiming the turn first, that arm could never fire, and no spec in E4 would have caught it — nothing
in the plan drives a service and a scene competing for one event.

**The fix.** The chain is **top scene → `mod_knobs` → `global_controls`**. `Router::route_post_render`
shares `run_chain` and therefore changed with it; its claim gate is unaffected (a claimed event is
still skipped outright, now before the scene rather than before the service).

Nothing in `ModKnobs` made this harder: `ModKnobs::handle` returns handled for a gold turn and
unhandled for everything else, which *is* the base-class-fallback contract.

**Coverage.** `tests/spec/shell/router_spec.cpp`'s *"a scene that claims a gold-knob turn gets it,
and mod_knobs never sees it"* and *"a gold-knob turn the scene declines falls through to mod_knobs,
before global_controls"*. Both directions are needed: a service-first router still lets a *declining*
service fall through to the scene, so the "scene wins" half alone does not distinguish the two
orderings, and a router that never consults the service at all passes the "scene wins" half too.

### 15.5 …and it excluded the gold knobs' own pushes, which are the same hook again (found in E4)

§15.4's sibling, and the reason it needs its own section: fixing the chain made a *second*,
previously-correct decision wrong. This section supersedes the "Routing order correction" bullet in
§4's spec-E ruling list, which still described the old behaviour.

**What §4 said.** "`ModEnc0`/`ModEnc1`'s own button presses are **not** `mod_knobs` events… only
`Mod0..Mod7` and the gold-knob *turns* go to `mod_knobs` first", ruled during plan D's Task 5 review
as a spec FAIL against a brief that had grouped all mod-encoder input as `mod_knobs`-first.

**Why it was right then.** With the chain running `mod_knobs` → scene → `global_controls`, sending a
push to `mod_knobs` would have given `View`'s handling of it *priority over the scene's* — the exact
inversion the reviewer caught. Excluding the two pushes from `is_mod_or_gold` was the only way to
express "legacy reaches this only after the current UI" in a chain that asked the service first.

**Why it is wrong now.** The push is the **third** hook with the base-class-fallback shape, and the
evidence is the same shape as §15.4's:

* `Buttons::buttonAction`'s tail runs `getCurrentUI()->modEncoderButtonAction(0, on)` and
  `…(1, on)` (`hid/buttons.cpp:227`, `:230`), after the `getCurrentUI()->buttonAction` call at
  `:134` and before `dealtWith:` at `:232`.
* `UI::modEncoderButtonAction` (`gui/ui/ui.cpp:43-45`) is the base-class implementation, and its
  entire body is `view.modEncoderButtonAction(whichModEncoder, on)`.
* **Five** UIs override it: `InstrumentClipView` (`gui/views/instrument_clip_view.h:199`),
  `SoundEditor` (`gui/ui/sound_editor.h:81`), `SessionView` (`gui/views/session_view.h:80`),
  `PerformanceView` (`gui/views/performance_view.h:97`), `AutomationView`
  (`gui/views/automation_view.h:117`).

So "scene first, `View` second" describes the push exactly as it describes a mod button and a
gold-knob turn. Once the chain is scene-first, *excluding* the push no longer expresses "after the
UI" — it expresses "never", which legacy does not say.

**What it would have cost.** `View::modEncoderButtonAction` (`gui/views/view.cpp:1332-1350`) is E4
task 3's own citation: it is the delete-automation gesture (shift + push) and the
`ModControllable::modEncoderButtonAction` dispatch. Ported in E4 task 3, it would have been
unreachable dead code — `ModKnobs::handle`'s `EncoderButton` arm has no other route to it.

**The fix.** `Router::is_mod_or_gold` gained a `UiEvent::Kind::EncoderButton` case matching
`ButtonId::ModEnc0`/`ModEnc1`. `EncoderButton{SelectEnc}` is deliberately **not** included: legacy's
tail handles the select press itself and that hook has no `view` fallback at all, so it must keep
going scene → `global_controls`.

**Coverage.** `tests/spec/shell/router_spec.cpp`'s "ButtonId::ModEnc0/ModEnc1 presses reach the
scene first and mod_knobs second, like every other View fallback" drives all four cases — the
declining scene (log reads `scene, mod, global`), the claiming scene (log reads `scene` alone), the
other knob, and `SelectEnc` still bypassing `mod_knobs` entirely.

**A fixture error the same change exposed.** Plan D's and E4 task 2's specs built the two pushes as
`UiEvent::Kind::Button`. `InputLowering` emits `Kind::EncoderButton` for them
(`is_encoder_button`, `src/shell/input_lowering.cpp:42-53`, applied at `:298`), so those examples
were asserting about a shape the lowering never produces. Corrected in both specs.

### 15.6 The chain's fourth step, and the `Service` half of the timer fold (E4 task 5)

Two changes, both made by E4 task 5 when `services::MidiLearn` became the first real occupant of
`Services::midi_learn`. Neither is a defect found in shipped behaviour; both are the shell growing
the seam a service that was only ever a slot now needs.

**`Router::run_chain` gained a fourth step.** The chain is now **top scene → `mod_knobs` →
`global_controls` → `midi_learn` → drop**. `midi_learn` is last, and the position is argued from
legacy rather than from the slot's ordinal:

* `View` is the fallback *after* the current UI in both of legacy's MIDI-learn dispatchers.
  `PlaybackHandler::noteMessageReceived` asks `getCurrentUI()->noteOnReceivedForMidiLearn` and falls
  through to `view.noteOnReceivedForMidiLearn` (`src/deluge/playback/playback_handler.cpp:2968-2971`);
  `PlaybackHandler::midiCCReceived` gives `soundEditor` first dibs and falls through to
  `view.ccReceivedForMIDILearn` (`:3128-3136`). Same shape as §15.4's argument for the other two.
* The four `View::buttonAction` arms `MidiLearn` owns — the `TAP_TEMPO` learn branch
  (`gui/views/view.cpp:148-166`), `LEARN` (`:185-196`), and `PLAY`/`RECORD` under learn (`:199-212`,
  `:215-228`) — sit in the **same `if`/`else if` chain** as the arms `GlobalControls` owns, so their
  relative order is unobservable *given* that `GlobalControls` abstains from exactly those four
  (`ViewArm::LearnOwns`) and suppresses its own tail for them, which reproduces `view.cpp:469`'s
  `DEALT_WITH`. Routing `midi_learn` last is what keeps that abstention live rather than dead.
* **LED composition requires last.** `LedCompose::compose` runs services in `Shell`'s own order
  (`src/shell/shell.cpp`'s `all[]`: `mod_knobs`, `global_controls`, `midi_learn`, notices,
  screensaver) and the last writer wins. `View::midiLearnFlash` (`view.cpp:804-815`) *overwrites*
  the PLAY, RECORD and TAP_TEMPO LEDs that `PlaybackHandler::setLedStates`
  (`playback/playback_handler.cpp:2499-2512`, i.e. `GlobalControls::leds`) had just declared, and
  `View::endMIDILearn` hands them back by calling `setLedStates` again (`view.cpp:498`). A
  `MidiLearn` composed before `GlobalControls` could never flash them.

Covered by `tests/spec/shell/router_spec.cpp`'s "midi_learn is the LAST step…", which drives the
drop case, the `global_controls`-claims case, and the `midi_learn`-claims case.

**`Service::timer_repaints()` is new, and its default is the opposite of `Scene`'s.** §15.1 folded
"a scene timer is a repaint request" into `Shell::tick` step 8b; the service half was never folded,
so a `Service::next_timer()` fed `compute_next_wake` and dirtied nothing. `MidiLearn`'s flash is the
first timer for which that is wrong: legacy's `View::midiLearnFlash` toggles `midiLearnFlashOn` and
then asks the **root UI** to repaint (`view.cpp:797-802`), and every reader of that bit is a pad
renderer. Step 8c is the service loop.

Three deliberate asymmetries with the scene loop, each for a stated reason:

1. **The default is `{false, false}`**, not `{true, true}`. Four services predate the hook
   (`ModKnobs`, `GlobalControls`, `Notices`, `Screensaver`) and none asked to dirty anything; a
   both-true default would have started arming two gates for all four as a side effect.
2. **No paintability narrowing.** §15.3's `oled_paintable`/`grid_paintable` answer "is this scene at
   or above the surface's base"; a service has no stack position and paints nothing itself, so its
   claim is honoured unconditionally.
3. **Because of (2), the OLED half is the dangerous half** — §15.1's asymmetry, one level up: the
   grid's flush diffs and sends nothing for an unchanged frame, an OLED surface is blitted
   unconditionally once rendered. `MidiLearn` claims `{.oled = false, .grid = true}`.

Covered by `tests/spec/shell/flush_spec.cpp`'s "a service timer arms only the surfaces it claims",
which drives claim-nothing, claim-grid and no-timer against a real `Shell`.

### 15.7 `SceneStack` both over- and under-notified `on_resume` (found in E4 task 10)

Found by measurement, not by review, and by a scene rather than by a shell spec. E4 task 9 mapped
`SlicerScene::on_resume()` onto `Slicer::focusRegained` (`gui/ui/slicer.cpp:59-72`), which is a
faithful reading of `closeUI`: it calls `newUI->focusRegained()` on whatever it leaves on top
(`gui/ui/ui.cpp:194-199`). Task 10 then gave the slicer its own timed popups — `Slicer` raises them
from four of its own arms — and forty hand-placed slices disappeared 800 ms after a pad tap.

**The premise the mapping rested on does not survive the port.** In legacy a popup is a display
overlay (`OLED::popupText`), drawn into the OLED image on its way out and never a `UI` on
`uiNavigationHierarchy`, so it can never reach `closeUI` and `focusRegained` never runs for one. In
the shell a popup **is a scene**: `Notices::show_popup_with_timeout` pushes `SceneKind::Popup`
(`shell/notices.cpp:301-325`) and `Notices::tick` removes it once `kPopupTimeoutMs` — 800 ms
(`notices.cpp:31`) — has elapsed (`notices.cpp:327-350`). Every scene spec was green throughout,
because no example had a popup and a stateful `on_resume` in the same run.

Three separate defects, of which two are fixed here.

1. **Over-notification.** `pop()` and `remove()` called `on_resume()` unconditionally, including
   when the scene that left was `input_transparent()`. `SceneStack::input_top()` skips those scenes,
   so a scene under a popup never lost the top, never stopped receiving input and was never exited —
   the notification asserted a transition the shell itself can prove did not happen.
2. **Under-notification, symmetrically.** Both arms resumed `stack_[n_ - 1]`, the *raw* top. In
   `[A, popup, B]`, removing `B` notified the **popup**, while `A` — which genuinely regains the
   input top — was told nothing.
3. **`Scene` has no dual.** There is no `on_pause`/`on_suspend`: `push()` notifies the covered scene
   of nothing. This is **left as it is, deliberately**, and is now written down on
   `Scene::on_resume` rather than left as an open asymmetry. Nothing needs the dual — a scene taking
   the top runs `on_enter`, and no ported `focusRegained` counterpart reads a "you are being
   covered" edge — and adding one would put a hook on every scene to serve no caller. If one is ever
   added it must fire on exactly the same condition as `on_resume`, or the pair stops being a pair.

**The fix.** `push`'s rollback, `pop` and `remove` snapshot `&input_top()` immediately before their
mutation and compare it with `&input_top()` immediately after, resuming only when the two differ
(`SceneStack::resume_new_input_top`). No new predicate was needed: `input_transparent()` is already
the shell's own statement that a scene does not hold the input top. The `NullScene` is excluded,
because an empty stack — or one left holding nothing but transparent layers — has no input top to
resume.

`replace_top`'s rollback is the deliberate exception and stays unconditional. Its edge is *paired*:
`previous->on_exit()` really ran, so `on_resume` is that call's dual rather than a claim about the
top, and suppressing it for a transparent `previous` would leave a scene that had been told it left
the stack with no notice that it is back. `changeUIAtLevel`'s own failure path does the same thing
(`gui/ui/ui.cpp:96-101`).

**What it cost in shipped code.** `SlicerScene` is the scene that paid: its `on_resume` was emptied
for one task and is now restored. `MenuScene` was taking the spurious edge too — it raises
`Notice::Kind::Console` itself (`menu_scene.cpp:1256`, `menu/bindings.cpp:372`), `ConsoleScene` is
`input_transparent()`, and `MenuScene::on_resume`'s `go_up_on_resume` flag guards only the `ascend()`
(`:332-335`) while the tail resets `render_resp`, `value_changed`, `value_node`, `back_claimed` and
`column_press` unconditionally (`:337-341`). **It cost nothing**, and the reason is the tick order:
`Notices` is a `Service`, so `Notices::tick` runs at `Shell::tick` step 4 (`shell/shell.cpp:304-305`),
before step 5's OLED render and step 7's `after_render`. All five fields are per-tick observations
written during render, read by `after_render` and drained at its tail (`:1358-1362`), with
`render_oled` re-zeroing two of them at its own head (`:482-483`) — so every one of them was already
at its default at the instant the spurious `on_resume` arrived. It was a latent bug, not a shipped
one, and it is closed either way.

**A limitation, stated rather than fixed.** The comparison is scene identity, and the registry holds
one `Scene` per kind — so a kind stacked *twice* is one object at two indices, and popping the upper
one reads as "no transition" even though that object was exited. Nothing in the shell builds such a
stack and no lifecycle hook is coherent under one (the scene takes `on_enter` twice with no
`on_exit` between, and both indices share a single `SceneState` slot). Refusing it in `push` is the
real fix and is not taken here.

Covered by five rows in `tests/spec/shell/scene_stack_spec.cpp` — removing a transparent scene
notifies nobody; removing a scene from above one resumes the input top rather than the raw top (and
the `pop()` twin of the same); removing the real top from under a transparent scene notifies nobody;
a refused push resumes only when the refused scene held the input top; and a refused `replace_top`
resumes the scene it exited whatever the input top is — plus, from the scene side,
`sim/scenes_spec/slicer_scene_spec.cpp`'s "keeps its slice table when a popup it raised itself
expires" and "resets its state when a real scene above it closes".
