#include "utils.h"
#include "gui/ui/sound_editor.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "processing/engines/audio_engine.h"

const deluge::effects::SidechainState& sidechainSettings(bool forReverb) {
	if (forReverb) {
		return AudioEngine::reverbSidechainSettings;
	}
	return soundEditor.currentModControllable->effects().sidechain;
}
