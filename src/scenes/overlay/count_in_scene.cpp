/// @file
/// @brief `CountInScene`. See `count_in_scene.h`.

#include "scenes/overlay/count_in_scene.h"

#include "ui/oled/popup.h"

#include "playback/playback_handler.h"

#include <algorithm>
#include <cstdio>

namespace deluge::scenes::overlay {

using deluge::ui::shell::Action;
using deluge::ui::shell::Result;
using deluge::ui::shell::SurfaceRole;

void CountInScene::tick(uint32_t /*dt_ms*/) {
	// `notify_drain.cpp:95-96`: the fact this scene polls for, and its edge-detector.
	const int32_t count = playbackHandler.count_in_visual();
	if (count == shown_count_) {
		return;
	}
	if (count > 0) {
		// `notify_drain.cpp:97-100`: format the digit and (re)draw. `%d` matches `intToString`'s
		// plain decimal (`util/c_string.h:31-33`, minimum one digit) for every value this ever
		// shows -- `src/scenes` may not include `util/c_string.h` (the same wall
		// `param_popup_scene.cpp` documents for `sourceToStringShort`).
		const int n = std::snprintf(digit_, sizeof(digit_), "%d", static_cast<int>(count));
		digit_len_ = n > 0 ? std::min(static_cast<size_t>(n), kTextCap) : 0;
		showing_ = true;
		was_showing_ = true;
	}
	else {
		// `notify_drain.cpp:102-103`: cancel. The masked `UI_MODE_RECORD_COUNT_IN` clear
		// (`:104-111`) has no port here -- see the class doc.
		showing_ = false;
	}
	shown_count_ = count;
}

void CountInScene::render_oled(SurfaceRole role, deluge::ui::oled::OledFrame& frame) {
	if (role != SurfaceRole::Main || !showing_) {
		return;
	}
	// `display->displayPopup(buffer, 0, true, 255, 2)` resolves, on the shipped OLED-only build, to
	// `OLED::displayPopup` -> `popupText(buffer, /*persistent=*/true, PopupType::GENERAL)`
	// (`oled.h:154-157`; the call at `notify_drain.cpp:100` passes no sixth argument so `type`
	// defaults to `PopupType::GENERAL`, `display.h:49`) -- a framed (non-`NOTIFICATION`) popup box
	// (`OLED::setupPopup`, `oled.cpp:131-134`). See the class doc for why `alignRight`/`blinkSpeed`
	// play no part in this.
	deluge::ui::oled::popup_text(frame, shown_text(), deluge::ui::oled::PopupStyle::Framed);
}

Result CountInScene::handle(const deluge::ui::shell::UiEvent& /*event*/, const deluge::ui::shell::UiContext& /*ctx*/) {
	return Result::unhandled();
}

Result CountInScene::after_render() {
	Result r;
	if (was_showing_ && !showing_) {
		was_showing_ = false;
		(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = kCountInSceneKind});
	}
	return r;
}

CountInScene& count_in_scene() {
	static CountInScene scene;
	return scene;
}

} // namespace deluge::scenes::overlay
