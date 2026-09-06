# deluge_ui_shell

Surfaces, scene stack, input lowering, services, tick, flush (spec: `docs/superpowers/specs/2026-08-26-ui-shell-design.md`,
spec D of the UI stack redesign, amended by B.1 Task 9: `render_oled`/`render_grid`/`after_render`,
per-surface `Style`, input claims, `begin_tick`).

Rules:
1. `src/shell/**` may include only `shell/**`, `ui/**`, `gfx/**`, `notify/**`, `libdeluge/**` and
   the standard library -- never `model/`, `gui/`, `hid/`, `definitions*`, `board_config.h`.
2. `snake_case` functions/members, `PascalCase` types, `k` constants; namespace
   `deluge::ui::shell`.
3. No mutable globals; a `BoardProfile` is a view over board-owned storage, not an owner.

## Board profile (`profile.h`, Task 1)

The board-independent vocabulary a `BoardProfile` is built from:

| type | purpose |
|---|---|
| `ButtonId` | Every physical button/encoder-push the shell can name (`Count` bounds a `ButtonSet` bit and `button_name()`, and is asserted `<= 64`) |
| `SurfaceRole` | Which surface a UI event/render call targets (`Main`, `Param0..3`, `Aux`, `None`) |
| `EncoderKind` | `Detented` or `Accelerating` |
| `ButtonSet` | A `uint64_t`-backed bitset of `ButtonId`s (`has`/`set`/`any`) |
| `TextLookup`, `ErrorLookup` | The shell's l10n bridge -- the shell has no l10n of its own |
| `TextScratch`, `BoardProfile::text_or_digits`/`error_text_or_digits` | The one place those two hooks are read: a null hook renders the id as digits into the caller's `TextScratch`, never an empty string |
| `BoardProfile` | The full board description: button matrix, LED map, encoder roles/kinds, OLED surface roles and per-surface `Style`, the l10n bridge, and the matrix dimensions |
| `button_name(ButtonId)` | For `.state` files and diagnostics |
| `FixedVector<T, N>` (`fixed_vector.h`) | A no-heap, fixed-capacity vector; `push_back` fails soft past `N` |

`BoardProfile::encoder_by_index` and `encoder_kind` are indexed by `deluge::ui::EncoderRole`
(`ui/core/input.h`) and `style_by_index`/the style fields by `deluge::ui::Style`
(`ui/core/style.h`) -- both from the finished toolkit, not redefined here.

