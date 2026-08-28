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
| `ShortcutTable` | Pad-index -> param-id tables for a shortcut layer (`layer0`, `layer1`; spec E fills it in) |
| `TextLookup`, `ErrorLookup` | The shell's l10n bridge -- the shell has no l10n of its own |
| `BoardProfile` | The full board description: button matrix, LED map, encoder roles/kinds, OLED surface roles and per-surface `Style`, shortcuts, the l10n bridge, and the matrix dimensions |
| `button_name(ButtonId)` | For `.state` files and diagnostics |
| `FixedVector<T, N>` (`fixed_vector.h`) | A no-heap, fixed-capacity vector; `push_back` fails soft past `N` |

`BoardProfile::encoder_by_index` and `encoder_kind` are indexed by `deluge::ui::EncoderRole`
(`ui/core/input.h`) and `style_by_index`/the style fields by `deluge::ui::Style`
(`ui/core/style.h`) -- both from the finished toolkit, not redefined here.

Concrete board tables (e.g. the Deluge's own matrix/LED/encoder layout) are a later task; Task 1
is the vocabulary only.

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
   router stored is resolved through `BoardProfile::text` and handed to `Notices::show_popup`. The
   window itself is closed at the bottom of `tick()`, not here.
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
9. `derive_leds`, then `flush` (Task 8).

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
legacy has only the greyout, and the dim is unconditional on "an overlay is above" -- spec D §7
names no exception for an overlay that paints nothing, and the shell cannot know what one painted
without rendering it and diffing. A notice therefore dims the pads beneath it while it is up.

The three notice scenes are grid overlays that paint nothing (`is_grid_overlay()` true,
`occludes_grid()` false, no `render_grid`): the grid base is the topmost scene that is *not* an
overlay, so without that flag a pushed notice would become the base and -- since `render_grid`
clears first -- leave the pads black. Legacy's popups, console lines and working animation are
OLED-only; the pads keep showing the view underneath.

## LEDs (`leds.h`, Task 7)

| name | purpose |
|---|---|
| `led_is_on(state, now_ms)` | The whole `indicator_leds` blinker table (`indicator_leds.h:71-79`, `LED_BLINK*` timers at `kFlashTime`/`kFastFlashTime`) as a pure function: phase is `now_ms / period_ms`, so same-period LEDs blink together, and `until_ms` folds an alert back to On |
| `LedCompose::compose` | Services first, then the stack bottom to top; `LedFrame::set` overwrites, so the top scene wins for every LED it mentions and the ones beneath supply the rest |
| `led_send_needed` | The diff Task 8's flush drives `set_led` from; an LED nothing declared is never sent |

`Shell::derive_leds` composes into `LedSurface::frame` and resolves each declaration into
`LedSurface::desired_on`/`desired_declared`, both indexed by *board LED index* (`LedId`'s own
value), not by position in `frame.entries()`.
