#include "shell/boot_scenes.h"

#include "gfx/oled/geometry.h"
#include "ui/oled/popup.h"
#include "ui/oled/text.h"

#include <algorithm>
#include <cstring>

namespace deluge::ui::shell {

namespace {

/// @brief Copy \p src into \p dst, truncating to fit and always NUL-terminating.
/// @param dst      Destination buffer.
/// @param dst_size Total bytes in \p dst, NUL included.
/// @param src      Source text.
void copy_bounded(char* dst, size_t dst_size, std::string_view src) {
	const size_t n = std::min(src.size(), dst_size - 1);
	std::memcpy(dst, src.data(), n);
	dst[n] = '\0';
}

} // namespace

void BootScene::set_firmware_version(std::string_view version) {
	copy_bounded(version_, sizeof(version_), version);
}

void BootScene::render_oled(SurfaceRole, ui::oled::OledFrame& frame) {
	ui::oled::title(frame, "DELUGE");
	const gfx::oled::Rect b = frame.bounds();
	const int32_t h = ui::oled::text_height_for(frame.style.row_font);
	// Right under the title's own separator rule -- `Style::title_height` is documented as
	// exactly that row.
	ui::oled::text(frame, {0, frame.style.title_height + 2, b.w, h}, std::string_view{version_},
	               ui::oled::TextStyle{.font = frame.style.row_font, .align = ui::oled::Align::Centre});
}

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
