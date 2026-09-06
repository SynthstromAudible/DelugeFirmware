#pragma once
#include "notify/dirty_set.h"
#include "shell/fixed_vector.h"
#include "shell/input_lowering.h"
#include "shell/leds.h"
#include "shell/loading_scene.h"
#include "shell/mirror.h"
#include "shell/notices.h"
#include "shell/profile.h"
#include "shell/router.h"
#include "shell/scene.h"
#include "shell/scene_stack.h"
#include "shell/screensaver.h"
#include "shell/service.h"
#include "shell/surfaces.h"
#include "ui/grid/effects.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace deluge::ui::shell {

/// @brief The board-independent settings one `Shell` is built with.
struct ShellConfig {
	/// Passed straight through to the `InputLowering` the shell owns.
	LoweringConfig lowering;
	/// @brief How long the shell may sit with no input before the screensaver scene is pushed; 0 is
	///        off.
	///
	/// @warning **Nothing computes this at boot yet**, so it stays 0 and the screensaver never
	///          shows. Legacy's number is
	///          `screensaverMode == ScreensaverMode::OFF ? 0 : screensaverTimeoutMinutes * 60000`
	///          -- both live in `FlashStorage`, which `src/shell` may not read -- and it has to be
	///          re-supplied whenever either setting changes, which is also the missing port of
	///          `Screensaver::settingsChanged()`. Recorded, with the rest of the boot wiring this
	///          stack does not do for itself, at `scenes/registry.h`'s boot-wiring note.
	uint32_t screensaver_idle_ms = 0;
	/// The seed the screensaver's animation is started from; passed through to the pushed
	/// scene's `SceneId::arg` (see `Screensaver`).
	uint32_t seed = 0;
};

/// @brief What the shell calls to find the application's root scene for a song, on the two paths
///        that ask for it (`Shell::song_loading(false)` and a bare `Dirty::SongSwapped`).
///
/// A free function pointer plus an opaque context rather than a `std::function`, matching the
/// rest of `src/shell`'s no-allocation rule; `ctx` is passed back unchanged, so an application
/// wanting member access closes over `this` there.
struct SongHooks {
	/// @param ctx The context below, passed straight through.
	/// @return The scene to make the new root.
	SceneId (*root_for_song)(void* ctx) = nullptr;
	void* ctx = nullptr;
};

/// @brief The shell's own name for \p k, for `.state` lines and diagnostics.
/// @param k The kind to name.
/// @return A static, NUL-terminated name for one of the shell's own kinds, or nullptr for an
///         application kind (`SceneKind::FirstApp` and up) and for a value the shell does not
///         know -- the shell has no vocabulary for an application's scenes, so `state_lines()`
///         spells those numerically instead of inventing a name for them.
const char* scene_kind_name(SceneKind k);