Concrete board tables (e.g. the Deluge's own matrix/LED/encoder layout) are a later task; Task 1
is the vocabulary only.

`deluge_profile()` sets `text` and `error_text` to nothing on purpose: the `l10n::String` table
they resolve against is spec E's and wiring the firmware's copy of it to this profile is spec F's. Until then every popup string id and error code shows as its own digits --
which is what `text_or_digits`/`error_text_or_digits` do with a null hook, and the reason no call
site is allowed to test the hook itself.

## Surfaces (`surfaces.h`, `board_surfaces.h`)

`SurfaceSet` is the shell's only hardware seam. Everything above it renders into the surfaces and
flushes through the sink virtuals; `BoardSurfaceSet` binds those sinks to libdeluge and the specs'
`uitest::FakeSurfaceSet` binds them to recorders.

| type | purpose |
|---|---|
| `OledSurface` | One panel: `Canvas` over the pool storage, its `Geometry`, a `RepaintGate`, its `SurfaceRole`, `index`, `blit_pending`, and the profile's `Style*` for it |
| `ColumnPair` | The 16 pad colours of one main-grid column pair, in the legacy `sortLedsForCol` order |
| `ActiveTween` | A grid animation the shell's tick drives (`Fade`, `ScrollH`, `ScrollV`, `GreyOut`) |
| `GridSurface` | The `PadGrid` plus the send-side shadow (`last_sent`/`sent_once`) the legacy `sendOutMainPadColours` lacks |
| `LedSurface` | The `LedFrame` plus the LED and knob-indicator send-side shadow |
| `SurfaceSet` | The surfaces, `oled(SurfaceRole)` lookup, and the sink virtuals |
| `BoardSurfaceSet` | The libdeluge-backed `SurfaceSet`; the **only** `.cpp` here that includes `libdeluge/display.h` / `libdeluge/control_surface.h` |

Storage is a fixed pool: `kMaxOled` (4) surfaces of `kOledStorageBytes` (128x64/8) each, sized at
`build()` time from the board descriptor. No heap.

`DelugeBoard::pad_grid_width` counts **main** pad columns only (16 on the Deluge) and the
descriptor has no sidebar field, so the sidebar width comes from `BoardProfile::sidebar_cols`
(default 2). The grid is `{pad_grid_width, sidebar_cols, pad_grid_height}` and
`GridSurface::pairs` counts pairs across the *total* width -- 9 on the Deluge.

## Input lowering and routing (`input_lowering.h`, `router.h`, Tasks 4–5)

One `InputLowering` sits between the board and every scene. It owns every piece of state legacy
scattered across `Buttons::`, `MatrixDriver::padStates` and the encoder accumulators -- held
buttons and pads, when each press began, whether a press was already consumed, the sticky-shift
latch, and each encoder's partial detent -- and turns raw board events into the shell's own
`UiEvent` vocabulary. A tick is a begin/feed/end machine: `begin_tick` clears the per-tick state
(the routed FIFO is the one exception -- it is drained by `next()`, so an event that arrived while
the shell was busy still gets delivered next tick, not dropped); `end_tick` stamps the tick's clock
onto the `ui::InputSnapshot` every widget's frame reads. Six shapes -- five encoder turns
(`Select`, `GoldUpper`, `GoldLower`, `Horizontal`, `Vertical`), the select encoder's own push, and
`Button{Back}` -- are the toolkit's widget-owned inputs; they still update the lowering's
bookkeeping and reach the snapshot every tick, but are held back from routing into `held_back()`
rather than delivered during `pump_input`, because whether a widget wants them is only knowable
once render has happened. `Router::route` delivers everything else immediately, in the legacy
order top scene -> mod-knobs -> global controls -> MIDI learn -> dropped; `Router::route_post_render` delivers
the held-back six the same way, once per tick, after render, skipping any input `ctx().claims`
shows a widget already took. Neither step ever touches `SceneStack` directly -- both apply the
`Result`'s `Action`s through the router, which is what keeps a scene's `handle()` from ever
re-entering the stack it is itself being called through.

## Scenes and the stack (`scene.h`, `scene_stack.h`)

A scene is one screen's worth of behaviour: what it draws on each surface, what it does with an
event nothing else claimed, and what it remembers. Scenes are long-lived singletons the
application owns and registers once; the stack only holds pointers.

| type | purpose |
|---|---|
| `SceneKind` | The registry index and state-slot key; `Null..Screensaver` are the shell's own, applications number from `FirstApp` (16) |
| `SceneId` | `{kind, arg}` -- the navigation target; `arg` is a clip/output/browser index the target scene interprets |
| `SceneState` | 256 opaque 8-aligned bytes per `SceneKind`, saved and restored across a `replace_root` |
| `UiEvent` | One lowered event, as a tagged POD struct (`Pad`, `Button`, `Encoder`, `EncoderButton`, `AllReleased`) |
| `UiContext` | The world around one event: held buttons and pads, shift, the clock, the board profile, and the `InputLowering` behind `press_used_up`/`held_since` |
| `Action`, `Result` | What a scene asks the shell to do (`Push`/`Replace`/`ReplaceRoot`/`Pop`/`Popup`/`Repaint`/`LockInput`) and whether the event was handled |
| `Scene` | The interface; only `kind()` and `handle()` are pure, everything else defaults to nothing |
| `Scene::dims_grid` | Whether what is beneath this scene is dimmed for it. Defaults to `is_grid_overlay()`; an overlay that composites nothing (the notice scenes) returns false |
| `Scene::input_transparent` | "Drawn over what is beneath, but never takes its input" -- popups, console lines, the working animation. `SceneStack::input_top()` walks past these to the scene that is routed to and focused |
| `SceneRegistry` | `std::span<Scene* const>` indexed by kind; `find()` is null for an unregistered or out-of-range kind |
| `NullScene` | What `SceneStack::top()` reports when the stack is empty: draws nothing, swallows everything |
| `SceneStack` | `push`/`pop`/`replace_root`/`replace_top`/`reset` over at most `kStackDepth` (8) scenes |

A scene never mutates the stack: it returns `Action`s and the shell's router applies them once the
call has returned, so a scene cannot be exited or re-entered inside its own method. Only
`replace_root` saves and restores `SceneState`; `pop` and `replace_top` do not. Every stack
operation sets `changed_since_last_query()`, which is deliberately conservative -- a redundant
greyout re-query is cheaper than a stale one.

`UiContext::press_used_up`/`held_since` are declared with the struct but defined alongside
`InputLowering`; a context built with a null `lowering` (a spec, a synthesised event) answers
false and 0.

## Services (`service.h`, Task 5) -- the seam for spec E

A `Service` is a scene-shaped participant in the routing chain that is not itself pushed or
popped and has no render surface: `handle()`, `leds()`, `tick()`, `next_timer()`, `timer_repaints()`, all defaulting
to nothing. `Services{mod_knobs, global_controls, midi_learn}` is what a `Shell` is built with;
every unfilled slot defaults to `null_service()`, so the full legacy chain shape (the scene, then
the mod-knobs bar, then global transport/shift/record handling, then MIDI learn) runs with the
unfilled slots contributing nothing. `Notices` and `Screensaver` are the two services this task
actually implemented, because they need nothing from the model; `ModKnobs`, `GlobalControls` and
`MidiLearn` are the seam spec E filled in (E4 tasks 2-5) -- the model-owned `ModTarget`, transport
state and MIDI-learn table -- with the shell changing only twice: `Scene::is_root()` for the
`View`-arm reachability gate, and `Service::timer_repaints()` so a service timer can arm a gate the
way a scene timer does. Nothing in `src/shell` depends on their being real:
a `Service` is the whole contract, and a `NullService` satisfies it completely.

`Screensaver` pushes `SceneKind::Screensaver` but ships no scene of that kind — the animation is
spec E3's, registered by the application like any other scene. Until it exists the push is refused,
and **the first refusal latches the service off for good**: `active()` stays false, `next_timer()`
reports nothing, no further push is attempted, and `Shell::screensaver_push_refused()` counts it.
Without the latch the idle deadline stays past for ever, so every subsequent tick retried the push
— which, with a `SceneStack` that flagged even a refused push as a change, re-queried the greyout
and invalidated every gate once per tick: a full OLED render, blit and grid recompose on every tick
with `next_wake()` pinned to now. That is why `SceneStack` now flags only operations that touched
something (see `changed_since_last_query`).

**Every absolute deadline in the shell is compared in difference form**
(`static_cast<int32_t>(now_ms - deadline) >= 0`), never `now_ms >= deadline` — the popup and console
lifetimes, the screensaver idle deadline, an LED blink's `until_ms`, and `compute_next_wake`'s
choice of the earliest deadline. The clock is a `uint32_t` millisecond counter that wraps at ~49.7
days of uptime, and an absolute compare across that wrap reads a freshly armed (wrapped, therefore
small) deadline as long past. This is the form `RepaintGate`, `Mirror::due` and `InputLowering`'s
hold/press comparisons already use.

## The tick (`shell.h`, Task 7)

`Shell` is the whole UI once per tick, replacing the legacy `doAnyPendingUIRendering` /
`uiTimerManager.routine` / UI half of `readButtonsAndPadsOnce` / `GRAPHICS_ROUTINE` quartet
(`deluge.cpp:592-624`). It owns the stack, the lowering, the router, the three overlay scenes and
`Notices`; it borrows the `SurfaceSet` and the board profile, and allocates nothing.

`tick(now_ms)` runs spec D §7's order exactly:

1. `notify::dirty().drain()` → `on_model_changed` on every stacked scene, and the facts onto the
   gates (`Oled` → the OLED surfaces; `GridMain`/`GridSidebar`/`ClipContent`/`NoteRows` → the grid;
   anything else → both).
2. `notices_.drain(notify::notices(), now)` → the popup/console/working scenes.
3. `pump_input` — end the lowering's *input* phase (`end_tick(now_ms)`, which stamps the tick's
   clock onto the snapshot every frame reads) and route the ordinary shapes; a `Popup` action the
   router stored is resolved through `BoardProfile::text_or_digits` and handed to
   `Notices::show_popup`. The window itself is closed at the bottom of `tick()`, not here. This
   bridge runs *before* steps 7 and 8, so a `Popup` action produced by `after_render()` or by
   post-render delivery is shown one tick later — `Router::popup_string_` is only cleared on
   consumption, so it is late, never lost.
