#include "gui/ui/root_ui.h"
#include "gui/ui/shell_host/shell_host.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/automation_view.h"
#include "gui/views/view.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip.h"
#include "model/clip/clip_minder.h"
#include "util/menu_entry.h"
#include "util/sound_menu_open.h"

namespace deluge::gui::shell_host {
namespace {

/// The other root views and the menu, each reached as the audio clip view's own arm reaches it.
static_assert(deluge::ui::scenes::bind::kOverviewColumns == kDisplayWidth
              && deluge::ui::scenes::bind::kOverviewRows == kDisplayHeight);

class HostRootViews final : public deluge::ui::scenes::bind::RootViews {
public:
	void to_session_or_arranger() override {
		// `AudioClipView::buttonAction`'s SESSION short-press arm: its timer goes before the transition starts.
		uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
		if (::RootUI* root = getRootUI(); root != nullptr) {
			if (ClipMinder* minder = root->toClipMinder(); minder != nullptr) {
				minder->transitionToArrangerOrSession();
			}
		}
	}

	void to_automation() override { changeRootUI(&automationView); }

	[[nodiscard]] bool menu_shortcut_pad(uint8_t x, uint8_t y, bool on) override {
		return deluge::gui::ui::menuShortcutPadAction(x, y, on) != ActionResult::NOT_DEALT_WITH;
	}

	void next_audio_output(int8_t direction) override {
		::Clip* clip = getCurrentClip();
		if (clip != nullptr && clip->type == ClipType::AUDIO) {
			view.navigateThroughAudioOutputsForAudioClip(direction, static_cast<AudioClip*>(clip));
		}
	}

	[[nodiscard]] bool open_clip_menu() override {
		// Site 5 of `gui/ui/menu_entry.cpp`'s table: an audio clip under a clip-minder root.
		return deluge::gui::ui::openMenu(getCurrentClip(), /*sourceIndex=*/0, /*performanceView=*/false,
		                                 /*sessionOrArrangerRoot=*/false, /*sessionViewRoot=*/false,
		                                 /*kitGlobalFx=*/false);
	}

	void clip_shortcut_overview(deluge::ui::scenes::bind::OverviewPads& pads) override {
		RGB colours[kDisplayHeight][kDisplayWidth]{};
		deluge::gui::ui::clipShortcutsOverviewColours(colours);
		for (size_t y = 0; y < pads.size(); ++y) {
			for (size_t x = 0; x < pads[y].size(); ++x) {
				const RGB c = colours[y][x];
				pads[y][x] = {.r = c.r, .g = c.g, .b = c.b};
			}
		}
	}
};

} // namespace

deluge::ui::scenes::bind::RootViews& root_views() {
	// Constant-initialised, so it links as `.data` into SDRAM with the host's other port objects (`sdram_sections.x`).
	static constinit HostRootViews host;
	return host;
}

} // namespace deluge::gui::shell_host