/// @brief The whole UI, once per tick: drain what changed, route what was pressed, tick the scene
///        stack, render every surface that needs a frame, and derive the LEDs.
///
/// One object replacing the legacy `doAnyPendingUIRendering` / `uiTimerManager.routine` / the UI
/// half of `readButtonsAndPadsOnce` / `GRAPHICS_ROUTINE` quartet (`deluge.cpp:592-624`). It owns
/// the scene stack, the input lowering, the router, the three overlay scenes and the `Notices`
/// service that drives them; it *borrows* the `SurfaceSet` (the only hardware seam) and the board
/// profile. Nothing here is global, and nothing on the tick path allocates -- `state_lines()`,
/// which builds a `std::string`, is the UI test harness's entry point and is never called from a
/// tick.
///
/// # The tick window
///
/// `InputLowering` is a begin/feed/end machine (`input_lowering.h`, "The tick"), and
/// `begin_tick()` is what clears the held-back queue, the tick's claims and `had_input_this_tick`.
/// Input, however, arrives *between* ticks -- the board polls and calls `on_board_event` long
/// before `tick()` runs -- so the shell cannot open the window at the top of `tick()` without
/// throwing away exactly the events that tick is meant to act on. The window is therefore opened
/// lazily by whichever comes first, a feed or `pump_input`, and closed at the *bottom* of
/// `tick()`:
///
///     on_board_event ... -> [begin_tick] feeds ... | tick: pump_input -> end_tick -> route
///       -> render -> after_render -> deliver held-back (claims still live) -> leds -> flush
///       -> window closed; the next feed re-opens it, which is what clears held_back()/claims.
///
/// That ordering is what lets `deliver_post_render_input` read `held_back()` and `ctx().claims`
/// after the frame has been built: nothing between `end_tick()` and the end of the tick calls
/// `begin_tick()`, so neither has been cleared yet. The window's *clock*, however, is the tick's,
/// not the opening feed's: `InputLowering::end_tick(now_ms)` re-stamps the snapshot, so
/// `f.now_ms()` is always the clock the frame is being built on.
///
/// # Repaint requests
///
/// **A `Router` repaint request is always read before it is cleared, and one raised too late to be
/// read this tick takes effect on the next one.** `Action::Repaint`/`RepaintAll` set a flag on the
/// `Router` and nothing else -- the router holds no surface and arms no gate -- so the flag's only
/// reader is `tick()`'s OLED loop (step 5). `router_.clear_repaint_requests()` therefore runs
/// immediately *after* the render decisions, not at the bottom of the tick: steps 7
/// (`after_render()`) and 8 (post-render delivery) both `apply()` results of their own, and a
/// clear placed after them would set and wipe such a request within one tick with no reader in
/// between -- losing it outright rather than delaying it. What those two steps request instead
/// lands on the named surfaces' own `RepaintGate`s at the bottom of the tick, at a zero-millisecond
/// deadline, so `next_wake()` reports `now` and the next tick both happens and renders.
///
/// # What a scene may read, and where
///
/// **A scene reads any one input in at most one render pass per tick.** Reading through a frame
/// *is* claiming it (`ui/core/frame_context.h`, rule 2), and a scene drawn on two surfaces builds
/// two independent `FrameInput`s in the same tick -- so reading `f.turn()` in both `render_oled`
/// and `render_grid` claims the same detent twice and trips `InputClaims::double_claim`. A scene
/// that wants one input to affect both surfaces reads it in the pass that owns it and lets the
/// other surface draw the state that produced; a scene that needs the raw event on both leaves it
/// unclaimed and reacts in `handle()`, which the shell delivers *after* render for exactly that
/// reason.
///
/// Only the **input-frontmost** scene (`SceneStack::input_top()`, the topmost that is not
/// `Scene::input_transparent()`) is drawn with `InputMask::all()`; every other layer in a pass
/// draws with `none()` and therefore reads 0/false and claims nothing.
class Shell {
public:
	/// @brief Build a shell over \p surfaces for the board \p profile describes.
	/// @param surfaces The hardware seam; borrowed, and must outlive the shell.
	/// @param profile  The board's tables; copied (a `BoardProfile` is a handful of spans).
	/// @param registry The application's scene table. Copied into the shell's own table, which
	///                 additionally holds the shell's own `Popup`/`Console`/`Working` scenes: the
	///                 shell owns those three kinds and `Notices` drives its own instances of
	///                 them, so an entry an application put in one of those slots is not used.
	/// @param services The mod-knob/global-controls/MIDI-learn services; unfilled slots default to
	///                 `null_service()`.
	/// @param config   Lowering and screensaver settings.
	Shell(SurfaceSet& surfaces, const BoardProfile& profile, const SceneRegistry& registry, Services services,
	      ShellConfig config);

	Shell(const Shell&) = delete;
	Shell& operator=(const Shell&) = delete;

	/// @brief Push the first scene.
	/// @param initial What to push.
	void init(SceneId initial);

	/// @brief Prepare the surfaces for the first frame, with nothing on the stack.
	///
	/// Ports legacy's boot sequence, which is only `renderUIsForOled` marking the OLED dirty
	/// (`deluge.cpp:120,133,371`, now `raise(Dirty::Oled)`): a repaint requested on every surface,
	/// so the first `tick()` after this call draws regardless of any gate's prior state.
	///
	/// **It pushes no scene, deliberately.** The stack stays empty until the application pushes its
	/// own root, and an empty stack draws nothing -- which is what legacy's firmware does before its
	/// first root frame. The logo a player sees at power-on is the *bootloader's*, not this
	/// firmware's: `hid/display/rainfall.cpp` transcribes its `logoPixels` for the screensaver, and
	/// that is the only thing here that draws it.
	///
	/// This call used to push a `BootScene` -- a "DELUGE" title over the firmware version -- which
	/// was removed because the port must not add UI the instrument did not have. The shell design
	/// spec's section 9 amendment has the full account.
	void boot();

