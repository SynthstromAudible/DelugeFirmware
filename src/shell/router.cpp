#include "shell/router.h"

namespace deluge::ui::shell {

namespace {

/// @brief Is \p e a mod button or a gold-knob turn -- the events `services.mod_knobs->handle` is
///        consulted for, *after* the top scene has had them and declined?
///
/// `mod_knobs` is the port of `View`, and `View` is what `UI`'s **base-class** implementations of
/// `modEncoderAction`/`modButtonAction` forward to (`gui/ui/ui.cpp:35-41`). The dispatchers call
/// the current UI, never `view` directly -- `interpretEncoders` at `hid/encoder_input.cpp:194`,
/// `Buttons::buttonAction`'s mod-button loop at `hid/buttons.cpp:104` -- so a UI that overrides
/// the hook wins and one that does not falls through to `View`. Hence: scene first, service
/// second. See `Router::route`'s doc for the override census.
///
/// A gold knob's own push (`ButtonId::ModEnc0`/`ModEnc1`) is included too, from one step further
/// down the same dispatcher: `Buttons::buttonAction` handles `MOD_ENCODER_0`/`MOD_ENCODER_1` in its
/// *tail* (`hid/buttons.cpp:225-231`, reached only past the `getCurrentUI()->buttonAction` call at
/// `:134`), and the line it runs there is `getCurrentUI()->modEncoderButtonAction(n, on)` -- again a
/// hook whose base implementation (`UI::modEncoderButtonAction`, `gui/ui/ui.cpp:43-45`) is a bare
/// forward to `view`, and which five UIs override (`gui/views/instrument_clip_view.h:199`,
/// `gui/ui/sound_editor.h:81`, `gui/views/session_view.h:80`,
/// `gui/views/performance_view.h:97`, `gui/views/automation_view.h:117`). Scene first, then here.
///
/// @note Plan D excluded the two pushes on the correct observation that legacy reaches them only
///       *after* the current UI. Under the inverted chain that meant "never a `mod_knobs` event",
///       which left `View::modEncoderButtonAction` (`gui/views/view.cpp:1349-1367`) unreachable in
///       the port. E4 task 3, which ports it, put them back once the chain was scene-first.
bool is_mod_or_gold(const UiEvent& e) {
	switch (e.kind) {
	case UiEvent::Kind::Button:
		return static_cast<uint8_t>(e.button) >= static_cast<uint8_t>(ButtonId::Mod0)
		       && static_cast<uint8_t>(e.button) <= static_cast<uint8_t>(ButtonId::Mod7);
	case UiEvent::Kind::EncoderButton:
		return e.button == ButtonId::ModEnc0 || e.button == ButtonId::ModEnc1;
	case UiEvent::Kind::Encoder:
		return e.encoder == EncoderRole::GoldUpper || e.encoder == EncoderRole::GoldLower;
	default:
		return false;
	}
}

/// @brief Does \p claims show a widget already claimed \p e this tick?
///
/// The map `route_post_render` is gated by: `Encoder{Select}` -> `turn`, `Encoder{GoldUpper}` ->
/// `edit0`, `Encoder{GoldLower}` -> `edit1`, `Encoder{Horizontal}` -> `horizontal`,
/// `Encoder{Vertical}` -> `vertical`, `EncoderButton{SelectEnc}` -> `select`, `Button{Back}` ->
/// `back`. Anything else reports unclaimed rather than guessing a bit for it -- `route_post_render`
/// is only ever called, from `pump_input`, with one of these six shapes.
bool already_claimed(const UiEvent& e, const InputClaims& claims) {
	switch (e.kind) {
	case UiEvent::Kind::Encoder:
		switch (e.encoder) {
		case EncoderRole::Select:
			return claims.turn;
		case EncoderRole::GoldUpper:
			return claims.edit0;
		case EncoderRole::GoldLower:
			return claims.edit1;
		case EncoderRole::Horizontal:
			return claims.horizontal;
		case EncoderRole::Vertical:
			return claims.vertical;
		default:
			return false;
		}
	case UiEvent::Kind::EncoderButton:
		return e.button == ButtonId::SelectEnc && claims.select;
	case UiEvent::Kind::Button:
		return e.button == ButtonId::Back && claims.back;
	default:
		return false;
	}
}

} // namespace

void Router::run_chain(const UiEvent& event, const UiContext& ctx) {
	// `input_top()`, not `top()`: a popup, a console line or the working animation sits on the
	// stack without ever taking the input of the scene it is drawn over (`Scene::input_transparent`,
	// and legacy's own popups, which are composited into the OLED image and route nothing --
	// `hid/display/oled.cpp:364-376`).
	Result scene_result = stack_.input_top().handle(event, ctx);
	apply(scene_result);
	if (scene_result.handled) {
		return;
	}

	// `mod_knobs` is `View`, and `View` is the *base-class fallback* the current UI drops through
	// to -- so it is consulted only once the scene has declined. See `is_mod_or_gold`.
	if (is_mod_or_gold(event)) {
		Result mod_result = services_.mod_knobs->handle(event, ctx);
		apply(mod_result);
		if (mod_result.handled) {
			return;
		}
	}

	Result global_result = services_.global_controls->handle(event, ctx);
	apply(global_result);
	if (global_result.handled) {
		return;
	}

	// `midi_learn` last, added by E4 task 5. The four `View::buttonAction` arms it owns
	// (`gui/views/view.cpp:149-167`, `:186-197`, `:200-213`, `:216-229`) sit in the SAME
	// `if`/`else if` chain as the arms `global_controls` owns, so their relative order is only
	// observable if both could claim one event -- and `GlobalControls::view_button_action` answers
	// `ViewArm::LearnOwns` for exactly those four, reporting them unhandled and suppressing its own
	// tail for them. Last is what keeps that abstention live, and it matches the order the two
	// legacy MIDI dispatchers use for the rest of this service's surface: current UI first, `view`
	// as the fallback (`playback/playback_handler.cpp:2968-2971`, `:3128-3136`).
	Result learn_result = services_.midi_learn->handle(event, ctx);
	apply(learn_result);
	if (learn_result.handled) {
		return;
	}

	dropped_++;
}

void Router::route(const UiEvent& event, const UiContext& ctx) {
	run_chain(event, ctx);
}

void Router::route_post_render(const UiEvent& event, const UiContext& ctx, const InputClaims& claims) {
	if (already_claimed(event, claims)) {
		return;
	}
	run_chain(event, ctx);
}

void Router::apply(const Result& result) {
	for (const Action& action : result.actions) {
		switch (action.kind) {
		case Action::Kind::Push:
			stack_.push(action.id);
			break;
		case Action::Kind::Replace:
			stack_.replace_top(action.id);
			break;
		case Action::Kind::ReplaceRoot:
			stack_.replace_root(action.id);
			break;
		case Action::Kind::Pop:
			stack_.pop(action.pop_to);
			break;
		case Action::Kind::Remove:
			stack_.remove(action.pop_to);
			break;
		case Action::Kind::Popup:
			popup_string_ = action.popup_string;
			popup_type_ = action.popup_type;
			break;
		case Action::Kind::Repaint: {
			// SurfaceRole::None keeps its "no surface, never matches" meaning (surfaces.h): it is
			// a no-op here, not an "all surfaces" sentinel -- RepaintAll is that.
			const size_t i = static_cast<size_t>(action.surface);
			if (i < kSurfaceCount) {
				repaint_[i] = true;
			}
			break;
		}
		case Action::Kind::RepaintAll:
			for (bool& flag : repaint_) {
				flag = true;
			}
			// The grid too. `SurfaceRole` names OLED panels only, so this is the only action that
			// can reach the pads at all -- see `grid_repaint_requested()`.
			grid_repaint_ = true;
			break;
		case Action::Kind::LockInput:
			lowering_.set_input_lock(action.lock);
			break;
		}
	}
}

bool Router::repaint_requested(SurfaceRole role) const {
	const size_t i = static_cast<size_t>(role);
	return i < kSurfaceCount && repaint_[i];
}

void Router::clear_repaint_requests() {
	for (bool& flag : repaint_) {
		flag = false;
	}
	grid_repaint_ = false;
}

} // namespace deluge::ui::shell