4. `tick_stack` — scenes top-down, stopping below the first `occludes_grid()`, then the services.
   A stack that changed re-queries the union of `greyout_mask()` and invalidates every gate.
5. OLED, per surface: `gate.needs_frame(...) || widgets_gate || router_.repaint_requested(role)`.
6. Grid: `gate.needs_frame(...) || widgets_gate`.

`needs_frame`'s `model_dirty` argument is always `false`: step 1 has already requested exactly the
gates this tick's facts name (and every gate for a fact that names none), so passing `dirty.any()`
as well — spec D §7's shorthand, written before the shell had that channel — would repaint the OLED
for a note-row change and the grid for an OLED one. A `Result` cannot request a *grid* repaint
today: `Action::Repaint` names a `SurfaceRole` and there is no grid role, so the grid decision omits
`repaint_requested`. Left as-is pending a ruling; the model side reaches the grid gate through
`notify::Dirty` instead.
7. `after_render()` on every scene rendered this tick, applied through the router.
8. `deliver_post_render_input` — the held-back, widget-owned shapes, each gated on `ctx().claims`.
8b. **Every stacked scene's `next_timer()`, armed as a repaint deadline on whichever of its own
   surfaces `Scene::timer_repaints()` names.** "Wake me later" and "repaint me later" are one thing,
   not two: a timer used to feed `next_wake()` only, so an animated scene *also* had to call
   `request_repaint_after` from its own `render_*` — and four scenes in a row shipped or nearly
   shipped without doing so (E2's menu cursor blink, `BrowserScene`, `MarkerScene`, `RecorderScene`,
   where the cost was a recording that never noticed it had finished). `timer_repaints()` returns
   `{oled, grid}`, both true by default; `{false, false}` is the opt-out for a timer with no visible
   consequence at all, and it is the *only* case `compute_next_wake`'s scene loop still exists for —
   every scene that names at least one surface reaches `next_wake()` as that surface's own gate
   deadline. The split matters because the grid's `flush` diffs against `last_sent` and the OLED
   panels' does not (`flush` blits an OLED surface unconditionally once rendered): `MenuScene`
   overrides `{.oled = false, .grid = true}` because its timer is the shortcut/source pad blink
   cadence alone, and arming the OLED gates from it too meant both panels blitting unconditionally,
   forever, whenever a patched-param leaf was focused (E3 task 10 fix round 1).
