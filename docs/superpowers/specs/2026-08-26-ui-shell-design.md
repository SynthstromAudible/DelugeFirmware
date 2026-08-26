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
	const Style* style;                                 // toolkit Style for this panel set
	ShortcutTable shortcuts;                            // pad (x,y) -> ParamMatcher, two layers
};

struct OledSurface { gfx::oled::Canvas canvas; gfx::oled::Geometry geometry; RepaintGate gate; SurfaceRole role; uint8_t index; };
struct GridSurface { gfx::grid::PadGrid grid; RepaintGate gate; ColumnPairCache last_sent; std::optional<ActiveTween> tween; };
struct LedSurface  { LedBits bits; KnobRing rings[2]; LedBits last_bits; KnobRing last_rings[2]; };

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

## 3. Scenes, registry, stack

```cpp
class Scene {
public:
	virtual void render(Frames&) = 0;
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

## 5. Services

Members of the shell, not globals. Each has `handle()`, `leds()`, `tick()` like a scene but
never renders OLED/grid.

| Service | Owns | Replaces |
|---|---|---|
| `GlobalControls` | transport buttons, record combos, tempo encoder/button, tap tempo, sync scaling, triplets, back long-press, affect-entire popup cancel, select combos; LEDs for `Play`/`Record`/`Tap`/`SyncScaling`/`Triplets`/time-base | `Buttons::buttonAction` tail, `View::buttonAction`, `View::setLedStates`, `flashPlay*`, `encoder_input.cpp` special cases |
| `ModKnobs` | the `ModTarget` (model, spec C §5) + gold-knob acceleration state, mod-button mode, knob rings, transient value popup (`ParamPopupScene`), MIDI-follow feedback trigger | `View::modEncoderAction*`, `modButtonAction`, `modEncoderButtonAction*`, `setKnobIndicatorLevel(s)`, `setModLedStates`, `displayModEncoderValuePopup`, `flushPendingModEncoderValuePopup`, `getModKnobMode` |
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
	pump_input(now_ms);                              // 3. lower + route + apply actions (deferred while sd_busy)
	tick_stack(now_ms - last_ms_);                   // 4. top-down, stop below occludes_grid()
	services_.tick(now_ms - last_ms_);
	for (auto& s : surfaces_.oled()) {               // 5. OLED surfaces whose gate fires
		if (s.gate.needs_frame(now_ms, had_input_, dirty.any())) { render_oled(s); blit(s); }
	}
	if (surfaces_.grid().gate.needs_frame(...)) { render_grid(); flush_grid(); }   // 6. incl. tween
	derive_leds(now_ms);                             // 7. every tick; cheap
	flush_leds();
	next_wake_ = earliest(gates' next_deadline(), scene timers);
	last_ms_ = now_ms;
}
```

- One scheduler task replacing `doAnyPendingUIRendering`, `uiTimerManager.routine`,
  the UI half of `readButtonsAndPadsOnce`, and `GRAPHICS_ROUTINE`; same cadence (10 ms),
  sleeping to `next_wake_` when nothing is pending.
- `render_oled(s)`: clear; walk the stack bottom-up from the lowest scene that is not
  `oled_transparent(s.role)`; root frame origin `(0, visible_top)`; services never draw.
- `render_grid()`: base = topmost scene with `!is_grid_overlay()`; render base; dim by
  right-shift if any overlay is above; render overlays; apply the stack's `greyout_mask()`;
  if `tween` is set, evaluate it into the output grid. `PadLEDs::renderingLock` disappears
  (single-entry tick).
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

- `Shell::init(board, profile)` builds surfaces and pushes `BootScene`; the load path
  pushes `SessionScene` as root when the song is ready.
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