	/// @brief Wire (or clear) the hooks the shell asks for the application's song root through,
	///        on `song_loading(false)` and on a bare `Dirty::SongSwapped` (see `apply_dirty`).
	/// @param hooks The hooks; a default-constructed `SongHooks` (`root_for_song == nullptr`)
	///              makes both paths a no-op -- see their own docs.
	void set_song_hooks(SongHooks hooks) { song_hooks_ = hooks; }

	/// @brief The shell's half of the song-load protocol (spec D §9): what the firmware calls from
	///        the load path (spec F wires it) to bracket a song read.
	///
	/// `loading=true`: reset the whole stack, push the shell's own `LoadingScene`, and take the
	/// input lock -- the port of `nullifyUIs()` (`gui/ui/ui.cpp:257-261`) plus the input side of
	/// `gui/ui/load/load_song_ui.*`'s "loading" presentation. `loading=false`: pop the loading
	/// scene, release the lock, and -- only if `set_song_hooks` was given a real
	/// `root_for_song` -- `replace_root()` to whatever it returns for the now-loaded song.
	///
	/// @note With no hooks set, `loading=false` pops `Loading` and stops there: the stack is left
	///       empty (`top()` is the `NullScene`, which draws nothing and swallows every event) --
	///       valid, if not useful, until either a later `init()`/`song_loading(true)` or hooks
	///       finally being set and a bare `Dirty::SongSwapped` arriving.
	/// @param loading True as loading starts, false once the song is ready.
	void song_loading(bool loading);

	/// @brief One pass of the whole UI, in spec D §7's order.
	/// @param now_ms The tick's wall clock.
	void tick(uint32_t now_ms);

	/// @brief Wire (or, with `nullptr`, unwire) the sysex/MIDI display mirror `flush` drives after
	///        every real blit of the `Main` surface.
	/// @param sink The mirror sink, or `nullptr` to stop mirroring.
	void set_mirror(MirrorSink sink) { mirror_.sink = sink; }

	/// @brief Start (or replace) the grid's tween.
	///
	/// Snapshots the live grid as \p tw's "from" frame, arms the grid gate for the next tick at
	/// this kind's step cadence, and hands the tween to `render_grid`, which blends from that
	/// frozen snapshot toward each tick's freshly rendered grid until `tw.duration_ms` has
	/// elapsed, at which point the tween clears and the live grid shows through unblended.
	/// A tween started before the surfaces are built (`grid_storage` unset) is refused outright --
	/// there is no grid to snapshot, and holding one pending would let it activate later against a
	/// snapshot from an unrelated moment.
	///
	/// \p tw.param's meaning depends on `tw.kind`:
	/// | `kind` | render, per tick | `param` |
	/// |---|---|---|
	/// | `Fade` | `gfx::grid::fade(from, live, t, live)` | unused |
	/// | `ScrollH` | `gfx::grid::scroll_horizontal(from, live, param * t / 65536, live)` | target offset, in main
	/// columns (`scroll_horizontal`'s own `offset`) | | `ScrollV` | `gfx::grid::scroll_vertical(from, live, param * t /
	/// 65536, live)` | target offset, in rows | | `GreyOut` | `ui::grid::greyout(live, greyout_mask, param * t /
	/// 65536)` | target `RGB::greyOut` proportion (the ~0..6500000 scale, e.g. spec D's `kGreyoutAmountFull`), reached
	/// at `t == 65535`; ramps **in** only -- there is no ramp-out kind |
	///
	/// where `t` is `(now_ms - tw.start_ms) * 65536 / tw.duration_ms`, clamped to 0..65535.
	/// `GreyOut` replaces (not stacks with) the tick's ordinary full-strength greyout pass while it
	/// runs, and applies only inside the stack's greyout mask, matching legacy's ramp
	/// (`pad_leds.cpp:983-1005`) feeding the same masked `prepareColour` (`:293-296`).
	/// @param tw The tween to play; `tw.start_ms` is the clock it is measured from.
	void start_tween(ActiveTween tw);

	/// @name Feeding input
	/// Called by the board poll loop between ticks; both open the lowering's tick window if it is
	/// not already open (see the class note).
	/// @{
	/// @brief Lower one event from `deluge_control_poll_event`.
	/// @param ev     The event.
	/// @param now_ms The wall clock at which it arrived.
	void on_board_event(const DelugeInputEvent& ev, uint32_t now_ms);
	/// @brief Lower the A-pin edges one encoder has accumulated.
	/// @param index  The board's encoder index.
	/// @param edges  Signed A-pin edge count since the last call.
	/// @param now_ms The wall clock.
	void on_encoder_edges(uint8_t index, int8_t edges, uint32_t now_ms);
	/// @}

