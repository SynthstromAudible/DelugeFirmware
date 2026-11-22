#include "shell/loading_scene.h"

#include "ui/widgets/oled/popup.h"

namespace deluge::ui::shell {

bool LoadingScene::on_enter(const SceneId&) {
	// Zero re-arms: `period_ms` doubles as "armed" for `ui::widgets::oled::working` (see `Blink`'s own
	// doc), so resetting it here restarts the animation from its first frame on every entry
	// rather than continuing wherever a previous loading run's clock left it.
	blink_.period_ms = 0;
	return true;
}

void LoadingScene::render_oled(SurfaceRole role, ui::widgets::oled::OledFrame& frame) {
	if (role != SurfaceRole::Main) {
		return;
	}
	ui::widgets::oled::working(frame, frame.bounds(), "Loading", blink_);
}

} // namespace deluge::ui::shell
