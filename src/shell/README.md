# deluge_ui_shell

The shell is the UI's runtime. It owns the scene stack, lowers raw board input into `UiEvent`s,
routes them through the scenes and services, ticks, renders every surface that needs a frame,
derives the LEDs and flushes it all through one hardware seam. It knows nothing about the model or
legacy UIs: scenes (`src/scenes`) supply the behaviour, and a host -- on the firmware, the legacy
`ShellHostUI` -- calls `Shell::tick`. Namespace `deluge::ui::shell`; no allocation on the tick
path, no mutable globals, `snake_case` members, `PascalCase` types, `k` constants.

## Layer rules

`scripts/check_layer_rules.py` enforces, over `src/shell/**`:

| row | rule |
|---|---|
| `shell-includes-outside` | include only `shell/`, `ui/`, `gfx/`, `notify/`, `libdeluge/` and the standard library -- never `model/`, `gui/`, `hid/`, `io/`, `storage/`, `definitions*`, `board_config.h` |
| `shell-no-clock` | no clock of its own: every deadline runs off the `now_ms` the caller passes, so a spec drives the shell with a fake clock |
| `scenes-no-root-ui` | no legacy root-UI globals (`soundEditor`, `sessionView`, ...) or UI-stack calls (`openUI`, ...) |
| `services-no-oled-timer-claim` | no `Service` claims the OLED in `timer_repaints()` (see Services) |

By convention `board_surfaces.cpp` is the only `.cpp` that includes `libdeluge/display.h` or
`libdeluge/control_surface.h`; the ratchet does not check that.

## Board profile (`profile.h`, `profiles/`)

`BoardProfile` is a view over board-owned tables: button matrix, `led_by_button`, encoder roles and
kinds (indexed by `ui::EncoderRole`), OLED roles and per-surface `Style` (indexed by `ui::Style`),
`sidebar_cols`, and the l10n bridge `text`/`error_text`. **Read the bridge only through
`text_or_digits`/`error_text_or_digits`**: a null hook renders the id as digits, never an empty
string. `shell::deluge_profile()` leaves both hooks null because the string table is outside this
layer; the application's `scenes::deluge_profile()` copies it and fills them in.

The Deluge profile numbers `LedId` the same as `ButtonId`, so the Play LED is
`static_cast<LedId>(ButtonId::Play)` and one table serves both. That is the profile's choice, not a
shell requirement.

## Surfaces (`surfaces.h`, `board_surfaces.h`)

`SurfaceSet` holds up to `kMaxOled` (4) `OledSurface`s, one `GridSurface` and one `LedSurface` in a
fixed pool sized by `build()`. Each surface has a `RepaintGate`; every OLED surface the shell
renders needs a non-null `style`. The sinks (`blit`, `oled_busy`, `send_columns`,
`pad_output_space`, `flash_pad`, `set_led`, `release_led`, `set_indicator`, `flush`) are called
**only from `Shell::flush`**. Three backends:

| backend | sinks go to |
|---|---|
| `BoardSurfaceSet` | libdeluge |
| `uitest::FakeSurfaceSet` (`tests/unit/spec/shell/support/fake_surfaces.h`) | recorders, so a spec runs the exact flush path |
| `GuestSurfaceSet` (`gui/ui/shell_guest_surfaces.h`) | legacy's `OLED::main`, `PadLEDs::image`, `indicator_leds` |

`GuestSurfaceSet` **copies; it does not alias**: `PadGrid` is wider than `PadLEDs::image`, and legacy
calls `OLED::clearMainImage()` all over the tree, which would blank an aliased retained-mode buffer
with nothing to re-dirty it.

`DelugeBoard::pad_grid_width` counts main columns only; the sidebar comes from
`BoardProfile::sidebar_cols`, and `GridSurface::pairs` counts column pairs across both (9 on the
Deluge).

## Input (`input_lowering.h`, `router.h`)

**Lowering.** `InputLowering` owns all input state (held buttons and pads, press times, used-up
presses, the sticky-shift latch, partial detents, the gold-knob wiggle debounce). `begin_tick`
clears the per-tick state and claims; `end_tick(now_ms)` stamps the tick's clock and assembles the
snapshot every frame reads. The routed FIFO survives `begin_tick`: a late event is delivered on a
later tick, not dropped.