9. `derive_leds`, then `flush` (Task 8).

**Repaint requests are cleared between 6 and 7, not at the bottom of the tick.**
`Action::Repaint`/`RepaintAll` set a flag on the `Router` and nothing else — no surface, no gate —
so step 5 is their only reader. Steps 7 and 8 both `apply()` results of their own, and a clear
placed after them set and wiped such a request inside one tick with nothing reading it in between:
a menu scene that advanced its selection from a post-render-delivered detent and asked for a
`Repaint{Main}` got no frame at all until some unrelated event fired a gate. What steps 7 and 8
request instead lands on the named surfaces' own `RepaintGate`s at the bottom of the tick, at a
zero-millisecond deadline, so `next_wake()` reports `now` and the next tick both happens and
renders.

The widgets' gate is evaluated **once** per tick (`needs_frame` consumes the request) and the
answer OR'd into every surface's decision.

**The tick window.** `InputLowering::begin_tick` clears the held-back queue, the claims and
`had_input_this_tick`, but input arrives *between* ticks -- so the shell opens the window lazily on
the first feed (or at `pump_input`) and closes it at the *bottom* of `tick()`. Nothing between
`end_tick()` and the end of the tick calls `begin_tick()`, which is what lets step 8 still read
`held_back()` and `claims` after the frame has been built.

**Focus and routing.** Exactly one scene per tick owns the input: `SceneStack::input_top()`, the
topmost scene that is not `Scene::input_transparent()`. It is the scene `Router` calls `handle()`
on, and the only one whose render frames are built with `InputMask::all()` — every other layer in a
pass, above or below, draws with `none()` and so paints normally but reads 0/false and claims
nothing. The three notice scenes are `input_transparent`, because legacy routes nothing to a popup,
a console line or the working animation; without the flag, a console message would deafen the view
it is drawn over for as long as the line is up.

**A scene reads any one input in at most one render pass per tick.** Reading through a frame *is*
claiming it (`ui/core/frame_context.h`, rule 2), and a scene drawn on two surfaces builds two
independent `FrameInput`s in one tick — reading `f.turn()` in both `render_oled` and `render_grid`
claims the same detent twice and trips `InputClaims::double_claim`. A scene whose two surfaces both
react to one input reads it in the pass that owns it and draws the resulting state on the other; a
scene that needs the raw event on both leaves it unclaimed and reacts in `handle()`, which the shell
delivers *after* render for exactly that reason.

