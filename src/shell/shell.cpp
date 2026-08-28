#include "shell/shell.h"

#include "notify/notice_queue.h"
#include "ui/grid/frame.h"
#include "ui/oled/frame.h"

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

/// @brief Does \p dirty carry any fact that names no particular surface?
///
/// `Oled` and the four grid facts each name a surface; everything else is a model change whose
/// consequences the shell cannot attribute, so it invalidates both sides.
bool has_untargeted_fact(const notify::DirtySet& dirty) {
	return dirty.has(Dirty::SampleReady) || dirty.has(Dirty::PlaybackState) || dirty.has(Dirty::SongSwapped)
	       || dirty.has(Dirty::OutputChanged) || dirty.has(Dirty::ParamAutomation) || dirty.has(Dirty::ModTarget)
	       || dirty.has(Dirty::SelectedDrum) || dirty.has(Dirty::MidiLearn);
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
	case SceneKind::Boot:
		return "Boot";
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
      router_(stack_, services, lowering_), notices_(stack_, profile_, popup_, console_, working_) {
	for (size_t i = 0; i < table_.size(); i++) {
		table_[i] = registry.find(static_cast<SceneKind>(i));
	}
	// The shell owns its three overlay kinds outright: `Notices` drives *these* instances, so a
	// scene an application registered in one of those slots could never be told anything.
	table_[static_cast<size_t>(SceneKind::Popup)] = &popup_;
	table_[static_cast<size_t>(SceneKind::Console)] = &console_;
	table_[static_cast<size_t>(SceneKind::Working)] = &working_;
}

void Shell::init(SceneId initial) {
	stack_.push(initial);
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
	lowering_.on_board_event(ev, now_ms);
}

void Shell::on_encoder_edges(uint8_t index, int8_t edges, uint32_t now_ms) {
	ensure_tick_window(now_ms);
	lowering_.on_encoder_edges(index, edges, now_ms);
}

void Shell::set_sd_busy(bool busy) {
	lowering_.set_sd_busy(busy);
}

// -- The tick ---------------------------------------------------------------------------------

void Shell::tick(uint32_t now_ms) {
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
		const std::string_view text =
		    profile_.text != nullptr ? profile_.text(router_.pending_popup_string()) : std::string_view{};
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
	// The widgets' gate is shell-wide and `needs_frame` *consumes* the request, so it is evaluated
	// exactly once and the answer OR'd into every surface's own decision -- asking it per surface
	// would give the request to whichever surface happened to be checked first.
	const bool widgets_want_frame = lowering_.ctx().gate.needs_frame(now_ms, had_input_, dirty.any());
	// 5. OLED.
	for (OledSurface& s : surfaces_.oled()) {
		if (s.gate.needs_frame(now_ms, had_input_, dirty.any()) || widgets_want_frame
		    || router_.repaint_requested(s.role)) {
			render_oled(s);
			s.blit_pending = true;
		}
	}
	// 6. Grid.
	GridSurface& g = surfaces_.grid();
	if (g.gate.needs_frame(now_ms, had_input_, dirty.any()) || widgets_want_frame) {
		render_grid(now_ms);
		g.dirty = true;
	}
	// 7. Navigation derived from what this tick's render observed.
	for (Scene* scene : rendered_this_tick_) {
		router_.apply(scene->after_render());
	}
	// 8. The held-back events, now that the claims they are gated on are knowable.
	deliver_post_render_input(now_ms);
	// 9. LEDs, every tick.
	derive_leds(now_ms);
	flush(now_ms); // Task 8
	router_.clear_repaint_requests();
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
	lowering_.end_tick();
	UiEvent event{UiEvent::Kind::AllReleased};
	while (lowering_.next(event)) {
		router_.route(event, make_context(now_ms));
	}
	had_input_ = lowering_.had_input_this_tick();
}

FixedVector<Service*, 4> Shell::service_list() {
	FixedVector<Service*, 4> out;
	Service* const all[] = {services_.mod_knobs, services_.global_controls, services_.midi_learn, &notices_};
	for (Service* service : all) {
		if (service == nullptr) {
			continue;
		}
		// The three `Services` slots default to the one shared `null_service()`, and nothing stops
		// an application putting one object in two of them; a service must be ticked once.
		bool seen = false;
		for (Service* already : out) {
			seen = seen || already == service;
		}
		if (!seen) {
			(void)out.push_back(service);
		}
	}
	return out;
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
	for (Service* service : service_list()) {
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

void Shell::render_oled(OledSurface& s) {
	// A surface with no style cannot be drawn (`surfaces.h`: "such a surface must not be
	// rendered"), and a board profile that leaves one null is a profile bug, not a frame to guess
	// the metrics for.
	if (s.style == nullptr) {
		return;
	}
	s.canvas.clear();
	const std::span<Scene* const> scenes = stack_.scenes();
	if (scenes.empty()) {
		return;
	}
	// Walk down from the top while each scene lets the one below show through; the scene that
	// stops the walk is the lowest one that has to be drawn. This is the port of
	// `OLED::sendMainImage`'s if-chain (`hid/display/oled.cpp:364-376`), which asked the same
	// question with two hard-coded layers (console over main, popup over that) and a
	// `copyBackgroundAroundForeground` blit per layer; here it is a property each scene declares.
	size_t lowest = scenes.size() - 1;
	while (lowest > 0 && scenes[lowest]->oled_transparent(s.role)) {
		lowest--;
	}
	for (size_t k = lowest; k < scenes.size(); k++) {
		// Only the frontmost scene on this surface owns the tick's input: everything beneath it
		// draws with `InputMask::none()`, so its widgets still paint but read 0/false and claim
		// nothing (`ui/oled/frame.h`, `root_frame`'s `mask` note).
		const InputMask mask = (k + 1 == scenes.size()) ? InputMask::all() : InputMask::none();
		ui::oled::OledFrame frame = ui::oled::root_frame(s.canvas, *s.style, lowering_.ctx(), mask);
		scenes[k]->render_oled(s.role, frame);
		append_rendered(scenes[k]);
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
	// The base is the topmost scene that is not a grid overlay; everything above it is one, by
	// construction. A stack of nothing but overlays has no base and starts at the root.
	size_t base = 0;
	for (size_t k = scenes.size(); k-- > 0;) {
		if (!scenes[k]->is_grid_overlay()) {
			base = k;
			break;
		}
	}
	const bool has_overlay = scenes.size() > base + 1;

	{
		const InputMask mask = has_overlay ? InputMask::none() : InputMask::all();
		ui::grid::GridFrame frame = ui::grid::root_grid_frame(grid, lowering_.ctx(), mask);
		scenes[base]->render_grid(frame);
		append_rendered(scenes[base]);
	}

	if (has_overlay) {
		// Dim what is underneath by one bit per channel, so the overlay reads as the live layer.
		//
		// **Spec design, not a port.** Legacy has no such dim: the only thing it does to the layer
		// under an overlay is the greyout below, applied in the *send* path by `prepareColour`
		// (`hid/led/pad_leds.cpp:283-298`), and its overlays (the sidebar menus, the context
		// menus) simply overwrite the pads they cover. `RGB::dim(1)` is `channel >> 1` per channel
		// (`gfx/grid/rgb.h`), which is what spec D §7 asks for in so many words.
		const gfx::grid::GridDimensions d = grid.dims();
		for (uint8_t y = 0; y < d.rows; y++) {
			for (uint8_t x = 0; x < d.cols(); x++) {
				grid.set(x, y, grid.at(x, y).dim(1));
			}
		}
		for (size_t k = base + 1; k < scenes.size(); k++) {
			const InputMask mask = (k + 1 == scenes.size()) ? InputMask::all() : InputMask::none();
			ui::grid::GridFrame frame = ui::grid::root_grid_frame(grid, lowering_.ctx(), mask);
			scenes[k]->render_grid(frame);
			append_rendered(scenes[k]);
		}
	}

	if (greyout_.cols != 0 || greyout_.rows != 0) {
		// Last, and once: `ui::grid::greyout` mutates the grid rather than tinting on the way out,
		// so anything painted after it would escape the tint (`ui/grid/effects.h`'s warning).
		// `InputMask::none()`: an effect pass paints, it does not consume input.
		ui::grid::GridFrame frame = ui::grid::root_grid_frame(grid, lowering_.ctx(), InputMask::none());
		ui::grid::greyout(frame, greyout_, kGreyoutAmountFull);
	}

	// Task 8 evaluates `g.tween` here, into the output grid.
	(void)now_ms;
}

void Shell::deliver_post_render_input(uint32_t now_ms) {
	const UiContext ctx = make_context(now_ms);
	// `Router::route_post_render` is what applies the claim map (`Encoder{Select}` -> `turn`,
	// ... `Button{Back}` -> `back`, documented on that method): it drops a claimed event outright
	// rather than routing it, so the gate lives in one place instead of being restated here.
	for (const UiEvent& event : lowering_.held_back()) {
		router_.route_post_render(event, ctx, lowering_.ctx().claims);
	}
	// Not cleared here: `InputLowering::begin_tick` owns that, and the window this shell opened is
	// closed at the bottom of `tick()` so the next feed re-opens (and therefore clears) it. See
	// the tick-window note on the class.
}

void Shell::derive_leds(uint32_t now_ms) {
	LedSurface& leds = surfaces_.leds();
	leds.frame.clear();
	const FixedVector<Service*, 4> services = service_list();
	LedCompose::compose(leds.frame, std::span<Service* const>{services.begin(), services.size()}, stack_.scenes());
	leds.desired_on.fill(false);
	leds.desired_declared.fill(false);
	for (const auto& [id, state] : leds.frame.entries()) {
		const size_t board_led = static_cast<size_t>(static_cast<uint8_t>(id));
		if (board_led >= LedFrame::kMaxEntries) {
			continue;
		}
		leds.desired_declared[board_led] = true;
		leds.desired_on[board_led] = led_is_on(state, now_ms);
	}
}

void Shell::flush(uint32_t now_ms) {
	// Task 8. It diffs and pushes what the tick above rendered: an OLED blit per surface whose
	// `blit_pending` is set and whose panel is not `oled_busy`, the column pairs that differ from
	// `GridSurface::last_sent` (under `pad_output_space` back-pressure), and the LEDs and knob
	// indicators `led_send_needed`/`indicators_sent` say have changed -- then clears
	// `blit_pending`, `GridSurface::dirty` and the send shadows. Empty until then, but called from
	// its final position so this task's specs already pin where in the tick it runs.
	(void)now_ms;
}

std::optional<uint32_t> Shell::compute_next_wake(uint32_t now_ms) {
	std::optional<uint32_t> best;
	const auto consider = [&best](std::optional<uint32_t> at) {
		if (at.has_value() && (!best.has_value() || *at < *best)) {
			best = at;
		}
	};
	// Gate deadlines are absolute; scene and service timers are "milliseconds from now".
	for (OledSurface& s : surfaces_.oled()) {
		consider(s.gate.next_deadline());
	}
	consider(surfaces_.grid().gate.next_deadline());
	consider(lowering_.ctx().gate.next_deadline());
	for (Scene* scene : stack_.scenes()) {
		if (const std::optional<uint32_t> in = scene->next_timer(); in.has_value()) {
			consider(now_ms + *in);
		}
	}
	for (Service* service : service_list()) {
		if (const std::optional<uint32_t> in = service->next_timer(); in.has_value()) {
			consider(now_ms + *in);
		}
	}
	return best;
}

std::string Shell::state_lines() const {
	std::string out;
	const std::span<Scene* const> scenes = stack_.scenes();
	for (size_t k = scenes.size(); k-- > 0;) {
		const SceneKind kind = scenes[k]->kind();
		out += "scene ";
		if (const char* name = scene_kind_name(kind); name != nullptr) {
			out += name;
		}
		else {
			out += "app";
			out += std::to_string(static_cast<unsigned>(kind));
		}
		out += ' ';
		out += std::to_string(static_cast<unsigned>(stack_.id_at(k).arg));
		out += '\n';
	}
	if (lowering_.input_locked()) {
		out += "lock 1\n";
	}
	for (Scene* scene : scenes) {
		if (scene->kind() == SceneKind::Popup) {
			out += "notice ";
			out += notices_.last_popup_text();
			out += '\n';
			break;
		}
	}
	return out;
}

} // namespace deluge::ui::shell
