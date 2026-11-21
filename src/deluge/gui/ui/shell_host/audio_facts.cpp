#include "definitions_cxx.hpp"
#include "gui/ui/shell_host/shell_host.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/recording/foreground_recording.h"

#include <utility>

namespace deluge::gui::shell_host {
namespace {

// The menu's threshold choices are the legacy mode's values, in order.
static_assert(std::to_underlying(ThresholdRecordingMode::OFF) == 0
              && std::to_underlying(ThresholdRecordingMode::LOW) == 1
              && std::to_underlying(ThresholdRecordingMode::MEDIUM) == 2
              && std::to_underlying(ThresholdRecordingMode::HIGH) == 3);

// Every internal source follows the external ones.
static_assert(AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION == AudioInputChannel::MIX);

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
	[[nodiscard]] bool audio_busy() const override { return AudioEngine::audioBusy(); }
	[[nodiscard]] scenes::bind::ForegroundRecording foreground_recording() const override {
		using scenes::bind::ForegroundRecording;
		switch (AudioEngine::foregroundRecordingSource()) {
		case AudioInputChannel::UNSET:
			return ForegroundRecording::UNSET;
		case AudioInputChannel::NONE:
			return ForegroundRecording::NONE;
		case AudioInputChannel::LEFT:
		case AudioInputChannel::RIGHT:
		case AudioInputChannel::STEREO:
		case AudioInputChannel::BALANCED:
		case AudioInputChannel::USB:
			return ForegroundRecording::EXTERNAL;
		case AudioInputChannel::MIX:
		case AudioInputChannel::OUTPUT:
		case AudioInputChannel::SPECIFIC_OUTPUT:
		case AudioInputChannel::OFFLINE_OUTPUT:
			return ForegroundRecording::INTERNAL;
		}
		return ForegroundRecording::UNSET;
	}
	[[nodiscard]] bool resampling() const override { return deluge::recording::isCurrentlyResampling(); }
};

} // namespace

const scenes::bind::AudioFacts& audio_facts() {
	static const HostAudioFacts facts;
	return facts;
}

} // namespace deluge::gui::shell_host
