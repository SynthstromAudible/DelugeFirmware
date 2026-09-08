#include "shell/shell.h"

#include "gfx/grid/tween.h"
#include "notify/notice_queue.h"
#include "ui/grid/frame.h"
#include "ui/oled/frame.h"

#include <algorithm>
#include <iterator>
#include <string_view>

namespace deluge::ui::shell {

namespace {

using notify::Dirty;

/// @brief Legacy's full greyout strength, as `ui::grid::greyout`'s `amount` argument.
///
/// The toolkit's `amount` *is* legacy's `greyProportion` (`ui/grid/effects.h`: "Legacy's
/// `greyProportion`, the argument `RGB::greyOut` takes"), and `greyProportion` is not the 0..1
/// float the callers pass -- `setGreyoutAmount(float)` scales it: `greyProportion = newAmount *
/// 6500000` (`hid/led/pad_leds.cpp:786-788`). Every place that greys a whole view outright asks
/// for the full amount, `setGreyoutAmount(1)` (`pad_leds.cpp:753`, `:767`, `:783`, `:988`), so the
/// scaled constant is `1 * 6500000`. (The fractional values at `pad_leds.cpp:991,1003` are a
/// transition ramping the same scale in and out, which the tween driver -- Task 8 -- owns.)
/// `RGB::greyOut` then reads it as `0x808080 - proportion` plus `totalRGB * (proportion >> 5)`
/// over a 23-bit shift (`gfx/grid/rgb.h`), which is why the number is this size rather than 0..255.
constexpr int32_t kGreyoutAmountFull = 6500000;

/// @brief Does \p f name a surface the shell can invalidate on its own?
///
/// `Oled` names the OLED surfaces; the four grid facts name the grid. Deliberately a *denylist*:
/// a fact added to `notify::Dirty` later is untargeted until someone says otherwise, which
/// over-invalidates. An allowlist of the untargeted facts would instead leave a new fact
/// repainting nothing at all -- the one failure mode a dirty flag must not have.
bool names_a_surface(Dirty f) {
	return f == Dirty::Oled || f == Dirty::GridMain || f == Dirty::GridSidebar || f == Dirty::ClipContent
	       || f == Dirty::NoteRows;
}

/// @brief Does \p dirty carry a fact that names no particular surface?
///
/// Such a fact is a model change whose consequences the shell cannot attribute, so it invalidates
/// both sides.
bool has_untargeted_fact(const notify::DirtySet& dirty) {
	for (uint8_t i = 0; i < static_cast<uint8_t>(Dirty::Count); i++) {
		const Dirty f = static_cast<Dirty>(i);
		if (!names_a_surface(f) && dirty.has(f)) {
			return true;
		}
	}
	return false;
}

/// @brief Assemble column pair \p pair_index's 16 pad colours from \p grid.
///
/// Legacy order (`PadLEDs::sortLedsForCol`, `hid/led/pad_leds.cpp:260-275`): column
/// `pair_index * 2` rows 0..7, then column `pair_index * 2 + 1` rows 0..7 -- `doubleColumn`'s two
/// back-to-back fills, each over `kDisplayHeight` (8) rows.
ColumnPair read_column_pair(const gfx::grid::PadGrid& grid, uint8_t pair_index) {
	ColumnPair out{};
	const uint8_t rows = std::min<uint8_t>(grid.dims().rows, kColumnPairLeds / 2);
	const uint8_t x0 = static_cast<uint8_t>(pair_index * 2);
	const uint8_t x1 = static_cast<uint8_t>(x0 + 1);
	uint8_t idx = 0;
	for (uint8_t y = 0; y < rows; y++) {
		out.colours[idx++] = grid.row(y)[x0];
	}
	for (uint8_t y = 0; y < rows; y++) {
		out.colours[idx++] = grid.row(y)[x1];
	}
	return out;
}

/// @brief Elementwise equality for `KnobIndicator`, which (unlike `ColumnPair`) declares no
///        `operator==` of its own. Iterates the real extent of `levels` rather than a hardcoded
///        4, so this does not silently go stale if that array's size ever changes.
bool same_indicator(const KnobIndicator& a, const KnobIndicator& b) {
	for (size_t i = 0; i < std::size(a.levels); i++) {
		if (a.levels[i] != b.levels[i]) {
			return false;
		}
	}
	return true;
}

/// @brief How many knob indicators the board has: `IndicatorId::Lower` and `::Upper`.
///
/// `IndicatorId` (`ui/core/led_frame.h`) declares no `Count`, and `LedFrame::indicator_in_range`
/// hardcodes this same "2" privately -- this is the shell's own name for that same fact, not a
/// duplicate source of truth to keep in sync with a differently-spelled one.
constexpr uint8_t kIndicatorCount = 2;

/// @brief Legacy's "come back in a little while" retry interval for backpressured pad sends.
///
/// `setTimerForSoon` (`hid/led/pad_leds.cpp:1063-1067`) re-arms `TimerName::MATRIX_DRIVER` at
/// 20 ms when `deluge_control_pad_output_space()` was too small to send (the `sendOutMainPadColours`
/// / `timerRoutine` backpressure paths, `:1019-1023`, `:850`).
constexpr uint32_t kBackpressureRetryMs = 20;

/// @brief The tween's blend parameter, 0..65535, from elapsed time over its duration.
///
/// `elapsed * 65536 / duration` rather than `* 65535`: at `elapsed == duration / 2` this lands on
/// an exact half (e.g. 100/200 -> 32768) instead of landing one short of it.
/// @pre `duration_ms > 0` and `elapsed_ms < duration_ms` -- the caller never asks once a tween has
///      run its full duration (it clears instead), so the unreachable `t == 65536` overflow of a
///      true 0..65535 scale, and the zero-duration divide, never arise.
uint16_t tween_progress(uint32_t elapsed_ms, uint32_t duration_ms) {
	const uint64_t t = (static_cast<uint64_t>(elapsed_ms) * 65536u) / duration_ms;
	return static_cast<uint16_t>(std::min<uint64_t>(t, 65535u));
}

/// @brief How often a running tween should ask for its next step, per `ActiveTween::Kind`.
///
/// Only `Fade` and the greyout ramp have a legacy per-kind timer to cite; `ScrollH`'s legacy
/// counterpart has its own faster one, and `ScrollV`'s has none at all (its caller drives it
/// directly rather than the animation re-arming itself) -- ported values are cited per kind below.
/// This cadence only governs `Shell::next_wake()`'s reported deadline: the driver itself
/// re-evaluates every tick regardless, so a caller ticking faster than this still animates
/// correctly. Used both by `Shell::start_tween` (the initial deadline) and `render_grid` (each
/// mid-tween step's re-arm).
/// @param kind Which tween is running.
/// @return Milliseconds until the next step should be considered due.
uint32_t tween_cadence_ms(ActiveTween::Kind kind) {
	switch (kind) {
	case ActiveTween::Kind::Fade:
		// `renderFade` re-arms `TimerName::MATRIX_DRIVER` at `UI_MS_PER_REFRESH`
		// (`hid/led/pad_leds.cpp:1479`; 50 ms, `board_config.h:55`).
		return 50;
	case ActiveTween::Kind::ScrollH:
		// `horizontal::renderScroll` re-arms at `UI_MS_PER_REFRESH_SCROLLING`
		// (`hid/led/pad_leds.cpp:1409`; 7 ms, `board_config.h:59`).
		return 7;
	case ActiveTween::Kind::ScrollV:
		// `vertical::renderScroll` (`hid/led/pad_leds.cpp:1431-1456`) re-arms no timer of its own
		// at all -- its caller drives each step directly. `UI_MS_PER_REFRESH` (50 ms) is this
		// port's own choice for "how often to ask", not a legacy value; there is none to cite.
		return 50;
	case ActiveTween::Kind::GreyOut:
		// The greyout ramp re-arms at `UI_MS_PER_REFRESH` too (`pad_leds.cpp:991,1003`; 50 ms).
		return 50;
	}
	return 50;
}

} // namespace

const char* scene_kind_name(SceneKind k) {
	switch (k) {
	case SceneKind::Null:
		return "Null";
	case SceneKind::Popup:
		return "Popup";
	case SceneKind::Console:
		return "Console";
	case SceneKind::Working:
		return "Working";
	case SceneKind::Loading:
		return "Loading";
	case SceneKind::Screensaver:
		return "Screensaver";
	default:
		return nullptr;
	}
}

Shell::Shell(SurfaceSet& surfaces, const BoardProfile& profile, const SceneRegistry& registry, Services services,
             ShellConfig config)
    : surfaces_(surfaces), profile_(profile), config_(config), services_(services),
      registry_{std::span<Scene* const>{table_}}, stack_(registry_), lowering_(profile, config.lowering),
      router_(stack_, services, lowering_), screensaver_(stack_, config.screensaver_idle_ms, config.seed),
      notices_(stack_, profile_, popup_, console_, working_) {
	for (size_t i = 0; i < table_.size(); i++) {
		table_[i] = registry.find(static_cast<SceneKind>(i));
	}
	// The shell owns these kinds outright: `Notices` drives the first three instances directly and
	// `song_loading()` drives `Loading`, so a scene an application registered in one of those slots
	// could never be told anything. `SceneKind::Screensaver` is deliberately
	// absent from this list -- see `screensaver_`'s own doc.
	table_[static_cast<size_t>(SceneKind::Popup)] = &popup_;
	table_[static_cast<size_t>(SceneKind::Console)] = &console_;
	table_[static_cast<size_t>(SceneKind::Working)] = &working_;
	table_[static_cast<size_t>(SceneKind::Loading)] = &loading_;

	rebuild_service_list();
}

void Shell::set_services(Services services) {
	// Three things hold the slots and all three must move together: this shell's own copy, the
	// `Router`'s by-value copy (which is what routing actually reads -- `Router::route` consults
	// `services_.mod_knobs` etc., not `service_list_`), and the deduplicated per-tick list. A
	// change that updated two of the three would leave a shell that either ticks services it never
	// routes to or routes to services it never ticks, and each half passes the other's tests.
	services_ = services;
	router_.set_services(services);
	rebuild_service_list();
}

void Shell::rebuild_service_list() {
	// The slots change only when an application installs them, so the deduplicated list is built
	// here rather than three times a tick. The three `Services` slots default to the one shared
	// `null_service()`, and nothing stops an application putting one object in two of them; a
	// service must be ticked, asked for LEDs and asked for its timer exactly once.
	//
	// The clear is what makes a *re*-install correct: rebuilt from scratch, so a service installed
	// by an earlier call is not left behind, and installing the same set twice cannot double an
	// entry (which would double-tick it and ask it for LEDs twice in one frame).
	service_list_.clear();
	Service* const all[] = {services_.mod_knobs, services_.global_controls, services_.midi_learn, &notices_,
	                        &screensaver_};
	for (Service* service : all) {
		if (service == nullptr) {
			continue;
		}
		bool seen = false;
		for (Service* already : service_list_) {
			seen = seen || already == service;
		}
		if (!seen) {
			(void)service_list_.push_back(service);
		}
	}
}

void Shell::init(SceneId initial) {
	stack_.push(initial);
}

void Shell::boot() {
	// No `init()` call: nothing goes on the stack here, so the panels stay blank until the
	// application pushes its own root. See the header for the splash this call used to put up and
	// why it was removed.
	for (OledSurface& s : surfaces_.oled()) {
		s.gate.request_repaint();
	}
	surfaces_.grid().gate.request_repaint();
	lowering_.widgets_gate().request_repaint();
}

void Shell::song_loading(bool loading) {
	if (loading) {
		stack_.reset();
		(void)stack_.push(SceneId{SceneKind::Loading, 0});
		lowering_.set_input_lock(true);
		return;
	}
	stack_.pop(SceneKind::Loading);
	lowering_.set_input_lock(false);
	if (song_hooks_.root_for_song != nullptr) {
		(void)stack_.replace_root(song_hooks_.root_for_song(song_hooks_.ctx));
	}
	// No hooks: the stack is left exactly as `pop` leaves it -- empty, `top()` the `NullScene` --
	// per this method's own doc.
}

// -- Feeding ----------------------------------------------------------------------------------

void Shell::ensure_tick_window(uint32_t now_ms) {
	if (!tick_open_) {
		lowering_.begin_tick(now_ms);
		tick_open_ = true;
	}
}

void Shell::on_board_event(const DelugeInputEvent& ev, uint32_t now_ms) {
	ensure_tick_window(now_ms);
	// Before lowering, not instead of it: see `Screensaver::note_activity`'s doc on why the event
	// that wakes the screensaver must still reach the scene beneath it.
	screensaver_.note_activity(now_ms);
	lowering_.on_board_event(ev, now_ms);
}

void Shell::on_encoder_edges(uint8_t index, int8_t edges, uint32_t now_ms) {
	ensure_tick_window(now_ms);
	screensaver_.note_activity(now_ms);
	lowering_.on_encoder_edges(index, edges, now_ms);
}

bool Shell::route_midi(const UiEvent& event, uint32_t now_ms) {
	// No `ensure_tick_window` and no `note_activity`, and both omissions are decisions.
	//
	// The window: this event is not lowered and never enters `InputLowering`'s queue, so there is
	// nothing for `begin_tick`/`end_tick` to assemble on its behalf. Opening one here would reset
	// the tick-scoped detent sums and press edges of a window the board feeds have already opened
	// -- `ensure_tick_window` is idempotent per tick, but the first MIDI message of a tick would be
	// the one that opened it, and a board event arriving afterwards would then share it. Harmless
	// today and pointless always: nothing this routes reads tick-scoped state.
	//
	// The screensaver: legacy does not wake it for MIDI either. `Screensaver::noteActivity()`
	// (`hid/display/screensaver.cpp:85`) has exactly three callers -- the two board-event arms of
	// `deluge_control_poll_event`'s dispatch (`deluge.cpp:240`, `:254`) and
	// `Encoders::interpretEncoders` (`hid/encoder_input.cpp:208`) -- and none of `PlaybackHandler`'s
	// MIDI dispatchers is one. A MIDI message is not a user touching the box.
	return router_.route(event, make_context(now_ms));
}

void Shell::set_sd_busy(bool busy) {
	// Releasing the lock replays the whole deferred queue through the ordinary lowering path
	// (`InputLowering::set_sd_busy`), which writes the tick-scoped state -- the detent sums, the pad
	// events, the held-back queue and `had_input_this_tick`. Those are exactly what `begin_tick`
	// clears, so a replay into a *closed* window would be thrown away by the next `pump_input`
	// before anything could route it: every deferred edge lost, which is the one thing the deferral
	// queue exists to prevent. Opening the window first makes the replay a feed like any other.
	//
	// `last_ms_` is the clock to open at: `begin_tick`'s stamp is provisional (`end_tick(now_ms)`
	// overwrites it with the tick's own), and each replayed event carries the clock it originally
	// arrived at, so nothing downstream reads this value.
	ensure_tick_window(last_ms_);
	lowering_.set_sd_busy(busy);
}

// -- The tick ---------------------------------------------------------------------------------

void Shell::tick(uint32_t now_ms) {
	// A shell first ticked at a non-zero clock has no previous tick to measure from; handing every
	// scene `now_ms` as its first `dt` would fast-forward every animation by the boot time.
	if (!ticked_) {
		last_ms_ = now_ms;
		ticked_ = true;
	}
	const uint32_t dt = now_ms - last_ms_;
	// 1. Model facts -> the gates and every stacked scene.
	const notify::DirtySet dirty = notify::dirty().drain();
	apply_dirty(dirty);
	// 2. Presentation requests -> the overlay scenes.
	notices_.drain(notify::notices(), now_ms);
	// 3. Lower, route and apply. The six widget-owned shapes stay in `lowering_.held_back()`.
	pump_input(now_ms);
	// A `Action::Kind::Popup` applied anywhere above leaves its string id on the router; the
	// router has no l10n and `Notices` takes text, so the shell is the one place both are in
	// scope. Read and cleared as a pair, per `Router::clear_pending_popup`.
	if (router_.pending_popup_string() != 0) {
		// `scratch` outlives `text`, which points into it when the profile has no `text` hook and
		// the id is spelled as digits instead (`BoardProfile::text_or_digits`); `show_popup` copies
		// before either goes out of scope.
		TextScratch scratch;
		const std::string_view text = profile_.text_or_digits(router_.pending_popup_string(), scratch);
		notices_.show_popup(text, router_.pending_popup_type(), now_ms);
		router_.clear_pending_popup();
	}
	// 4. Time.
	tick_stack(now_ms, dt);
	// A stack that moved may have changed what is greyed and what is drawn over what, and
	// `changed_since_last_query` is deliberately conservative, so re-query and invalidate together.
	if (stack_.changed_since_last_query()) {
		greyout_ = grid::GreyoutMask{};
		for (Scene* scene : stack_.scenes()) {
			const grid::GreyoutMask m = scene->greyout_mask();
			greyout_.cols |= m.cols;
			greyout_.rows |= m.rows;
		}
		for (OledSurface& s : surfaces_.oled()) {
			s.gate.request_repaint();
		}
		surfaces_.grid().gate.request_repaint();
		lowering_.widgets_gate().request_repaint();
	}
	rendered_this_tick_.clear();
	// `model_dirty` is deliberately `false` at all three `needs_frame` sites below. The gate's
	// third argument fires *every* gate, which would make step 1's careful attribution dead
	// weight: `apply_dirty` has already called `request_repaint()` on exactly the gates this
	// tick's facts name, and on every gate for a fact that names none. Passing `dirty.any()` as
	// well -- spec D §7's shorthand, written before the shell had that finer channel -- would
	// repaint the OLED for a note-row change and the grid for an OLED one.
	//
	// The widgets' gate is shell-wide and `needs_frame` *consumes* the request, so it is evaluated
	// exactly once and the answer OR'd into every surface's own decision -- asking it per surface
	// would give the request to whichever surface happened to be checked first.
	const bool widgets_want_frame = lowering_.ctx().gate.needs_frame(now_ms, had_input_, false);
	// 5. OLED.
	for (OledSurface& s : surfaces_.oled()) {
		if (s.gate.needs_frame(now_ms, had_input_, false) || widgets_want_frame || router_.repaint_requested(s.role)) {
			// Only a surface that actually drew has anything to push: `render_oled` refuses a
			// surface with no style, and blitting its untouched canvas would push a blank frame.
			if (render_oled(s)) {
				s.blit_pending = true;
			}
		}
	}
	// 6. Grid.
	GridSurface& g = surfaces_.grid();
	// `router_.grid_repaint_requested()` is the grid's half of `Action::Kind::RepaintAll`, and it
	// belongs here for the same reason `repaint_requested(s.role)` belongs one step up: a scene that
	// changed what the pads show in response to *post-render* input has no other way to say so. Its
	// own gate is not one -- `render_grid` is what arms that, and the whole problem is that the
	// render already happened this tick. E3 task 11's `marker.txt` is where the hole showed: a
	// select turn moved the marker on the OLED and left the pads a whole blink phase behind.
	if (g.gate.needs_frame(now_ms, had_input_, false) || widgets_want_frame || router_.grid_repaint_requested()) {
		render_grid(now_ms);
		g.dirty = true;
	}
	// Every render decision that could read a router repaint request has now been made, so the
	// requests those decisions consumed are cleared *here* rather than at the bottom of the tick.
	// Steps 7 and 8 below both run `router_.apply()`, and a `Repaint`/`RepaintAll` either of them
	// produces must survive to be acted on -- clearing after them set and wiped it inside one tick
	// with no reader in between. See the "Repaint requests" invariant on `Shell` (`shell.h`).
	router_.clear_repaint_requests();
	// 7. Navigation derived from what this tick's render observed.
	//
	// Applied as we go rather than collected first: spec D §7 step 7 says every rendered scene gets
	// exactly one `after_render()` call, so an early scene whose `Result` pops the stack still
	// leaves the later calls to run -- deliberately, on scenes that may no longer be stacked. A
	// scene's `after_render` is about the frame it just drew, which happened either way.
	for (Scene* scene : rendered_this_tick_) {
		router_.apply(scene->after_render());
	}
	// 8. The held-back events, now that the claims they are gated on are knowable.
	deliver_post_render_input(now_ms);
	// A repaint requested by step 7 or step 8 is for the *next* tick -- this tick's frames are
	// already built. Arming the surface's own gate at a zero-millisecond deadline is what makes
	// that next tick happen at all: `compute_next_wake` reads gate deadlines, so `next_wake()`
	// reports `now_ms` and a scheduler that would otherwise sleep until the next input wakes
	// immediately instead. (A bare `request_repaint()` would fire the gate but arm no deadline, so
	// the frame would wait for an unrelated wake.) The router's own flags are deliberately left
	// standing too: the next tick's step-5 loop reads them before clearing them above.
	for (OledSurface& s : surfaces_.oled()) {
		if (router_.repaint_requested(s.role)) {
			s.gate.request_repaint_after(0, now_ms);
		}
	}
	// And the grid, for the identical reason. Not folded into the loop above because the grid has
	// no `SurfaceRole` to look up (`shell/profile.h:60`) -- `RepaintAll` is the only action that
	// reaches it, through `grid_repaint_requested()`. Without this line the flag is read by the
	// next tick's step 6 but nothing makes that tick happen any sooner: today `RepaintAll` always
	// sets the OLED flags as well, so the loop above arms a wake and the grid comes along with it,
	// which is exactly the kind of accident a profile with no OLED surfaces would end (fix round 1,
	// minor).
	if (router_.grid_repaint_requested()) {
		surfaces_.grid().gate.request_repaint_after(0, now_ms);
	}
	// 8b. A scene timer is a repaint request, not merely a wake.
	//
	// `Scene::next_timer` used to feed `compute_next_wake` and nothing else, so a scene that wanted
	// its blink to move had to arm the gate itself from inside `render_*` as well -- and four scenes
	// in a row shipped or nearly shipped without doing so (the history is on `Scene::next_timer`).
	// Folding the two together here makes the *default* correct: overriding `next_timer()` is now
	// enough, and `Scene::timer_repaints()` is the explicit, per-scene declaration of which surfaces
	// (if any) that deadline actually has a visible consequence on.
	//
	// Same shape as the router arming just above -- `request_repaint_after`, not `request_repaint`:
	// the deadline is what `compute_next_wake` reads, so this both fires the gate when the moment
	// comes and tells a sleeping scheduler when that is.
	//
	// **Per surface KIND, not a guess at which surface this tick happened to paint.** A `Scene`
	// declares no per-tick surface set: `render_oled` is called for it on every OLED surface in the
	// pass and `render_grid` on the grid, so narrowing the arm to whatever the scene painted *this*
	// tick would be exactly wrong -- the tick a timer exists to rescue is the quiet one where the
	// scene painted nothing, and a scene that armed nothing could never paint again.
	// `timer_repaints()` is a static claim instead ("my timer matters to the grid", "to the OLED
	// panels", or both), not a per-tick one.
	//
	// **Why the OLED half is not free to over-arm the way the grid half is** (E3 task 10 fix round
	// 1): the grid's own `Shell::flush` diffs against `last_sent` and blits nothing when a repaint
	// changed no pixel, so arming it needlessly costs a repaint, not a blit. The OLED panels have no
	// such diff -- `flush` blits an OLED surface unconditionally once it has rendered -- so arming
	// them from a timer that never touches OLED content (`MenuScene`'s shortcut/source pad blink
	// cadence, purely a grid concern) turned into both panels blitting unconditionally at that
	// cadence, forever, with no pixel ever changing. `Scene::timer_repaints()`'s doc has the full
	// account; `MenuScene::timer_repaints()` is the fix.
	//
	// **And only from where the scene can actually paint** (E3 final review, A2). A claim is a
	// claim about surfaces, not about visibility, and the render functions draw from a base up:
	// `render_grid` from `grid_base()`, `render_oled` from `oled_lowest(role)`, per role. A scene
	// below that base is covered, so arming its gate buys a repaint it can never appear in --
	// which on the OLED is a blit, since that flush has no diff. Two live cases, both regressions
	// the fold itself introduced: a covered `MenuScene` with source pads on its focused node
	// (exactly the node that opens Browser/Marker/Recorder) kept the grid gate at 20 ms for the
	// whole life of the scene above it, where pre-fold that arm lived inside `MenuScene::render_grid`
	// and simply stopped being called; and `RecorderScene`, whose own opaque clipping `PopupScene`
	// covers it on the OLED, armed every OLED gate every 10 ms for the popup's lifetime -- a 100 Hz
	// two-panel blit with no pixel changing. The per-surface mask cannot see either one: the surface
	// is the right surface, the scene is just not the drawn one.
	//
	// `Shell::timer_arms_a_gate` is the same question asked as one bool, and `compute_next_wake`
	// asks it -- a scene that armed nothing here has no gate deadline standing in for its timer.
	const std::span<Scene* const> timer_scenes = stack_.scenes();
	for (size_t k = 0; k < timer_scenes.size(); k++) {
		Scene* const scene = timer_scenes[k];
		const Scene::RepaintSurfaces surfaces = scene->timer_repaints();
		if (!surfaces.any()) {
			continue;
		}
		const std::optional<uint32_t> in = scene->next_timer();
		if (!in.has_value()) {
			continue;
		}
		if (surfaces.oled) {
			for (OledSurface& s : surfaces_.oled()) {
				if (oled_paintable(s, k)) {
					s.gate.request_repaint_after(*in, now_ms);
				}
			}
		}
		if (surfaces.grid && grid_paintable(k)) {
			surfaces_.grid().gate.request_repaint_after(*in, now_ms);
		}
	}
	// 8c. And the same fold for the SERVICES, added by E4 task 5.
	//
	// A service draws nothing, so what its timer arms is the repaint of whichever scene reads the
	// state that timer advances. `MidiLearn`'s flash is the case that needed it: legacy's
	// `View::midiLearnFlash` toggles `midiLearnFlashOn` and then asks the ROOT UI to repaint
	// (`gui/views/view.cpp:814-819`, `getRootUI()->midiLearnFlash()`), and every reader of that bit
	// in the tree is a pad renderer. Without this loop the phase advanced on the wake
	// `compute_next_wake` already granted and nothing ever repainted with it.
	//
	// No `oled_paintable`/`grid_paintable` narrowing: those answer "is this scene at or above the
	// surface's base", and a service has no stack index. See `Service::timer_repaints`, whose
	// default is `{false, false}` precisely so this loop changes nothing for the services that
	// existed before it.
	for (Service* service : service_list_) {
		const Scene::RepaintSurfaces surfaces = service->timer_repaints();
		if (!surfaces.any()) {
			continue;
		}
		const std::optional<uint32_t> in = service->next_timer();
		if (!in.has_value()) {
			continue;
		}
		if (surfaces.oled) {
			for (OledSurface& s : surfaces_.oled()) {
				s.gate.request_repaint_after(*in, now_ms);
			}
		}
		if (surfaces.grid) {
			surfaces_.grid().gate.request_repaint_after(*in, now_ms);
		}
	}
	// 9. LEDs, every tick.
	derive_leds(now_ms);
	flush(now_ms); // Task 8
	next_wake_ = compute_next_wake(now_ms);
	last_ms_ = now_ms;
	// The lowering's tick window closes here and not before: `deliver_post_render_input` above
	// reads `held_back()` and `ctx().claims`, both of which the *next* `begin_tick()` clears.
	tick_open_ = false;
}

void Shell::apply_dirty(const notify::DirtySet& dirty) {
	if (!dirty.any()) {
		return;
	}
	for (Scene* scene : stack_.scenes()) {
		scene->on_model_changed(dirty);
	}
	// A bare `SongSwapped` (no preceding `song_loading(true)`/`(false)` bracket) only re-queries
	// the root -- spec D §9's second, narrower protocol. Run after the `on_model_changed` loop
	// above, never inside it: `replace_root` tears down and rebuilds the stack `scenes()` is a
	// view over, and `SceneStack` is explicitly not reentrant from within a lifecycle hook. With
	// no hooks set this is a no-op, per this method's own doc.
	if (dirty.has(Dirty::SongSwapped) && song_hooks_.root_for_song != nullptr) {
		(void)stack_.replace_root(song_hooks_.root_for_song(song_hooks_.ctx));
	}
	const bool untargeted = has_untargeted_fact(dirty);
	if (dirty.has(Dirty::Oled) || untargeted) {
		for (OledSurface& s : surfaces_.oled()) {
			s.gate.request_repaint();
		}
	}
	if (dirty.has(Dirty::GridMain) || dirty.has(Dirty::GridSidebar) || dirty.has(Dirty::ClipContent)
	    || dirty.has(Dirty::NoteRows) || untargeted) {
		surfaces_.grid().gate.request_repaint();
	}
}

UiContext Shell::make_context(uint32_t now_ms) const {
	return UiContext{
	    .held = lowering_.held(),
	    .held_pads = lowering_.held_pads(),
	    .shift = lowering_.shift(),
	    .sticky = lowering_.sticky(),
	    .now_ms = now_ms,
	    .profile = profile_,
	    .lowering = &lowering_,
	};
}

void Shell::pump_input(uint32_t now_ms) {
	// A tick with no input at all still has to open and close a window, so that `begin_tick`'s
	// per-tick reset runs and `end_tick` assembles a snapshot for this tick's frames to read.
	ensure_tick_window(now_ms);
	lowering_.end_tick(now_ms);
	UiEvent event{UiEvent::Kind::AllReleased};
	while (lowering_.next(event)) {
		router_.route(event, make_context(now_ms));
	}
	had_input_ = lowering_.had_input_this_tick();
}

void Shell::tick_stack(uint32_t now_ms, uint32_t dt_ms) {
	const std::span<Scene* const> scenes = stack_.scenes();
	for (size_t k = scenes.size(); k-- > 0;) {
		scenes[k]->tick(dt_ms);
		// A scene that covers the grid completely is the only thing anyone can see, so nothing
		// beneath it needs its clock advanced -- legacy's `PadLEDs::renderingLock` era kept every
		// UI running underneath and paid for it in wasted work.
		if (scenes[k]->occludes_grid()) {
			break;
		}
	}
	for (Service* service : service_list_) {
		service->tick(now_ms, dt_ms);
	}
}

// -- Rendering --------------------------------------------------------------------------------

void Shell::append_rendered(Scene* scene) {
	for (Scene* already : rendered_this_tick_) {
		if (already == scene) {
			return;
		}
	}
	(void)rendered_this_tick_.push_back(scene);
}

size_t Shell::oled_lowest(SurfaceRole role) const {
	const std::span<Scene* const> scenes = stack_.scenes();
	if (scenes.empty()) {
		return 0;
	}
	// The port of `OLED::sendMainImage`'s if-chain (`hid/display/oled.cpp:364-376`), which asked the
	// same question with two hard-coded layers (console over main, popup over that) and a
	// `copyBackgroundAroundForeground` blit per layer; here it is a property each scene declares.
	size_t lowest = scenes.size() - 1;
	while (lowest > 0 && scenes[lowest]->oled_transparent(role)) {
		lowest--;
	}
	return lowest;
}

bool Shell::grid_paintable(size_t index) {
	// `render_grid` gives up before it walks the stack at all on a profile whose grid surface has
	// no storage, so on such a profile *no* scene paints the grid, at any index. That is the same
	// shape as an OLED claim on a profile with no OLED panels, which `oled_paintable`'s callers get
	// for free by having nothing to iterate -- but the grid is a single surface reached directly,
	// so it needs saying (E3 final review residual 2: without this the predicate returned true for
	// a gate no render could ever service, and its name was false there).
	return surfaces_.grid().grid_storage.has_value() && index >= grid_base();
}

bool Shell::oled_paintable(const OledSurface& s, size_t index) {
	// `render_oled` refuses a surface with no `Style` before it draws anything ("such a surface must
	// not be rendered"), so it is not a surface any scene can paint either.
	return s.style != nullptr && index >= oled_lowest(s.role);
}

bool Shell::timer_arms_a_gate(size_t index, Scene::RepaintSurfaces surfaces) {
	if (surfaces.grid && grid_paintable(index)) {
		return true;
	}
	if (surfaces.oled) {
		for (OledSurface& s : surfaces_.oled()) {
			if (oled_paintable(s, index)) {
				return true;
			}
		}
	}
	return false;
}

size_t Shell::grid_base() const {
	const std::span<Scene* const> scenes = stack_.scenes();
	for (size_t k = scenes.size(); k-- > 0;) {
		if (!scenes[k]->is_grid_overlay()) {
			return k;
		}
	}
	return 0;
}

bool Shell::render_oled(OledSurface& s) {
	// A surface with no style cannot be drawn (`surfaces.h`: "such a surface must not be
	// rendered"), and a board profile that leaves one null is a profile bug, not a frame to guess
	// the metrics for.
	if (s.style == nullptr) {
		return false;
	}
	s.canvas.clear();
	const std::span<Scene* const> scenes = stack_.scenes();
	if (scenes.empty()) {
		return true;
	}
	// The lowest scene that has to be drawn -- the same walk step 8b's arming reads, so there is one
	// copy of it (`Shell::oled_lowest`).
	const size_t lowest = oled_lowest(s.role);
	Scene* const focus = &stack_.input_top();
	for (size_t k = lowest; k < scenes.size(); k++) {
		// Exactly one scene per pass owns the tick's input, and it is the same one the router hands
		// events to: the input-frontmost scene (`Scene::input_transparent`). Everything else in the
		// pass -- beneath it, or a passive layer above it -- draws with `InputMask::none()`, so its
		// widgets still paint but read 0/false and claim nothing (`ui/oled/frame.h`, `root_frame`'s
		// `mask` note). Keying this on the *topmost* scene instead would let a console line or the
		// working animation, which route nothing, deafen the view they are drawn over.
		const InputMask mask = scenes[k] == focus ? InputMask::all() : InputMask::none();
		ui::oled::OledFrame frame = ui::oled::root_frame(s.canvas, *s.style, lowering_.ctx(), mask);
		scenes[k]->render_oled(s.role, frame);
		append_rendered(scenes[k]);
	}
	return true;
}

void Shell::dim_grid(gfx::grid::PadGrid& grid) {
	// Dim what is underneath by one bit per channel, so the overlay reads as the live layer.
	//
	// **Spec design, not a port.** Legacy has no such dim: the only thing it does to the layer
	// under an overlay is the greyout, applied in the *send* path by `prepareColour`
	// (`hid/led/pad_leds.cpp:283-298`), and its overlays (the sidebar menus, the context menus)
	// simply overwrite the pads they cover. `RGB::dim(1)` is `channel >> 1` per channel
	// (`gfx/grid/rgb.h`), which is what spec D §7 asks for in so many words.
	//
	// @warning **That sentence is an OPEN FLAG, not a settled design note**, and reading it as
	//          settled is the mistake this paragraph exists to prevent. The UI-stack port's
	//          standing constraint is that everything be a port of something legacy already does;
	//          this is one of four inventions Kate has deferred rather than decided. It is argued
	//          at `src/shell/README.md`'s composition section, and `src/scenes/README.md`'s carry
	//          table ties it to the sidebar divergence the F1 level-3 A/B found empirically --
	//          which is plausibly the same question, since both are about what the grid under an
	//          open menu should look like. Since F1 this function runs inside the shipping
	//          firmware (`ShellHostUI`'s guest), so it is live code rather than a design note.
	//          Do not remove the flag, and do not treat it as decided, without settling both.
	const gfx::grid::GridDimensions d = grid.dims();
	for (uint8_t y = 0; y < d.rows; y++) {
		for (uint8_t x = 0; x < d.cols(); x++) {
			grid.set(x, y, grid.at(x, y).dim(1));
		}
	}
}

void Shell::render_grid(uint32_t now_ms) {
	GridSurface& g = surfaces_.grid();
	if (!g.grid_storage.has_value()) {
		return;
	}
	gfx::grid::PadGrid& grid = g.grid();
	grid.clear();
	const std::span<Scene* const> scenes = stack_.scenes();
	if (scenes.empty()) {
		return;
	}
	// The topmost scene that is not a grid overlay -- the same walk step 8b's arming reads, so there
	// is one copy of it (`Shell::grid_base`).
	const size_t base = grid_base();
	const bool has_overlay = scenes.size() > base + 1;
	// Spec D §7 dims "if any overlay is above", but an overlay that composites nothing may opt out
	// (`Scene::dims_grid`) -- a popup must not halve the brightness of a grid it never touches.
	bool dim = false;
	for (size_t k = base + 1; k < scenes.size(); k++) {
		dim = dim || scenes[k]->dims_grid();
	}

	// Same focus rule as `render_oled`: the input-frontmost scene, not simply the topmost one.
	Scene* const focus = &stack_.input_top();
	{
		const InputMask mask = scenes[base] == focus ? InputMask::all() : InputMask::none();
		ui::grid::GridFrame frame = ui::grid::root_grid_frame(grid, lowering_.ctx(), mask);
		scenes[base]->render_grid(frame);
		append_rendered(scenes[base]);
	}

	if (has_overlay) {
		if (dim) {
			dim_grid(grid);
		}
		for (size_t k = base + 1; k < scenes.size(); k++) {
			const InputMask mask = scenes[k] == focus ? InputMask::all() : InputMask::none();
			ui::grid::GridFrame frame = ui::grid::root_grid_frame(grid, lowering_.ctx(), mask);
			scenes[k]->render_grid(frame);
			append_rendered(scenes[k]);
		}
	}

	// A `GreyOut` tween ramps `greyout_`'s own mask in place -- legacy's ramp
	// (`pad_leds.cpp:983-1005`) is the only thing that ever changes `greyProportion` gradually,
	// and `prepareColour` (`:293-296`) applies whichever value is current to the same masked pads
	// regardless of whether it is ramping or settled. So while that tween is running, it *replaces*
	// this static full-strength pass rather than stacking underneath it; running both would grey
	// the frame twice; the tween block below applies the masked `ui::grid::greyout` itself, at the
	// ramped or (on completion) full proportion.
	const bool greyout_tween_active = g.tween.has_value() && g.tween->kind == ActiveTween::Kind::GreyOut;
	if (!greyout_tween_active && (greyout_.cols != 0 || greyout_.rows != 0)) {
		// Last, and once: `ui::grid::greyout` mutates the grid rather than tinting on the way out,
		// so anything painted after it would escape the tint (`ui/grid/effects.h`'s warning).
		// `InputMask::none()`: an effect pass paints, it does not consume input.
		ui::grid::GridFrame frame = ui::grid::root_grid_frame(grid, lowering_.ctx(), InputMask::none());
		ui::grid::greyout(frame, greyout_, kGreyoutAmountFull);
	}

	// A grid tween blends the whole composed frame above -- scenes, overlay dim and greyout alike
	// -- so it runs last, from the snapshot `start_tween` took of the grid at the moment it was
	// called toward *this* tick's freshly composed grid (which plays "to").
	if (g.tween.has_value() && tween_from_.has_value()) {
		const ActiveTween& tw = *g.tween;
		const uint32_t elapsed = now_ms - tw.start_ms;
		if (elapsed >= tw.duration_ms) {
			if (greyout_tween_active && (greyout_.cols != 0 || greyout_.rows != 0)) {
				// The ramp has reached its target; settle at full strength once, matching legacy's
				// `amountDone > 1` snap to `setGreyoutAmount(1)` (`pad_leds.cpp:986-988`) -- the
				// static pass above was skipped this tick precisely because this tween was still
				// live when that check ran.
				ui::grid::GridFrame frame = ui::grid::root_grid_frame(grid, lowering_.ctx(), InputMask::none());
				ui::grid::greyout(frame, greyout_, kGreyoutAmountFull);
			}
			// Otherwise the live grid above is already the final frame; nothing left to blend.
			g.tween.reset();
			tween_from_.reset();
			tween_scratch_.reset();
		}
		else {
			const uint16_t t = tween_progress(elapsed, tw.duration_ms);
			switch (tw.kind) {
			case ActiveTween::Kind::Fade:
				gfx::grid::fade(*tween_from_, grid, t, grid);
				break;
			case ActiveTween::Kind::ScrollH: {
				// Unlike fade()/grey_out(), scroll_horizontal() reads `incoming` at column indices
				// other than the one it is writing, so passing `grid` as both `incoming` and `out`
				// could read a column this same pass already overwrote. `tween_scratch_` keeps the
				// read and write sides distinct; it is then copied back onto the live grid.
				const int32_t offset = static_cast<int32_t>((static_cast<int64_t>(tw.param) * t) / 65536);
				gfx::grid::scroll_horizontal(*tween_from_, grid, offset, *tween_scratch_);
				grid = *tween_scratch_;
				break;
			}
			case ActiveTween::Kind::ScrollV: {
				const int32_t offset = static_cast<int32_t>((static_cast<int64_t>(tw.param) * t) / 65536);
				gfx::grid::scroll_vertical(*tween_from_, grid, offset, *tween_scratch_);
				grid = *tween_scratch_;
				break;
			}
			case ActiveTween::Kind::GreyOut: {
				// Masked, not `gfx::grid::grey_out`'s unconditional "every main column": see the
				// class comment above on why this replaces the static pass rather than adding to it.
				const int32_t proportion = static_cast<int32_t>((static_cast<int64_t>(tw.param) * t) / 65536);
				ui::grid::GridFrame frame = ui::grid::root_grid_frame(grid, lowering_.ctx(), InputMask::none());
				ui::grid::greyout(frame, greyout_, proportion);
				break;
			}
			}
			// Still mid-tween: ask for another frame at this kind's step cadence so the animation
			// keeps stepping even with no input and no model change -- and so `next_wake()` can
			// actually see it (`request_repaint()` alone arms no deadline).
			g.gate.request_repaint_after(tween_cadence_ms(tw.kind), now_ms);
		}
	}
}

void Shell::deliver_post_render_input(uint32_t now_ms) {
	const UiContext ctx = make_context(now_ms);
	// `Router::route_post_render` is what applies the claim map (`Encoder{Select}` -> `turn`,
	// ... `Button{Back}` -> `back`, documented on that method): it drops a claimed event outright
	// rather than routing it, so the gate lives in one place instead of being restated here.
	// Size snapshotted first: routing can reach arbitrary application code, and a feed from
	// anything it calls would append to the same vector mid-iteration. Such an event belongs to the
	// *next* tick's window -- it was never in this tick's snapshot and no widget could have claimed
	// it -- so it is deliberately left for the next `begin_tick` rather than routed here.
	const size_t n = lowering_.held_back().size();
	for (size_t i = 0; i < n; i++) {
		router_.route_post_render(lowering_.held_back()[i], ctx, lowering_.ctx().claims);
	}
	// Not cleared here: `InputLowering::begin_tick` owns that, and the window this shell opened is
	// closed at the bottom of `tick()` so the next feed re-opens (and therefore clears) it. See
	// the tick-window note on the class.
}

void Shell::derive_leds(uint32_t now_ms) {
	LedSurface& leds = surfaces_.leds();
	leds.frame.clear();
	LedCompose::compose(leds.frame, std::span<Service* const>{service_list_.begin(), service_list_.size()},
	                    stack_.scenes());
	leds.desired_on.fill(false);
	leds.desired_declared.fill(false);
	for (const auto& [id, state] : leds.frame.entries()) {
		const size_t board_led = static_cast<size_t>(static_cast<uint8_t>(id));
		if (board_led >= LedFrame::kMaxEntries) {
			// A board LED past the shadow arrays cannot be diffed or sent. `LedFrame::set` returns
			// a bool precisely so a dropped declaration is not swallowed, and neither is this one:
			// the count is the diagnostic that a board profile has outgrown `kMaxEntries`.
			leds_dropped_++;
			continue;
		}
		leds.desired_declared[board_led] = true;
		leds.desired_on[board_led] = led_is_on(state, now_ms);
	}
}

void Shell::start_tween(ActiveTween tw) {
	GridSurface& g = surfaces_.grid();
	if (!g.grid_storage.has_value()) {
		// No grid to snapshot or animate yet (the surfaces haven't been `build()`-t). Refuse
		// outright, and clear any stale state, rather than leaving a tween armed to activate later
		// against whatever the grid happens to hold once `build()` finally runs -- `render_grid`'s
		// guard requires both `g.tween` and `tween_from_`, so a half-set pair here would otherwise
		// sit inert indefinitely and then fire against an unrelated snapshot.
		g.tween.reset();
		tween_from_.reset();
		tween_scratch_.reset();
		return;
	}
	tween_from_.emplace(g.grid());
	tween_scratch_.emplace(g.grid().dims());
	g.tween = tw;
	// Arms the deadline `next_wake()` needs to see, not just the request `needs_frame` consumes --
	// `request_repaint()` alone leaves no deadline for a scheduler to wake on.
	g.gate.request_repaint_after(tween_cadence_ms(tw.kind), tw.start_ms);
	// `next_wake()` is otherwise only refreshed at the end of `tick()`; a caller starting a tween
	// from outside the tick loop (e.g. in response to some external event) needs an up-to-date
	// answer without first forcing a whole tick just to find out.
	next_wake_ = compute_next_wake(tw.start_ms);
}

void Shell::flush(uint32_t now_ms) {
	bool any_sent = false;

	// OLED: one blit per surface that rendered this tick, skipping (and leaving `blit_pending`
	// armed on) a panel the backend says cannot take a frame right now.
	//
	// This comment used to say that was "the `deluge_display_busy` skip around
	// `OLED::sendMainImage`". There is no such skip: `OLED::sendMainImage`
	// (`hid/display/oled.cpp:335-385`) gates only on its own `needsSending` flag, and no file under
	// `src/deluge/` calls `deluge_display_busy` or `deluge_display_busy_n` at all -- the only
	// callers in the tree are the BSPs that define them and `board_surfaces.cpp` (F1 task 4,
	// grepped repo-wide). What legacy actually consults before pushing a panel is the pad-output
	// gate in `doAnyPendingUIRendering` (`gui/ui/ui.cpp:375-396`, the `if` at `:389`), which guards
	// the grid and the OLED render together. `oled_busy` is therefore whatever the backend's own
	// pre-push condition is: `deluge_display_busy_n` for `BoardSurfaceSet`, that legacy gate for
	// the guest (`gui/ui/shell_guest_surfaces.cpp`).
	for (OledSurface& s : surfaces_.oled()) {
		if (!s.blit_pending) {
			continue;
		}
		if (surfaces_.oled_busy(s.index)) {
			continue;
		}
		surfaces_.blit(s.index, s.canvas.bytes(), s.geometry.width, s.geometry.height);
		s.blit_pending = false;
		any_sent = true;
		// `HIDSysex` mirrors the one main panel, not every OLED a board might have
		// (`sendMainImage` -> `HIDSysex::sendDisplayIfChanged`, `hid/display/oled.cpp:378`).
		if (s.role == SurfaceRole::Main) {
			mirror_.after_blit(s, now_ms);
		}
	}
	// Deliver a frame the throttle held back, once its interval has passed -- independent of
	// whether anything blitted *this* tick, since the deferred send must still go out on its own
	// schedule (`Mirror::retry`'s doc).
	mirror_.retry(now_ms);

	// Grid: send the column pairs that changed since `last_sent`, budgeted by the control
	// surface's free pad-output space. Port of `sendOutMainPadColours`'s backpressure check
	// (`hid/led/pad_leds.cpp:1019-1037`, gated on `kNumBytesInMainPadRedraw`) generalised to a
	// per-pair budget instead of an all-or-nothing gate, so a large backlog drains over several
	// ticks instead of stalling until the whole frame fits at once -- the same spirit as
	// `timerRoutine`'s retry-soon (`:850`) and `ui.cpp:389`'s *2 headroom, but pin the tick, not
	// the byte count, as the unit of back-pressure retry.
	GridSurface& g = surfaces_.grid();
	if (g.dirty && !g.grid_storage.has_value()) {
		// Dirty with nothing to read: clear the flag rather than latching it. `render_grid` only
		// sets `dirty` after its own `grid_storage` check, so this is unreachable today -- but the
		// set and the clear live in different functions and only one of them is guarded, so the
		// pair is closed here rather than left to stay closed by coincidence.
		g.dirty = false;
	}
	if (g.dirty) {
		const gfx::grid::PadGrid& grid = g.grid();
		const uint32_t budget = surfaces_.pad_output_space() / kBytesPerColumnPair;
		uint32_t sent = 0;
		bool any_remaining = false;
		for (uint8_t pair = 0; pair < g.pairs; pair++) {
			const ColumnPair current = read_column_pair(grid, pair);
			if (g.sent_once[pair] && current == g.last_sent[pair]) {
				continue;
			}
			if (sent >= budget) {
				// Changed, but no budget left this tick: leave it dirty for the next flush to
				// retry, in ascending order again (today's `sendOutMainPadColoursSoon`).
				any_remaining = true;
				continue;
			}
			surfaces_.send_columns(pair, current);
			g.last_sent[pair] = current;
			g.sent_once[pair] = true;
			any_sent = true;
			sent++;
		}
		g.dirty = any_remaining;
		if (any_remaining) {
			// Arms a real deadline (not just a request), so `next_wake()` reports it and a
			// scheduler that would otherwise sleep until the next input instead wakes to retry.
			g.gate.request_repaint_after(kBackpressureRetryMs, now_ms);
		}
	}

	// LEDs: diff this tick's desired on/off (`Shell::derive_leds`) against what was last sent.
	// `i` is `LedId`'s own underlying value -- the shadow-array index `derive_leds` and
	// `led_send_needed` both use -- and the board profile maps that to the physical board LED
	// `SurfaceSet::set_led` takes.
	LedSurface& leds = surfaces_.leds();
	for (size_t i = 0; i < LedFrame::kMaxEntries; i++) {
		if (!led_send_needed(leds, i)) {
			continue;
		}
		// Whether or not there is a board LED to actually send this id to, the shadow is marked
		// caught-up either way: an id with no mapping can never become "not needed" any other way,
		// and leaving it unmarked would make `led_send_needed` re-flag (and this loop re-look-up)
		// the same unmapped id every single tick forever.
		leds.last_on[i] = leds.desired_on[i];
		leds.sent_once[i] = true;
		if (i >= profile_.led_by_button.size()) {
			// No board mapping for this `LedId` value at all (an "extras" id past the button
			// table -- Task 10); nothing to send it as.
			continue;
		}
		const int8_t board_index = profile_.led_by_button[i];
		if (board_index < 0) {
			continue; // -1: this id names no board LED.
		}
		surfaces_.set_led(static_cast<uint8_t>(board_index), leds.desired_on[i]);
		any_sent = true;
	}

	// Knob indicators: diff against `last_indicators`, only while declared this tick -- an
	// indicator nothing declared is left exactly as it was, the same rule as an undeclared LED.
	for (uint8_t which = 0; which < kIndicatorCount; which++) {
		const KnobIndicator* ind = leds.frame.indicator_or_null(static_cast<IndicatorId>(which));
		if (ind == nullptr) {
			continue;
		}
		if (leds.indicators_sent[which] && same_indicator(*ind, leds.last_indicators[which])) {
			continue;
		}
		surfaces_.set_indicator(which, *ind);
		leds.last_indicators[which] = *ind;
		leds.indicators_sent[which] = true;
		any_sent = true;
	}

	if (any_sent) {
		surfaces_.flush();
	}
}

std::optional<uint32_t> Shell::compute_next_wake(uint32_t now_ms) {
	std::optional<uint32_t> best;
	// Difference form, not `*at < *best`: these are absolute clocks that wrap at ~49.7 days of
	// uptime, and a plain `<` across the wrap picks the *later* of two deadlines that straddle it.
	// `now_ms + *in` below may itself wrap, which is exactly why the comparison must not care.
	// Same form as `RepaintGate::request_repaint_after` (`ui/core/repaint_gate.cpp:7`).
	const auto consider = [&best](std::optional<uint32_t> at) {
		if (at.has_value() && (!best.has_value() || static_cast<int32_t>(*at - *best) < 0)) {
			best = at;
		}
	};
	// Gate deadlines are absolute; scene and service timers are "milliseconds from now".
	for (OledSurface& s : surfaces_.oled()) {
		consider(s.gate.next_deadline());
	}
	consider(surfaces_.grid().gate.next_deadline());
	consider(lowering_.ctx().gate.next_deadline());
	consider(mirror_.next_deadline());
	// Every scene whose timer did NOT reach a gate. The skip is on the *arm having happened*, not on
	// the mask being non-empty (E3 final review, A3): step 8b arms a scene's claimed surfaces only
	// where that scene can actually paint, so three cases reach this loop with no gate deadline
	// standing in for them -- the full opt-out (`RepaintSurfaces::any() == false`), a scene covered
	// on every surface it claims, and an OLED-only claim on a profile with no OLED surfaces. The
	// mask-only test missed the last of those even before A2.
	//
	// A covered scene still needs the wake, which is why this is not "a scene that can paint nothing
	// needs no wake": `Shell::tick_stack` calls `Scene::tick(dt)` on every scene down to the first
	// `occludes_grid()`, whatever the render pass decides, so a covered scene's clock still advances
	// and the moment it asked to be woken at is still the moment it wants to be ticked at. (What it
	// does *not* get while covered is `after_render()`, which runs only for `rendered_this_tick_`.)
	const std::span<Scene* const> scenes = stack_.scenes();
	for (size_t k = 0; k < scenes.size(); k++) {
		if (timer_arms_a_gate(k, scenes[k]->timer_repaints())) {
			continue;
		}
		if (const std::optional<uint32_t> in = scenes[k]->next_timer(); in.has_value()) {
			consider(now_ms + *in);
		}
	}
	for (Service* service : service_list_) {
		if (const std::optional<uint32_t> in = service->next_timer(); in.has_value()) {
			consider(now_ms + *in);
		}
	}
	// **A declared blink is a wake source too** (E4 task 4 fix round 1). Every source above is
	// something a scene or service *asked* for; a `LedState::Kind::Blink` asks for nothing, because
	// `led_is_on` derives its phase from `now_ms` alone (`shell/leds.cpp`). So without this loop a
	// declared blink froze at whatever phase the last tick left it in for as long as nothing else
	// happened -- and that was live rather than theoretical: `MenuScene::leds` declares BACK and
	// LEARN as `Blink(kFlashTime)` unconditionally (`scenes/menu/menu_scene.cpp:955`, `:962`) while
	// `MenuScene::next_timer` (`:1591-1605`) answers `nullopt` for any focused node with neither
	// source pads nor a shortcut position.
	//
	// Read from the frame `derive_leds` composed a few lines earlier in `tick()`, so this is the
	// same declaration `flush` is about to send. `start_tween`'s out-of-band call sees the previous
	// tick's frame, which is the right answer for a blink that is still declared and a harmless
	// early wake for one that is not.
	//
	// It also removes an aliasing hazard rather than merely a freeze: a scene whose own timer
	// happens to be armed at some unrelated cadence (`MenuScene`'s 180 ms pad blink) does not
	// divide the LED's 110 ms period, so even an "armed" system rendered the blink at beat
	// frequencies. The wake now lands on the LED's own boundaries.
	for (const auto& [id, state] : surfaces_.leds().frame.entries()) {
		if (const std::optional<uint32_t> in = led_next_change(state, now_ms); in.has_value()) {
			consider(now_ms + *in);
		}
	}
	return best;
}

std::string Shell::state_lines() const {
	std::string out;
	const std::span<Scene* const> scenes = stack_.scenes();
	bool popup_shown = false;
	for (size_t k = scenes.size(); k-- > 0;) {
		const SceneKind kind = scenes[k]->kind();
		const uint16_t arg = stack_.id_at(k).arg;
		out += "scene ";
		if (const char* name = scene_kind_name(kind); name != nullptr) {
			out += name;
			// The one shell kind whose *flavour* is visible on screen, and therefore the one worth
			// qualifying: two popups with the same text but different `PopupType`s draw different
			// boxes. Every other shell scene is spelled bare -- their `arg`s (the screensaver's
			// seed, say) are inputs to an animation, not something a snapshot can be read against.
			if (kind == SceneKind::Popup) {
				popup_shown = true;
				out += '/';
				if (const char* type = popup_type_name(popup_.popup_type()); type != nullptr) {
					out += type;
				}
				else {
					out += std::to_string(static_cast<unsigned>(popup_.popup_type()));
				}
			}
		}
		else {
			// An application kind, which the shell has no vocabulary for: spelled numerically,
			// qualified by the `arg` that selects *which* clip/section/track the scene is showing.
			// Omitted when zero, so the common "the scene, no argument" case stays `app16`.
			out += "app";
			out += std::to_string(static_cast<unsigned>(kind));
			if (arg != 0) {
				out += '/';
				out += std::to_string(static_cast<unsigned>(arg));
			}
		}
		out += '\n';
	}
	if (lowering_.input_locked()) {
		// One lock, one reason: `InputLowering` models the stem-export/song-load gate as a bare
		// bool (`set_input_lock`), so there is no reason to spell. If it ever grows one, this is
		// where the reason's name goes -- the line's shape already has room for it.
		out += "lock input\n";
	}
	if (popup_shown) {
		out += "notice ";
		out += notice_kind_name(notices_.last_notice_kind());
		out += ' ';
		out += notices_.last_popup_text();
		out += '\n';
	}
	return out;
}

} // namespace deluge::ui::shell
