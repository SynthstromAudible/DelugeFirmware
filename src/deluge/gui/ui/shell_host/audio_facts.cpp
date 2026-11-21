#include "gui/ui/shell_host/shell_host.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"

namespace deluge::gui::shell_host {
namespace {

class HostAudioFacts final : public scenes::bind::AudioFacts {
public:
	[[nodiscard]] bool audio_inputs_present() const override {
		return AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn;
	}
	[[nodiscard]] int32_t live_voice_count() const override {
		return AudioEngine::getNumVoices() + (currentSong != nullptr ? currentSong->countAudioVoices() : 0);
	}
};

} // namespace

const scenes::bind::AudioFacts& audio_facts() {
	static const HostAudioFacts facts;
	return facts;
}

} // namespace deluge::gui::shell_host
