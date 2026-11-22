#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/shell_host/shell_host.h"
#include "gui/ui/ui.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/output.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"

namespace deluge::gui::shell_host {
namespace {

/// The clip facts, answered from the running app: each clip is found by its id in the current song.
class HostClipFacts final : public scenes::bind::ClipFacts {
public:
	[[nodiscard]] std::optional<scenes::bind::AudioClipFact> audio_clip(project::ClipAddress clip) const override {
		::AudioClip* audio = audio_clip_at(clip);
		if (audio == nullptr || audio->output == nullptr || !audio->output->id()) {
			return std::nullopt;
		}
		return scenes::bind::AudioClipFact{
		    .track_id = *audio->output->id(),
		    .max_zoom = audio->getMaxZoom(),
		    .unarmed = audio->armState == ArmState::OFF,
		    .reversed = audio->sampleControls.isCurrentlyReversed(),
		    .session_index = currentSong->live.clips.session_clips.getIndexForClip(audio),
		};
	}
	[[nodiscard]] bool clip_playing(project::ClipAddress clip) const override {
		::AudioClip* audio = audio_clip_at(clip);
		return audio != nullptr && playbackHandler.playbackState != 0u && currentSong->isClipActive(audio)
		       && playbackHandler.ticksLeftInCountIn == 0u;
	}
	[[nodiscard]] int32_t clip_live_pos(project::ClipAddress clip) const override {
		::AudioClip* audio = audio_clip_at(clip);
		return audio != nullptr ? static_cast<int32_t>(audio->getLivePos()) : 0;
	}
	[[nodiscard]] bool clip_arrangement_only(project::ClipAddress clip) const override {
		const ::Clip* found = currentSong != nullptr ? currentSong->clipForId(clip.clip_id) : nullptr;
		return found != nullptr && found->isArrangementOnlyClip();
	}
	[[nodiscard]] bool clip_recording_linearly(project::ClipAddress clip) const override {
		::AudioClip* audio = audio_clip_at(clip);
		return audio != nullptr && audio->getCurrentlyRecordingLinearly();
	}
	[[nodiscard]] std::pair<int64_t, int64_t> audio_clip_samples(project::ClipAddress clip, int32_t scroll,
	                                                             int32_t zoom) const override {
		::AudioClip* audio = audio_clip_at(clip);
		if (audio == nullptr) {
			return {0, 0};
		}
		int64_t scroll_samples = 0;
		int64_t zoom_samples = 0;
		audio->getScrollAndZoomInSamples(scroll, zoom, &scroll_samples, &zoom_samples);
		return {scroll_samples, zoom_samples};
	}
	[[nodiscard]] std::optional<scenes::bind::IsomorphicKeyboard> isomorphic_keyboard() const override {
		InstrumentClip* clip = getCurrentInstrumentClip();
		if (getRootUI() != &keyboardScreen || clip == nullptr
		    || clip->keyboardState.currentLayout != KeyboardLayoutType::KeyboardLayoutTypeIsomorphic) {
			return std::nullopt;
		}
		return scenes::bind::IsomorphicKeyboard{.scroll_offset = clip->keyboardState.isomorphic.scrollOffset,
		                                        .row_interval = clip->keyboardState.isomorphic.rowInterval};
	}
	[[nodiscard]] std::optional<scenes::bind::ClipNavigation> clip_navigation() const override {
		if (currentSong == nullptr) {
			return std::nullopt;
		}
		const PersistentViewState& view = currentSong->persistentView;
		return scenes::bind::ClipNavigation{.x_scroll = view.xScroll[NAVIGATION_CLIP],
		                                    .x_zoom = view.xZoom[NAVIGATION_CLIP],
		                                    .triplets_on = view.tripletsOn,
		                                    .triplets_level = view.tripletsLevel};
	}
};

} // namespace

::AudioClip* audio_clip_at(project::ClipAddress clip) {
	::Clip* found = currentSong != nullptr ? currentSong->clipForId(clip.clip_id) : nullptr;
	return found != nullptr && found->type == ClipType::AUDIO ? static_cast<::AudioClip*>(found) : nullptr;
}

const scenes::bind::ClipFacts& clip_facts() {
	static const HostClipFacts facts;
	return facts;
}

} // namespace deluge::gui::shell_host
