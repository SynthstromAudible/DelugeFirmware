#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/shell_host/shell_host.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "model/clip/clip.h"
#include "model/song/song.h"

namespace deluge::gui::shell_host {
namespace {

/// The root view's actions, each as the legacy opener it names performs it.
class HostViewActions final : public scenes::bind::ViewActions {
public:
	void end_audition(scenes::bind::AuditionEnd how) override {
		using scenes::bind::AuditionEnd;
		switch (how) {
		case AuditionEnd::ClipViewWhenAuditioning:
			if (currentUIMode == UI_MODE_AUDITIONING) {
				instrumentClipView.cancelAllAuditioning();
			}
			break;
		case AuditionEnd::ClipViewForInstrumentClip:
			if (::Clip* clip = getCurrentClip(); clip != nullptr && clip->type == ClipType::INSTRUMENT) {
				instrumentClipView.cancelAllAuditioning();
			}
			break;
		case AuditionEnd::KeyboardWhenAuditioning:
			if (getRootUI() == &keyboardScreen && currentUIMode == UI_MODE_AUDITIONING) {
				keyboardScreen.exitAuditionMode();
			}
			break;
		}
	}
};

} // namespace

scenes::bind::ViewActions& view_actions() {
	// Constant-initialised, so it links as `.data` into SDRAM with the host's other port objects (`sdram_sections.x`).
	static constinit HostViewActions host;
	return host;
}

} // namespace deluge::gui::shell_host
