#include "gui/context_menu/clear_song.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/root_ui.h"
#include "gui/ui/save/save_song_ui.h"
#include "gui/ui/shell_host/shell_host.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/automation_view.h"
#include "gui/views/view.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip.h"
#include "model/clip/clip_minder.h"
#include "ui/scenes/audio_clip/audio_clip_scene.h"
#include "util/menu_entry.h"
#include "util/sound_menu_open.h"

#include <utility>

namespace deluge::gui::shell_host {
namespace {

/// The other root views and the menu, each reached as the audio clip view's own arm reaches it.
static_assert(deluge::ui::scenes::bind::kOverviewColumns == kDisplayWidth
              && deluge::ui::scenes::bind::kOverviewRows == kDisplayHeight);

class HostRootViews final : public deluge::ui::scenes::bind::RootViews {
public:
	void to_session_or_arranger() override { pending = Open::SESSION_OR_ARRANGER; }
	void to_automation() override { pending = Open::AUTOMATION; }

	[[nodiscard]] bool menu_shortcut_pad(uint8_t x, uint8_t y, bool on) override {
		return deluge::gui::ui::menuShortcutPadAction(x, y, on) != ActionResult::NOT_DEALT_WITH;
	}

	void next_audio_output(int8_t direction) override {
		::Clip* clip = getCurrentClip();
		if (clip != nullptr && clip->type == ClipType::AUDIO) {
			view.navigateThroughAudioOutputsForAudioClip(direction, static_cast<AudioClip*>(clip));
		}
	}

	void open_clip_menu() override { pending = Open::CLIP_MENU; }

	void open_song_saver() override { pending = Open::SAVER; }
	void open_song_loader() override { pending = Open::LOADER; }
	void open_clear_song() override { pending = Open::CLEAR_SONG; }

	/// The view or UI a press asked for, which `dispatchRootViewOpens` opens after the tick.
	enum class Open : uint8_t { NONE, SESSION_OR_ARRANGER, AUTOMATION, CLIP_MENU, SAVER, LOADER, CLEAR_SONG };
	Open pending = Open::NONE;

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

HostRootViews& host_root_views() {
	// Constant-initialised, so it links as `.data` into SDRAM with the host's other port objects (`sdram_sections.x`).
	static constinit HostRootViews host;
	return host;
}

} // namespace

deluge::ui::scenes::bind::RootViews& root_views() {
	return host_root_views();
}

void dispatchRootViewOpens() {
	HostRootViews& host = host_root_views();
	const HostRootViews::Open open = std::exchange(host.pending, HostRootViews::Open::NONE);
	switch (open) {
	case HostRootViews::Open::SESSION_OR_ARRANGER:
		// `AudioClipView::buttonAction`'s SESSION short-press arm: its timer goes before the transition starts.
		uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
		if (::RootUI* root = getRootUI(); root != nullptr) {
			if (ClipMinder* minder = root->toClipMinder(); minder != nullptr) {
				minder->transitionToArrangerOrSession();
			}
		}
		break;
	case HostRootViews::Open::AUTOMATION:
		changeRootUI(&automationView);
		break;
	case HostRootViews::Open::CLIP_MENU:
		// Site 5 of `gui/ui/menu_entry.cpp`'s table: an audio clip under a clip-minder root. The view drops its marker
		// once the menu is open, as `AudioClipView::buttonAction`'s SELECT arm does on `openMenu`'s success.
		if (deluge::gui::ui::openMenu(getCurrentClip(), /*sourceIndex=*/0, /*performanceView=*/false,
		                              /*sessionOrArrangerRoot=*/false, /*sessionViewRoot=*/false,
		                              /*kitGlobalFx=*/false)) {
			deluge::ui::scenes::audio_clip::audio_clip_scene().clip_menu_opened();
		}
		break;
	case HostRootViews::Open::SAVER:
		(void)openUI(&saveSongUI);
		break;
	case HostRootViews::Open::LOADER:
		(void)openUI(&loadSongUI);
		break;
	case HostRootViews::Open::CLEAR_SONG:
		if (deluge::gui::context_menu::clearSong.setupAndCheckAvailability()) {
			(void)openUI(&deluge::gui::context_menu::clearSong);
		}
		break;
	case HostRootViews::Open::NONE:
		break;
	}
}

} // namespace deluge::gui::shell_host
