/// @file
/// @brief `ScreensaverScene`. See `screensaver_scene.h`.

#include "scenes/overlay/screensaver_scene.h"

namespace deluge::scenes::overlay {

using deluge::project::ScreensaverMode;
using deluge::ui::shell::Result;
using deluge::ui::shell::SurfaceRole;

bool ScreensaverScene::animated(ScreensaverMode mode) {
	// `isAnimated` (`hid/display/screensaver.cpp`), expression for expression.
	return mode == ScreensaverMode::STARSCAPE || mode == ScreensaverMode::DELUGE;
}

bool ScreensaverScene::on_enter(const deluge::ui::shell::SceneId& id) {
	since_frame_ms_ = 0;
	// The seed, and only when there is one: zero is `ShellConfig::seed`'s shipped default and
	// legacy has no seed at all. See the doc on this override.
	if (id.arg != 0) {
		rainfall_.reseed(id.arg);
		starfield_.reseed(id.arg);
	}
	// `Screensaver::timerEvent`'s `if (!active_)` arm, `hid/display/screensaver.cpp`, which reads the mode as it
	// activates.
	mode_ = bind::read_device<ScreensaverMode>(device_, project::device_path::kScreensaverMode);
	switch (mode_) {
	case ScreensaverMode::STARSCAPE:
		starfield_.scatter();
		break;
	case ScreensaverMode::DELUGE:
		rainfall_.scatter();
		break;
	default:
		break;
	}
	return true;
}

void ScreensaverScene::tick(uint32_t dt_ms) {
	// `Screensaver::timerEvent` reads the mode on every frame; `render_oled` and `next_timer` answer from this read.
	mode_ = bind::read_device<ScreensaverMode>(device_, project::device_path::kScreensaverMode);
	if (!animated(mode_)) {
		// `BLANK` while already showing: "nothing changed, this tick is only an inhibitor
		// re-check" (`Screensaver::timerEvent` in `hid/display/screensaver.cpp`) -- and the inhibitor re-check has no
		// port, so there is nothing left to do. `OFF` falls here too.
		return;
	}
	since_frame_ms_ += dt_ms;
	if (since_frame_ms_ < kFrameIntervalMs) {
		return;
	}
	// Reset, not subtract: legacy re-arms its 50 ms timer (`Screensaver::timerEvent` in `hid/display/screensaver.cpp`)
	// from the moment the timer fired, so a tick that arrives late advances one frame, never several.
	since_frame_ms_ = 0;
	// `Screensaver::timerEvent`'s `else` arm, (`hid/display/screensaver.cpp`).
	switch (mode_) {
	case ScreensaverMode::STARSCAPE:
		starfield_.advance();
		break;
	case ScreensaverMode::DELUGE:
		rainfall_.advance();
		break;
	default:
		break;
	}
}

void ScreensaverScene::render_oled(SurfaceRole role, deluge::ui::oled::OledFrame& frame) {
	if (role != SurfaceRole::Main) {
		// Legacy substitutes its canvas for `main` and for nothing else
		// (`OLED::sendMainImage` in `hid/display/oled.cpp`); `oled_transparent` reports the same split.
		return;
	}
	// `Screensaver::render`, `hid/display/screensaver.cpp`. The `default` arm is legacy's
	// own: "BLANK: a cleared canvas is the whole picture", and the shell has already
	// cleared it (`Shell::render_oled`).
	switch (mode_) {
	case ScreensaverMode::STARSCAPE:
		deluge::ui::oled::draw_starfield(frame, starfield_);
		break;
	case ScreensaverMode::DELUGE:
		deluge::ui::oled::draw_rainfall(frame, rainfall_);
		break;
	default:
		break;
	}
}

Result ScreensaverScene::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                const deluge::ui::shell::UiContext& /*ctx*/) {
	return Result::unhandled();
}

std::optional<uint32_t> ScreensaverScene::next_timer() const {
	if (!animated(mode_)) {
		return std::nullopt;
	}
	// `kFrameIntervalMS`, less whatever this frame has already accumulated.
	return kFrameIntervalMs - (since_frame_ms_ < kFrameIntervalMs ? since_frame_ms_ : kFrameIntervalMs);
}

ScreensaverScene& screensaver_scene() {
	static ScreensaverScene scene;
	return scene;
}

} // namespace deluge::scenes::overlay