	/// @brief Take or release the SD-routine lock, deferring board input while it is held.
	///
	/// Releasing it replays the deferred queue, so this opens the lowering's tick window like any
	/// other feed does -- see the tick-window note on this class.
	/// @param busy True to take the lock.
	void set_sd_busy(bool busy);

	/// @brief When the shell next has something to do with no input at all.
	/// @return The absolute deadline in milliseconds, or `std::nullopt` when nothing is armed --
	///         the scheduler may then sleep until the next input. Recomputed at the end of every
	///         `tick()` from the surface gates, the widgets' gate, every service's `next_timer()`,
	///         and the `next_timer()` of every stacked scene for which step 8b armed no gate at all
	///         (`timer_arms_a_gate`) -- a scene whose timer did reach a gate already has its
	///         deadline read from that gate. The three that reach the scene loop are the full
	///         opt-out, a scene covered on every surface it claims, and an OLED-only claim on a
	///         profile with no OLED surfaces.
	std::optional<uint32_t> next_wake() const { return next_wake_; }

	/// @name Inspection
	/// @{
	SceneStack& stack() { return stack_; }
	InputLowering& lowering() { return lowering_; }
	Notices& notices() { return notices_; }
	const SurfaceSet& surfaces() const { return surfaces_; }
	/// @brief The union of every stacked scene's `greyout_mask()`, as re-queried on the last tick
	///        the stack changed.
	grid::GreyoutMask greyout() const { return greyout_; }
	/// @brief How many LED declarations were dropped for naming a board LED past
	///        `LedFrame::kMaxEntries`.
	uint32_t leds_dropped() const { return leds_dropped_; }
	/// @brief How many times the idle deadline came and went with no `SceneKind::Screensaver`
	///        scene for the stack to push (`Screensaver::push_refused`).
	/// @return 0 normally; 1 once the screensaver service has latched itself off, which is what
	///         `screensaver_idle_ms > 0` does on an application that has not registered a
	///         screensaver scene.
	uint32_t screensaver_push_refused() const { return screensaver_.push_refused(); }
	/// @}

