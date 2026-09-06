#include "shell/boot_scenes.h"

#include "ui/oled/popup.h"

namespace deluge::ui::shell {

bool LoadingScene::on_enter(const SceneId&) {
	// Zero re-arms: `period_ms` doubles as "armed" for `ui::oled::working` (see `Blink`'s own
	// doc), so resetting it here restarts the animation from its first frame on every entry
	// rather than continuing wherever a previous loading run's clock left it.
	blink_.period_ms = 0;
	return true;
}

void LoadingScene::render_oled(SurfaceRole role, ui::oled::OledFrame& frame) {
	if (role != SurfaceRole::Main) {
		return;
	}
	ui::oled::working(frame, frame.bounds(), "Loading", blink_);
}

} // namespace deluge::ui::shell