**Composition.** `render_oled` walks down from the top while each scene is `oled_transparent(role)`
and draws from there up -- the port of `OLED::sendMainImage`'s two-layer if-chain
(`oled.cpp:364-376`) as a property each scene declares. `render_grid` draws the topmost
non-overlay scene, dims it by `RGB::dim(1)` (`>> 1` per channel) if anything overlays it, draws the
overlays, then applies the stack's greyout at legacy's full strength (`setGreyoutAmount(1)` →
`greyProportion = 6500000`, `pad_leds.cpp:753,786-788`). The dim is **spec D design, not a port**:
legacy has only the greyout. Spec D §7 says "dim if any overlay is above"; the shell narrows that
to "if any overlay above the base `dims_grid()`", so an overlay that composites nothing can opt out.

The three notice scenes are grid overlays that paint nothing and dim nothing (`is_grid_overlay()`
true, `dims_grid()` false, `occludes_grid()` false, no `render_grid`). Both flags are load-bearing
and separate: the grid base is the topmost scene that is *not* an overlay, so without
`is_grid_overlay()` a pushed notice would become the base and -- since `render_grid` clears first --
leave the pads black; and with the dim left on, a "SAVED" popup would halve the brightness of a grid
it never touches. Legacy's popups, console lines and working animation are OLED-only; the pads keep
showing the view underneath, unchanged.

## Flush (`shell.cpp`, Task 8) -- the seam for spec F

`Shell::flush` is the only place the shell touches `SurfaceSet`'s sink virtuals, once per tick,
after LEDs are derived:

- **OLED**: one `blit()` per surface that rendered this tick and is not `oled_busy()` (the
  `deluge_display_busy` skip around legacy's `sendMainImage`); a busy panel keeps `blit_pending`
  armed and is retried next tick. The `Main` surface's blit additionally feeds `Mirror::after_blit`,
  which throttles sends to `MirrorSink` to at most one per `min_interval_ms` and -- unlike a naive
  throttle -- never drops a frame the throttle held back: `Mirror::retry` redelivers the pending
  frame once the interval has passed, every tick, independent of whether that tick blitted anything.
- **Grid**: column pairs are diffed against `last_sent` and sent within a budget derived from
  `SurfaceSet::pad_output_space() / kBytesPerColumnPair` (legacy's `uartGetTxBufferSpace` guard,
  generalised from an all-or-nothing gate to a per-pair budget so a large backlog drains over
  several ticks); a pair skipped for budget stays dirty and arms a real repaint deadline via
  `RepaintGate::request_repaint_after`, so `next_wake()` reports it rather than the scheduler
  sleeping past a backlog that still needs to drain.
- **Tweens** (`ActiveTween`, `Shell::start_tween`): `Fade`/`ScrollH`/`ScrollV` blend a snapshot
  taken at tween-start toward the live grid over `duration_ms`; `GreyOut` ramps `RGB::greyOut`'s
  proportion from 0 to a target *inside the stack's greyout mask only*, replacing (not stacking
  with) the tick's ordinary full-strength greyout pass while it runs -- the port of legacy's ramp
  (`pad_leds.cpp:983-1005`) feeding the same masked `prepareColour`. Every tween's step deadline is
  also a `request_repaint_after` call, for the same reason as the grid backlog above.
