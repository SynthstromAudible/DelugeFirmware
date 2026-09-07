/// @file
/// @brief `ScreensaverScene`. See `screensaver_scene.h`.

#include "scenes/overlay/screensaver_scene.h"

#include "storage/flash_storage.h"

namespace deluge::scenes::overlay {

using deluge::ui::shell::Result;
using deluge::ui::shell::SurfaceRole;

bool ScreensaverScene::animated(::ScreensaverMode mode) {
	// `hid/display/screensaver.cpp:59-62`, expression for expression.
	return mode == ::ScreensaverMode::STARSCAPE || mode == ::ScreensaverMode::DELUGE;
}

::ScreensaverMode ScreensaverScene::mode() const {
	// The one global legacy reads, read the same way legacy reads it -- live, per question
	// (`hid/display/screensaver.cpp:103,128,141,163,213`). See the class doc.
	return FlashStorage::screensaverMode;
}

bool ScreensaverScene::on_enter(const deluge::ui::shell::SceneId& id) {
	since_frame_ms_ = 0;
	// The seed, and only when there is one: zero is `ShellConfig::seed`'s shipped default and
	// legacy has no seed at all. See the doc on this override.
	if (id.arg != 0) {
		rainfall_.reseed(id.arg);
		starfield_.reseed(id.arg);
	}
	// `Screensaver::timerEvent`'s `if (!active_)` arm, `hid/display/screensaver.cpp:126-138`.
	switch (mode()) {
	case ::ScreensaverMode::STARSCAPE:
		starfield_.scatter();
		break;
	case ::ScreensaverMode::DELUGE:
		rainfall_.scatter();
		break;
	default:
		break;
	}
	return true;
}

void ScreensaverScene::tick(uint32_t dt_ms) {
	const ::ScreensaverMode m = mode();
	if (!animated(m)) {
		// `BLANK` while already showing: "nothing changed, this tick is only an inhibitor
		// re-check" (`hid/display/screensaver.cpp:150-153`) -- and the inhibitor re-check has no
		// port, so there is nothing left to do. `OFF` falls here too.
		return;
	}
	since_frame_ms_ += dt_ms;
	if (since_frame_ms_ < kFrameIntervalMs) {
		return;
	}
	// Reset, not subtract: legacy re-arms its 50 ms timer at `:161-162` from the moment the timer
	// fired, so a tick that arrives late advances one frame, never several.
	since_frame_ms_ = 0;
	// `Screensaver::timerEvent`'s `else` arm, `hid/display/screensaver.cpp:141-154`.
	switch (m) {
	case ::ScreensaverMode::STARSCAPE:
		starfield_.advance();
		break;
	case ::ScreensaverMode::DELUGE:
		rainfall_.advance();
		break;
	default:
		break;
	}
}

void ScreensaverScene::render_oled(SurfaceRole role, deluge::ui::oled::OledFrame& frame) {
	if (role != SurfaceRole::Main) {
		// Legacy substitutes its canvas for `main` and for nothing else
		// (`hid/display/oled.cpp:342-348`); `oled_transparent` reports the same split.
		return;
	}
	// `Screensaver::render`, `hid/display/screensaver.cpp:213-222`. The `default` arm is legacy's
	// own: "BLANK: a cleared canvas is the whole picture" (`:188`), and the shell has already
	// cleared it (`shell.cpp:642`).
	switch (mode()) {
	case ::ScreensaverMode::STARSCAPE:
		deluge::ui::oled::draw_starfield(frame, starfield_);
		break;
	case ::ScreensaverMode::DELUGE:
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
	if (!animated(mode())) {
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
