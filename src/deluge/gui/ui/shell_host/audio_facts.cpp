#include "definitions_cxx.hpp"
#include "gui/ui/shell_host/shell_host.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"

#include <utility>

namespace deluge::gui::shell_host {
namespace {

// The menu's threshold choices are the legacy mode's values, in order.
static_assert(std::to_underlying(ThresholdRecordingMode::OFF) == 0
              && std::to_underlying(ThresholdRecordingMode::LOW) == 1
              && std::to_underlying(ThresholdRecordingMode::MEDIUM) == 2
              && std::to_underlying(ThresholdRecordingMode::HIGH) == 3);

class HostAudioFacts final : public scenes::bind::AudioFacts {
public:
	[[nodiscard]] bool audio_inputs_present() const override {
		return AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn;
	}
	[[nodiscard]] int32_t live_voice_count() const override {
		return AudioEngine::getNumVoices() + (currentSong != nullptr ? currentSong->countAudioVoices() : 0);
	}
	[[nodiscard]] bool line_in_plugged() const override { return AudioEngine::lineInPluggedIn; }
	[[nodiscard]] int32_t song_threshold_recording() const override {
		return currentSong != nullptr ? static_cast<int32_t>(currentSong->thresholdRecordingMode) : 0;
	}
	[[nodiscard]] plugin::SyncType reverb_sidechain_sync_type() const override {
		return AudioEngine::reverbSidechainSettings.sync_type;
	}
};

} // namespace

const scenes::bind::AudioFacts& audio_facts() {
	static const HostAudioFacts facts;
	return facts;
}

} // namespace deluge::gui::shell_host