**The tick window.** Input arrives *between* ticks, so the shell opens the window lazily on the first
feed (`on_board_event`, `on_encoder_edges`, `set_sd_busy`) or at `pump_input`, and closes it at the
bottom of `tick()`. A host whose scenes leave without a tick calls `discard_pending_input()`, which
closes the window and drops what it held, so nothing fed for them reaches the scenes up next.

| gate | effect | for |
|---|---|---|
| `set_sd_busy` | **defers** board events and replays them on release; encoder edges are never deferred | legacy's `waitingForSDRoutineToEnd` |
| `set_input_lock`, `Action::LockInput` | **drops** every event but `NO_PRESSES`, encoder edges included | the song-load and stem-export bracket only |

The input lock is one global bool with no owner. A scene removed by anything but its own `Result`
cannot release it from `on_exit`, and the device goes deaf, so a scene that wants to ignore input
swallows it in `handle()` instead.

`Shell::route_midi` (MIDI learn) and `Shell::route_lowered_turn` (a gold turn the host already
debounced and accelerated) route synchronously, outside the lowering and its window.

**Widget-owned shapes.** The `Select`, `GoldUpper`, `GoldLower`, `Horizontal` and `Vertical` turns,
the select push and `Button{Back}` can be claimed by a widget (`is_widget_owned_shape`). `pump_input`
takes events in arrival order and asks the input-frontmost scene `Scene::owns(event)` for each such
shape: **owned**, it stays in the snapshot for the render and is not routed; **not owned**, it is
`withdraw()`n from the snapshot and routed. Everything else is routed. **Nothing is delivered after
the render.** Once the render owns something this tick, an event that would act now, or a second
press of an owned edge, waits with everything behind it for the next tick (`defer_rest`), so it sees
what came before it applied, as legacy's serial dispatch does. Mismatches between `owns` and what
the widgets claimed are counted in `ownership_audit()`; the scenes-spec runner fails on non-zero.

**Routing.** `Router::route` runs the legacy chain **input-top scene -> `mod_knobs` ->
`global_controls` -> `midi_learn` -> dropped**. `mod_knobs` sees only mod buttons, gold turns and
gold pushes, and only after the scene declined them: legacy's `UI::modEncoderAction`,
`modButtonAction` and `modEncoderButtonAction` forward to `View`, so the current UI has first
refusal. Each `Result` is `apply()`d as it returns; the first `handled` stops the chain. The scene
step uses `SceneStack::input_top()`, the topmost scene that is not `input_transparent()`, so a
popup, console line or working animation never takes the input of what it is drawn over.

## Scenes and the stack (`scene.h`, `scene_stack.h`)

Scenes are long-lived singletons the application registers once in a `SceneRegistry` indexed by
`SceneKind`; the stack holds pointers, at most `kStackDepth` (8). Kinds `Null` to `Notification` are
the shell's; applications number from `FirstApp` (16) up to `kMaxSceneKinds` (48). An empty stack's
`top()` is `NullScene`, which draws nothing and swallows everything.

**A scene never mutates the stack.** It returns up to 8 `Action`s in a `Result`, applied after the
call returns. The overlay services (`Notices`, `Screensaver`) call `SceneStack` directly; scenes do
not, and no lifecycle hook calls back into the stack.

| `Action::Kind` | effect |
|---|---|
| `Push`, `Replace`, `ReplaceRoot` | `push`, `replace_top` (sideways), `replace_root` |
| `Pop` | pop down to and including `pop_to`; `Null` means "whatever is on top" |
| `Remove` | excise the topmost `pop_to` scene, leaving the rest in place |
| `Popup` | show `popup_string`, resolved through `text_or_digits` |
| `Repaint`, `RepaintAll` | request one OLED surface; every OLED surface **and the grid** |
| `LockInput` | the input lock, above |
| `UseUpSelect` | Select's release does not act |

A scene closing **itself** uses `Remove` with its own kind: a notice overlay may be pushed above it
between render and apply, and `Pop` would take that overlay down too.

Lifecycle (table in `scene_stack.h`): `on_enter` may refuse, rolling the operation back.
`on_resume` fires only when `input_top()` changes hands, so a passive layer coming or going
notifies nobody (legacy's `focusRegained`). `replace_root` saves and restores `SceneState` (256
bytes per `SceneKind`, keyed by kind not `arg`, unversioned); `remove` saves but never restores;
`pop` and `replace_top` do neither. `changed_since_last_query()` is set by any operation that ran a
lifecycle hook, and by none that touched nothing.