	/// @brief The shell's half of the UI test harness's `.state` snapshot (spec G §3).
	///
	/// `"scene <KindName>[/<qualifier>]"` per stacked scene, **top first**, then `"lock input"`
	/// while the input lock is held, then `"notice <Kind> <arg>"` while the popup scene is on the
	/// stack. The surfaces' half (pads, LEDs, indicators) comes from the `SurfaceSet` side of the
	/// harness.
	///
	/// A scene of an application kind, which the shell has no name for, is spelled `app<n>`,
	/// qualified by its `SceneId::arg` when that is non-zero (`app16/3`). `Popup` is qualified by
	/// its popup type's name (`Popup/General`); every other shell kind is spelled bare. The
	/// `notice` line names the `Notice::Kind` that raised the popup and the text it resolved to
	/// (`notice Text SAVED`; an error with no l10n bridge resolves to its digits, `notice Error
	/// 248`).
	/// @return The lines, each newline-terminated; empty when nothing is stacked or locked.
	std::string state_lines() const;

private:
	/// @brief `on_model_changed` on every stacked scene, `Dirty::SongSwapped`'s bare re-query, and
	///        the dirty facts onto the gates.
	///
	/// `Dirty::Oled` invalidates the OLED surfaces; `GridMain`/`GridSidebar`/`ClipContent`/
	/// `NoteRows` invalidate the grid; every other fact is a model change with no surface named,
	/// so it invalidates both. `Dirty::SongSwapped` additionally -- once every scene has had its
	/// `on_model_changed` -- calls `replace_root(hooks.root_for_song(ctx))` if `set_song_hooks`
	/// was given real hooks; this is the narrower of the song-swap protocol's two paths (see
	/// `song_loading`'s own doc for the bracketed one) and a no-op with none set.
	/// @param dirty This tick's drained facts.
	void apply_dirty(const notify::DirtySet& dirty);
	/// @brief Close the lowering's tick window and route everything ordinary out of it.
	/// @param now_ms The tick's wall clock.
	void pump_input(uint32_t now_ms);
	/// @brief Tick the scene stack top-down, stopping below the first scene that occludes the
	///        grid, then every service.
	/// @param now_ms The tick's wall clock.
	/// @param dt_ms  Milliseconds since the previous tick.
	void tick_stack(uint32_t now_ms, uint32_t dt_ms);
	/// @brief The lowest scene an OLED surface with role \p role actually draws.
	///
	/// Walk down from the top while each scene lets the one below show through
	/// (`Scene::oled_transparent`); the scene that stops the walk is the lowest one that has to be
	/// drawn. **Per role**, because transparency is declared per role.
	///
	/// Factored out of `render_oled` so step 8b's timer arming can ask the same question the render
	/// answers -- a scene below this index cannot paint this surface, so arming its gate is a
	/// repaint and a blit that nothing can see (E3 final review, A2).
	///
	/// @param role The surface's role.
	/// @return The index into `stack_.scenes()`, or 0 for an empty stack.
	size_t oled_lowest(SurfaceRole role) const;
	/// @brief Can the scene at \p index paint the pad grid at all?
	///
	/// Two conditions, and the second is easy to forget: the scene has to sit at or above
	/// `grid_base()`, *and* the profile's grid surface has to have storage -- `render_grid` returns
	/// before the stack walk without it, so on such a profile the answer is no for every scene.
	///
	/// @param index The scene's index into `stack_.scenes()`.
	/// @return True when a grid repaint armed for this scene could reach the panel.
	bool grid_paintable(size_t index);
	/// @brief Can the scene at \p index paint OLED surface \p s at all?
	///
	/// The `oled_lowest` test, plus the `Style` `render_oled` itself demands.
	///
	/// @param s     The surface.
	/// @param index The scene's index into `stack_.scenes()`.
	/// @return True when an OLED repaint armed for this scene could reach that panel.
	bool oled_paintable(const OledSurface& s, size_t index);
	/// @brief Would step 8b arm any gate for the scene at \p index?
	///
	/// The one-bool form of what step 8b does: a claimed surface only counts when this scene sits at
	/// or above that surface's base, and the OLED half only counts when the profile has an OLED
	/// surface at all. `compute_next_wake` asks it, because a scene that armed nothing has no gate
	/// deadline standing in for its timer.
	///
	/// @param index    The scene's index into `stack_.scenes()`.
	/// @param surfaces The scene's `timer_repaints()` claim.
	/// @return True when at least one gate would be armed.
	bool timer_arms_a_gate(size_t index, Scene::RepaintSurfaces surfaces);
	/// @brief The lowest scene the pad grid actually draws: the topmost non-overlay scene.
	///
	/// Everything above it is a grid overlay by construction. A stack of nothing but overlays has no
	/// base and starts at the root. Factored out of `render_grid` for the reason `oled_lowest` is.
	///
	/// @return The index into `stack_.scenes()`, or 0 for an empty stack.
	size_t grid_base() const;
	/// @brief Draw one OLED surface: the topmost opaque scene for its role, and everything above.
	/// @param s The surface.
	/// @return False when the surface could not be drawn (no `Style`), so the caller does not mark
	///         an untouched canvas for blitting.
	bool render_oled(OledSurface& s);
	/// @brief Draw the pad grid: the base scene, dimmed if an overlay above it `dims_grid()`, then
	///        the overlays, then the stack's greyout.
	/// @param now_ms The tick's wall clock.
	void render_grid(uint32_t now_ms);
	/// @brief Halve every pad's channels in place, the dim an overlay is composited over.
	/// @param grid The pad framebuffer.
	static void dim_grid(gfx::grid::PadGrid& grid);
	/// @brief Offer this tick's held-back, widget-owned events to the routing chain.
	/// @param now_ms The tick's wall clock.
	void deliver_post_render_input(uint32_t now_ms);
	/// @brief Compose and resolve this tick's LED declarations into the LED surface.
	/// @param now_ms The tick's wall clock.
	void derive_leds(uint32_t now_ms);
	/// @brief Push the rendered surfaces to the hardware.
	/// @param now_ms The tick's wall clock.
	void flush(uint32_t now_ms);

