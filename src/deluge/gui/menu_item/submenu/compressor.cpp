#include "compressor.h"
#include "gui/ui/sound_editor.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"

namespace menu_item::submenu {
void Compressor::beginSession(MenuItem* navigatedBackwardFrom) {
	soundEditor.currentCompressor =
	    forReverbCompressor ? &AudioEngine::reverbCompressor : &soundEditor.currentSound->compressor;
	Submenu::beginSession(navigatedBackwardFrom);
}
}
