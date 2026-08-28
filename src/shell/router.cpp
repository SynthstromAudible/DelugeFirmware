#include "shell/router.h"

namespace deluge::ui::shell {

namespace {

/// @brief Is \p e a mod button or a gold-knob turn -- the events `services.mod_knobs->handle` is
///        consulted for before the top scene sees them?
///
/// @note A gold knob's own push (`ButtonId::ModEnc0`/`ModEnc1`) is deliberately **not** included:
///       legacy routes it to `getCurrentUI()->modEncoderButtonAction` from the tail block, after
///       the scene's own `buttonAction` has already run and declined to handle it
///       (`hid/buttons.cpp:225-231`, reached only past the `buttonAction` call at `:134`) -- so it
///       goes through the ordinary chain (scene, then `global_controls`) like any other plain
///       button, never straight to `mod_knobs`.
bool is_mod_or_gold(const UiEvent& e) {
	switch (e.kind) {
	case UiEvent::Kind::Button:
		return static_cast<uint8_t>(e.button) >= static_cast<uint8_t>(ButtonId::Mod0)
		       && static_cast<uint8_t>(e.button) <= static_cast<uint8_t>(ButtonId::Mod7);
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
	if (is_mod_or_gold(event)) {
		Result mod_result = services_.mod_knobs->handle(event, ctx);
		apply(mod_result);
		if (mod_result.handled) {
			return;
		}
	}

	// `input_top()`, not `top()`: a popup, a console line or the working animation sits on the
	// stack without ever taking the input of the scene it is drawn over (`Scene::input_transparent`,
	// and legacy's own popups, which are composited into the OLED image and route nothing --
	// `hid/display/oled.cpp:364-376`).
	Result scene_result = stack_.input_top().handle(event, ctx);
	apply(scene_result);
	if (scene_result.handled) {
		return;
	}

	Result global_result = services_.global_controls->handle(event, ctx);
	apply(global_result);
	if (global_result.handled) {
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
}

} // namespace deluge::ui::shell