	/// @brief Open the lowering's tick window if it is not already open.
	/// @param now_ms The clock to open it at.
	void ensure_tick_window(uint32_t now_ms);
	/// @brief The world around one event, as the scenes and services see it.
	/// @param now_ms The tick's wall clock.
	UiContext make_context(uint32_t now_ms) const;
	/// @brief Note that \p scene rendered this tick, at most once per tick.
	/// @param scene The scene that was just drawn.
	void append_rendered(Scene* scene);
	/// @brief The earliest deadline any gate, scene or service is waiting on.
	/// @param now_ms The tick's wall clock, which the scenes' relative timers are measured from.
	std::optional<uint32_t> compute_next_wake(uint32_t now_ms);

	SurfaceSet& surfaces_;
	BoardProfile profile_;
	ShellConfig config_;
	Services services_;
	/// The shell's own scene table: the application's, plus the three overlay scenes below.
	std::array<Scene*, kMaxSceneKinds> table_{};
	SceneRegistry registry_;
	SceneStack stack_;
	InputLowering lowering_;
	Router router_;
	PopupScene popup_;
	ConsoleScene console_;
	WorkingScene working_;
	/// The shell's own boot splash and song-loading scenes, installed into `table_` alongside
	/// `popup_`/`console_`/`working_` above -- unlike `SceneKind::Screensaver`, which names no
	/// shell-owned scene at all (see `screensaver_`'s own doc).
	LoadingScene loading_;
	/// Pushes/pops `SceneKind::Screensaver` after idle. Deliberately does *not* own a scene
	/// instance the way `popup_`/`console_`/`working_`/`loading_` do: the real animation
	/// is spec E3's, registered by the application (or a spec's stand-in) like any other scene, so
	/// `table_[Screensaver]` is left exactly as `registry.find()` reported it.
	Screensaver screensaver_;
	Notices notices_;
	/// `mod_knobs`, `global_controls`, `midi_learn`, `Notices` and `Screensaver`, each listed
	/// once; built in the constructor, since `Services` is fixed there.
	FixedVector<Service*, 5> service_list_;
	/// The hooks `song_loading(false)` and a bare `Dirty::SongSwapped` ask for the application's
	/// song root through; unset (`root_for_song == nullptr`) until `set_song_hooks` is called.
	SongHooks song_hooks_;

	/// The union of the stack's `greyout_mask()`s, re-queried whenever the stack changes.
	grid::GreyoutMask greyout_{};
	/// The sysex/MIDI display mirror `flush` drives after a real blit of the `Main` surface.
	Mirror mirror_;
	/// The grid tween's "from" frame: a snapshot taken by `start_tween` of the grid as it stood at
	/// that moment. `std::nullopt` whenever `GridSurface::tween` is, and cleared together with it;
	/// held here rather than on `GridSurface` because it is a `Shell`-side concern (the surface
	/// pool has no business owning a whole extra framebuffer) and `PadGrid` has no default
	/// constructor for a plain array member to hold before a tween's first snapshot.
	std::optional<gfx::grid::PadGrid> tween_from_;
	/// Scratch storage for `ScrollH`/`ScrollV` tween steps: `gfx::grid::scroll_horizontal`/
	/// `scroll_vertical` read `incoming` at column/row indices other than the one they are
	/// writing, so blending straight into the live grid could read a cell this same pass already
	/// overwrote. Reused every step rather than a per-tick stack local (~1.5 KB for `kMaxGridRows`
	/// x `kMaxGridCols` `RGB`s -- `PadGrid`'s own doc), since `Shell` already lives for the whole
	/// tick loop and the render path should not pay for it on every call.
	std::optional<gfx::grid::PadGrid> tween_scratch_;
	/// The scenes drawn this tick, in the order they were drawn; each gets one `after_render()`.
	FixedVector<Scene*, kStackDepth> rendered_this_tick_;
	std::optional<uint32_t> next_wake_;
	uint32_t last_ms_ = 0;
	/// Whether the lowering accepted anything this tick -- `RepaintGate::needs_frame`'s
	/// `had_input`, and the place Task 4 parked "set where legacy sets `anything`"
	/// (`deluge.cpp:609-612`).
	bool had_input_ = false;
	/// Whether the lowering's tick window is open; see the class note on the tick window.
	bool tick_open_ = false;
	/// Whether `tick()` has ever run, so the first tick's `dt` is 0 rather than the boot clock.
	bool ticked_ = false;
	/// How many LED declarations named a board LED at or past `LedFrame::kMaxEntries` and could
	/// therefore neither be diffed nor sent. Should stay at zero; non-zero means the board profile
	/// has outgrown the shadow arrays.
	uint32_t leds_dropped_ = 0;
};

} // namespace deluge::ui::shell