- **LEDs**: `derive_leds`'s composed `desired_on`/`desired_declared`, indexed by *board LED index*
  (`LedId`'s own value -- see the LEDs section below), diffed against `last_on` and sent through
  `SurfaceSet::set_led`/`set_indicator`.

`SurfaceSet` (`surfaces.h`) is the shell's only hardware seam, and `flush` is the only method that
calls its sink virtuals -- `blit`, `oled_busy`, `send_columns`, `pad_output_space`, `set_led`,
`set_indicator`. `BoardSurfaceSet` (`board_surfaces.cpp`) binds those to `libdeluge`, and is the
**only** `.cpp` in `src/shell` that includes `libdeluge/display.h` or `libdeluge/control_surface.h`
-- the two headers that actually name hardware, and the only `.cpp`, not the only file:
`input_lowering.h` includes `libdeluge/control_surface.h` for the raw event type it lowers.
Other `libdeluge/*` headers are ordinary dependencies here (`surfaces.h`, `profiles/deluge.cpp`
and `profiles/test_board.cpp` all include `libdeluge/board.h`).
The `shell-includes-outside` ratchet enforces the *layer* rule at the top of this file -- no
`model/`, `gui/`, `hid/`, `definitions*` -- and allows `libdeluge/` from anywhere, so the
display/control-surface narrowing is a convention this file records, not something the ratchet
checks. The specs' `uitest::FakeSurfaceSet` binds the same virtuals to recorders
instead, so a spec drives the exact code path a board does. Spec D §8's grid scroll opcodes
(hardware column-shift instructions some boards expose) are **not implemented** -- there is no
`SurfaceSet` sink for them yet, so `ScrollH`/`ScrollV` tweens always redraw every pair every step;
a board that wants the hardware path needs a new sink added to `SurfaceSet`, which is the seam
spec F (or a later amendment) would extend.

## LEDs (`leds.h`, Task 7)

| name | purpose |
|---|---|
| `led_is_on(state, now_ms)` | The whole `indicator_leds` blinker table (`indicator_leds.h:71-79`, `LED_BLINK*` timers at `kFlashTime`/`kFastFlashTime`) as a pure function: phase is `now_ms / period_ms`, so same-period LEDs blink together, and `until_ms` folds an alert back to On |
| `LedCompose::compose` | Services first, then the stack bottom to top; `LedFrame::set` overwrites, so the top scene wins for every LED it mentions and the ones beneath supply the rest |
| `led_send_needed` | The diff Task 8's flush drives `set_led` from; an LED nothing declared is never sent |

`Shell::derive_leds` composes into `LedSurface::frame` and resolves each declaration into
`LedSurface::desired_on`/`desired_declared`, both indexed by *board LED index* (`LedId`'s own
value), not by position in `frame.entries()`. `LedId` is the toolkit's opaque enum
(`ui/core/led_frame.h`); this board's profile chooses its values to equal `ButtonId`'s own, so a
scene declaring the LED under `ButtonId::Play` writes `static_cast<LedId>(ButtonId::Play)` rather
than a separately-numbered id -- one table (`led_by_button`) instead of two, at the cost of a
board profile that reuses another enum's numbering rather than inventing its own. A future board
is free to choose differently -- `LedId` and `ButtonId` are only required to agree here, by this
profile's own contract, not by anything in `shell.cpp` or `leds.h`.

## Testing

Every header above has a matching spec in `tests/spec/shell/` (`profile_spec.cpp`,
`profiles_spec.cpp`, `surfaces_spec.cpp`, `scene_stack_spec.cpp`, `input_lowering_spec.cpp`,
`router_spec.cpp`, `notices_spec.cpp`, `leds_spec.cpp`, `tick_spec.cpp`, `flush_spec.cpp`,
`lifecycle_spec.cpp`), built into the `all_specs` binary and run individually through CTest --
`layer_rules` (this file's ratchet script) is one more CTest entry alongside them, not a separate
mechanism. Two rigs make that possible without a board:

- **`uitest::FakeSurfaceSet`** (`tests/spec/shell/support/fake_surfaces.h`) implements every
  `SurfaceSet` sink as a recorder instead of a `libdeluge` call -- `BlitRecord`s, sent column
  pairs, sent LED/indicator states -- so a spec drives `Shell::flush`'s exact code path and reads
  back what would have gone to hardware. The same file's board/profile helpers (`test_profile()`,
  a two-OLED test board) are what `profiles_spec.cpp` and most of the others build a `Shell` or a
  `SurfaceSet` over.
- **Golden PBMs** (`tests/golden/shell/*.pbm`) pin exact OLED pixel output -- `popup_saved_48.pbm`/
  `popup_saved_64.pbm` for the popup scene at both panel heights, `overlay_48.pbm` for a grid
  overlay's composition -- so a rendering regression fails a byte comparison, not just a "something
  changed" assertion.

Event-script tests (spec G's acceptance suite: boot -> session -> press Clip -> snapshot every
surface + `.state`) are not part of this task; `Shell::state_lines()` exists for that harness to
call but nothing under `src/shell` depends on the harness existing.

## Archive size

```
$ ./dbt build Debug
$ ls -l build/src/shell/Debug/libdeluge_ui_shell.a
-rw-r--r-- 1 kate kate 1813358 Aug 28 18:34 build/src/shell/Debug/libdeluge_ui_shell.a
```

1.8 MB, unstripped, with full debug info -- the static archive alone, not a linked firmware image.
`./dbt sizediff` (the tool that reports the *linked* firmware's size delta against a baseline)
needs a full device link, which this environment cannot produce: the toolchain's ThinLTO Rust/C++
link step fails with `ld.lld: ... Invalid record` before a `.bin` ever exists (a pre-existing
environment limitation, not something this branch introduced -- see the plan's environment
rulings). The number above is the nearest substitute this environment can produce.