## Services (`service.h`, `notices.h`, `screensaver.h`)

A `Service` is scene-shaped but never pushed and has no surface: `handle`, `leds`, `tick`,
`next_timer`, `timer_repaints`, all defaulting to nothing. `Services` holds the three routed slots
(`mod_knobs`, `global_controls`, `midi_learn`), each defaulting to `null_service()`. The shell's own
`Notices` and `Screensaver` are ticked and asked for LEDs and timers but not routed to. A service in
two slots is ticked once but routed to once per slot.

The real services (`src/scenes/services/`) are built over the shell's own `stack()` and
`lowering()`, so the application builds the `Shell` with `Services{}`, constructs them, then calls
`Shell::set_services`; re-installing is safe. A service may also reach shared lowering state
directly (`ModKnobs` calls `InputLowering::pretend_knob_untouched`).

`Service::timer_repaints()` defaults to `{false, false}`: a service timer is a wake only. A service
has no stack position, so a claim is honoured unconditionally, and an OLED claim is an unconditional
periodic blit on every panel -- hence the ratchet.

**`Screensaver`** pushes `SceneKind::Screensaver` (the application's `ScreensaverScene`) after
`idle_ms` without input. The first refused push **latches it off** (`screensaver_push_refused()`)
rather than retrying every tick. The host supplies `set_screensaver_idle_ms` on every settings
change, `note_activity` for controls it handled itself, and `set_screensaver_inhibit`. MIDI does not
wake it, as in legacy.

**`Notices`** drains `notify::notices()`: errors and text to `PopupScene`, console lines to
`ConsoleScene`, the working animation to `WorkingScene`, notifications to `NotificationScene` -- or,
with a `NotificationSink` installed, to legacy's own popup slot, so either queue consumer produces
the same legacy call. Popup and banner share one slot and displace each other, as in legacy. Cancels
use `remove`, so a scene pushed above a popup survives it.

## The tick (`shell.h`)

`Shell::tick(now_ms)` is the whole UI once, in place of legacy's `doAnyPendingUIRendering`,
`uiTimerManager.routine`, the UI half of `readButtonsAndPadsOnce` and `GRAPHICS_ROUTINE`. Step
numbers match the comments in `Shell::tick`:

1. **Model facts.** Drain `notify::dirty()` (`DirtyConsumer::Shell`, its own copy, so legacy's drain
   never takes a fact first) -> `on_model_changed` on every scene, then the gates: `Oled` -> the
   OLED surfaces; `GridMain`/`GridSidebar`/`ClipContent`/`NoteRows` -> the grid; anything else ->
   both. A bare `SongSwapped` then `replace_root`s to the song hooks' root, if set.
2. **Notices** into the notice scenes.
3. **Input.** `pump_input`: `end_tick`, then route every event in arrival order, leaving owned widget
   shapes for the render. A router-stored `Popup` is shown here.
4. **Time.** `tick_stack`: scenes top-down, stopping below the first `occludes_grid()`, then the
   services. A scene pushed since the last tick gets `dt = 0`. A changed stack re-queries the
   greyout (`SceneStack::topmost_greyout`, legacy's top-down `getUIGreyoutColsAndRows`) and
   requests every gate.
5. **OLED**, per surface, if `gate.needs_frame || widgets_gate || router.repaint_requested(role)`.
6. **Grid**, if `gate.needs_frame || widgets_gate || router.grid_repaint_requested()`. Then clear
   the router's repaint requests and audit ownership.
7. **`after_render()`** once per scene rendered this tick, applied as it goes. A repaint it requests
   arms the gate at a zero deadline, so the next tick happens and draws it.

- **8b. Scene timers**: each stacked scene's `next_timer()` arms the surfaces `timer_repaints()`
  names, only where the scene can paint (at or above `grid_base()` / `oled_lowest(role)`).
- **8c. Service timers**, the same fold, unconditionally.
- **8d. Late facts**: drain the dirty set again; facts raised in `handle`, `tick` or `after_render`
  reach the scenes now and arm their gates for the next tick. Between ticks, `next_wake(now_ms)`
  answers `now_ms` while a fact is pending.

9. **`derive_leds`, `flush`**, then recompute `next_wake()`.

The steps rely on:

- `needs_frame`'s `model_dirty` is always `false`: step 1 already requested exactly the named gates.
- The widgets' gate is evaluated once (it consumes the request) and OR'd into every surface.
- Router repaint requests are cleared **between 6 and 7**: 5 and 6 are their only readers, and step
  7's must survive to the next tick.
- Every absolute deadline compares in difference form, `static_cast<int32_t>(now_ms - deadline) >=
  0`, because the `uint32_t` clock wraps after about 49.7 days.

### What a scene may read, and where

**Focus.** Only `input_top()` is routed to and rendered with `InputMask::all()`; every other layer
renders with `none()`, painting normally but reading 0/false and claiming nothing.

**Covered scenes.** A scene beneath the input-frontmost one renders *covered*
(`ui::FrameInput::covered`): it reads no pads, held pads, shift or held Select. A cover can pass
them down with `Scene::passes_pads_beneath` (the sample browser's context menu types into the
browser's keyboard). Pads go to whoever could read them when the tick's first pad event arrived,
so a press that closes a context menu is not typed into the scene it uncovers.

**A scene reads any one input in at most one render pass per tick.** Reading through a frame *is*
claiming it (`ui/core/frame_context.h`, rule 2), and a scene on two surfaces builds two
`FrameInput`s, so reading `f.turn()` in both `render_oled` and `render_grid` trips
`InputClaims::double_claim`. Read it in the pass that owns it and draw the resulting state on the
other; a scene that needs the raw event on both does not own it and reacts in `handle()`.

### Composition and timers

- **OLED**: walk down from the top while each scene is `oled_transparent(role)`, then draw up --
  `OLED::sendMainImage`'s layering as a per-scene property.
- **Grid**: draw the topmost scene that is not `is_grid_overlay()`, the overlays over it, then the
  greyout at legacy's full strength, unless `ShellConfig::greys_in_send_path`. Nothing else dims
  what is under an overlay. Notice scenes are grid overlays that paint nothing, so the pads keep
  showing the view, as with legacy's OLED-only popups.
- **Tweens** (`Shell::start_tween`): `Fade`/`ScrollH`/`ScrollV` blend a snapshot toward the live
  grid; `GreyOut` ramps the greyout inside the stack's mask, replacing the static greyout while it
  runs.
- **Timers**: `Scene::next_timer()` means "repaint me later" -- step 8b arms what
  `timer_repaints()` names, so overriding `next_timer()` is enough for a blink. The default claim is
  `{true, true}`, but `RepaintSurfaces`' members default to `false`, so `{.grid = true}` arms only
  the grid. Claim the OLED only if its content changes on the timer: the grid flush diffs against
  `last_sent`, an OLED surface is blitted whenever it rendered. Zero means "next tick".

## Flush and LEDs (`shell.h`, `leds.h`, `mirror.h`)

`Shell::flush` runs once per tick after the LEDs are derived:

- **OLED**: `blit()` each surface that rendered and is not `oled_busy()`; a busy one keeps
  `blit_pending` for next tick. The `Main` blit feeds `Mirror`, which throttles to one send per
  `min_interval_ms` and redelivers a held-back frame instead of dropping it.
- **Grid**: column pairs diffed against `last_sent`, sent within `pad_output_space() /
  kBytesPerColumnPair` (legacy's guard in `sendOutMainPadColours`, as a per-pair budget). A pair
  skipped for budget arms a repaint deadline so `next_wake()` reports the backlog. This tick's pad
  flashes go out through `flash_pad`.
- **LEDs**: by board LED index (`LedId`'s value), diffed against `last_on`. An LED nothing declares
  is never driven; one the shell sent and nothing declares any more is handed back once through
  `release_led`.

`LedCompose::compose` declares **services first, then the stack bottom to top**; the last writer
wins, so the top scene owns every LED it mentions. `led_is_on` is legacy's blinker table as a pure
function. A `LedState::shared(rate)` blink reads the shell's one phase for that rate (legacy's
`indicator_leds::ledBlinkState`), restarted by `LedFrame::restart` (legacy's `blinkLed`) and when an
LED starts blinking on it.

## The firmware host

`gui::ui::ShellHostUI` (`gui/ui/shell_host_ui.h`) is a legacy `UI` holding one `Shell` over a
`GuestSurfaceSet`, opened by `openShellMenu` (`util/shell_menu_entry.h`) unless the Legacy Menu
runtime setting is on.

| shell | guest |
|---|---|
| `tick` | `timerCallback`, re-arming `UI_SPECIFIC` from `next_wake()` with a 1 ms floor |
| `on_board_event`, `on_encoder_edges` | `padAction`, `buttonAction`, the select/horizontal/vertical encoders |
| `route_lowered_turn`, `route_midi` | `modEncoderAction`; the MIDI-learn hooks |
| `set_services` | the real services (`gui/ui/shell_guest_services.h`) |
| `Scene::pad_regions` | a region no stacked scene draws is left to legacy, so the clip view shows under a menu |
| `Scene::grid_pad_claim` | unclaimed pads go to the root view (`gui/ui/shell_pad_forward.cpp`) |
| greyout | `greys_in_send_path`: legacy's `prepareColour` greys each pad once by the guest's mask |
| `occludes_grid()` | `canSeeViewUnderneath`, so the root view keeps animating under the menu |

**Host-side mailboxes.** A request that is not a scene -- open a legacy view, start a stem export,
claim a sample -- is not an `Action::Kind`, which would put legacy root-UI concepts into the shell.
It is a scene-owned one-slot outbox: `MenuScene::take_action()`, drained by `dispatchShellActions`,
and `BrowserScene::take_choice()`, drained by `dispatchShellChoices`. Neither announces it is full
and both overwrite, so the host polls every tick:

1. **after `Shell::tick`** -- actions are filed in step 7, choices while step 3 routes;
2. **before any early return** (the close on an empty stack), or the closing tick's request strands;
3. **actions, then choices** -- both can fill on one tick, since a deferred choice closes the
   browser and the menu beneath gets that tick's `after_render`.

## How to add

- **A scene.** Subclass `Scene`; override `kind()` and `handle()` (return `Result::unhandled()`
  explicitly to ignore input). Draw in `render_oled`/`render_grid`, turn widget responses into
  navigation in `after_render()`, and make `owns()` answer exactly the widget-owned shapes the
  render claims. Declare composition (`is_grid_overlay`, `occludes_grid`, `oled_transparent`,
  `input_transparent`, `pad_regions`, `grid_pad_claim`, `greyout_mask`), any timer
  (`next_timer`/`timer_repaints`) and `save_state`/`restore_state`. Register it under a kind from
  `FirstApp` up.
- **A service.** Subclass `Service`; give it a `Services` slot to be routed to. Leave
  `timer_repaints()` at `{false, false}` unless a surface changes on its timer.
- **A board.** A `BoardProfile` under `profiles/`, with a `Style` for every rendered OLED surface,
  and a `SurfaceSet` backend if the sinks differ.
- **A host request.** A scene-owned outbox drained after the tick, as above -- not an `Action::Kind`.

## Testing

The shell's specs are in `tests/unit/spec/shell/` (`./dbt test unit`). `uitest::FakeSurfaceSet`
records every sink call; `tests/golden/shell/*.pbm` pin the popup at both panel heights and an
overlay's composition. `Shell::state_lines()` is the shell's half of the UI scripts' `.state`
snapshot. At firmware level, `tests/ui-scripts/app/menu.txt` runs on two cards that differ only in
the Legacy Menu setting, ShellMenu on and off (`tests/fixtures/card`, `tests/fixtures/card-legacy-menu`), and
`scripts/ui_app_ab_check.py` requires every OLED panel of the two runs to be byte-identical.

## Not yet ported

- **Hardware grid scroll.** `SurfaceSet` has no sink for a board's column-shift opcodes, so
  `ScrollH`/`ScrollV` tweens redraw every pair every step.
- **A per-frame wake.** A scene returning `0u` from `next_timer()` (`AudioClipScene` while playing)
  makes `next_wake()` report now every tick; under the guest's 1 ms floor that is a grid render and
  legacy's timer sweep at up to 1 kHz, where legacy's `graphicsRoutine` runs at about 66 Hz. The fix
  is a distinct per-frame cadence for the host, not a new meaning for zero.
