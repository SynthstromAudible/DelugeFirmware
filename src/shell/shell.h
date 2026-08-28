#pragma once
#include "notify/dirty_set.h"
#include "shell/fixed_vector.h"
#include "shell/input_lowering.h"
#include "shell/leds.h"
#include "shell/notices.h"
#include "shell/profile.h"
#include "shell/router.h"
#include "shell/scene.h"
#include "shell/scene_stack.h"
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
	/// How long the shell may sit with no input before the screensaver scene is pushed; 0 is off.
	/// @note Stored but unread until Task 9 builds `Screensaver`.
	uint32_t screensaver_idle_ms = 0;
	/// The seed the screensaver's animation is started from.
	/// @note Stored but unread until Task 9.
	uint32_t seed = 0;
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

	/// @brief Push the first scene. Task 9 makes this `SceneKind::Boot`.
	/// @param initial What to push.
	void init(SceneId initial);

	/// @brief One pass of the whole UI, in spec D §7's order.
	/// @param now_ms The tick's wall clock.
	void tick(uint32_t now_ms);

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
	/// @param busy True to take the lock.
	void set_sd_busy(bool busy);

	/// @brief When the shell next has something to do with no input at all.
	/// @return The absolute deadline in milliseconds, or `std::nullopt` when nothing is armed --
	///         the scheduler may then sleep until the next input. Recomputed at the end of every
	///         `tick()` from the surface gates, the widgets' gate, and every stacked scene's and
	///         service's `next_timer()`.
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
	/// @}

	/// @brief The shell's half of the UI test harness's `.state` snapshot (spec G §3).
	///
	/// `"scene <name> <arg>"` per stacked scene, **top first**, then `"lock 1"` while the input
	/// lock is held, then `"notice <text>"` while the popup scene is on the stack. The surfaces'
	/// half (pads, LEDs, indicators) comes from the `SurfaceSet` side of the harness. A scene of
	/// an application kind, which the shell has no name for, is spelled `app<n>`.
	/// @return The lines, each newline-terminated; empty when nothing is stacked or locked.
	std::string state_lines() const;

private:
	/// @brief `on_model_changed` on every stacked scene, and the dirty facts onto the gates.
	///
	/// `Dirty::Oled` invalidates the OLED surfaces; `GridMain`/`GridSidebar`/`ClipContent`/
	/// `NoteRows` invalidate the grid; every other fact is a model change with no surface named,
	/// so it invalidates both.
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
	/// @brief Draw one OLED surface: the topmost opaque scene for its role, and everything above.
	/// @param s The surface.
	/// @return False when the surface could not be drawn (no `Style`), so the caller does not mark
	///         an untouched canvas for blitting.
	bool render_oled(OledSurface& s);
	/// @brief Draw the pad grid: the base scene, dimmed if anything overlays it, then the
	///        overlays, then the stack's greyout.
	/// @param now_ms The tick's wall clock.
	void render_grid(uint32_t now_ms);
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
	Notices notices_;
	/// `mod_knobs`, `global_controls`, `midi_learn` and `Notices`, each listed once; built in the
	/// constructor, since `Services` is fixed there.
	FixedVector<Service*, 4> service_list_;

	/// The union of the stack's `greyout_mask()`s, re-queried whenever the stack changes.
	grid::GreyoutMask greyout_{};
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
