#include "gui/ui/shell_host/shell_host.h"
#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "model/clip/clip.h"
#include "model/song/song.h"
#include "playback/mode/session.h"

namespace deluge::gui::shell_host {
namespace {

using deluge::ui::scenes::bind::MacroFact;
using deluge::ui::scenes::bind::MacroKind;
using deluge::ui::scenes::bind::MacroPress;

/// The macro in slot @p y of the current song, or none past the eight.
const deluge::project::SessionMacro* slot(uint8_t y) {
	if (currentSong == nullptr || y >= deluge::ui::scenes::bind::SessionMacros::kSlots) {
		return nullptr;
	}
	return &currentSong->project.arrangement.session_macros[y];
}

/// The clip a macro names, or none the song holds (`gui/views/view.cpp`'s `macroClip`).
::Clip* macro_clip(const deluge::project::SessionMacro& m) {
	return m.clip_id ? currentSong->clipForId(*m.clip_id) : nullptr;
}

/// The song's macros, read and run as `View::renderMacros` and `View::activateMacro` do.
class HostSessionMacros final : public deluge::ui::scenes::bind::SessionMacros {
public:
	[[nodiscard]] MacroFact macro(uint8_t y) const override {
		const deluge::project::SessionMacro* m = slot(y);
		if (m == nullptr) {
			return {};
		}
		using deluge::project::SessionMacroKind;
		switch (m->kind) {
		case SessionMacroKind::CLIP_LAUNCH: {
			const ::Clip* clip = macro_clip(*m);
			return {.kind = MacroKind::CLIP_LAUNCH,
			        .has_clip = clip != nullptr,
			        .clip_active = clip != nullptr && clip->activeIfNoSolo,
			        .clip_armed = clip != nullptr && clip->armState != ArmState::OFF};
		}
		case SessionMacroKind::OUTPUT_CYCLE:
			return {.kind = MacroKind::OUTPUT_CYCLE};
		case SessionMacroKind::SECTION:
			return {.kind = MacroKind::SECTION};
		case SessionMacroKind::NO_MACRO:
		case SessionMacroKind::NUM_KINDS:
			break;
		}
		return {};
	}

	void activate(uint8_t y, MacroPress press) override {
		// `View::activateMacro`, with the two buttons it reads given by the press.
		const deluge::project::SessionMacro* m = slot(y);
		if (m == nullptr) {
			return;
		}
		using deluge::project::SessionMacroKind;
		switch (m->kind) {
		case SessionMacroKind::CLIP_LAUNCH: {
			::Clip* clip = macro_clip(*m);
			if (clip == nullptr) {
				break;
			}
			if (press.affect_entire) {
				if (getCurrentClip() != clip) {
					sessionView.transitionToViewForClip(clip);
				}
			}
			else {
				session.toggleClipStatus(clip, nullptr, press.shift, kInternalButtonPressLatency);
			}
			break;
		}
		case SessionMacroKind::OUTPUT_CYCLE: {
			::Output* output = m->output_id ? currentSong->outputForId(*m->output_id) : nullptr;
			if (::Clip* next = view.findNextClipForOutput(output); next != nullptr) {
				session.toggleClipStatus(next, nullptr, press.shift, kInternalButtonPressLatency);
			}
			break;
		}
		case SessionMacroKind::SECTION:
			session.armSection(m->section, kInternalButtonPressLatency);
			break;
		case SessionMacroKind::NO_MACRO:
		case SessionMacroKind::NUM_KINDS:
			break;
		}
	}
};

} // namespace

deluge::ui::scenes::bind::SessionMacros& session_macros() {
	// Constant-initialised, so it links as `.data` into SDRAM with the host's other port objects (`sdram_sections.x`).
	static constinit HostSessionMacros host;
	return host;
}

} // namespace deluge::gui::shell_host
