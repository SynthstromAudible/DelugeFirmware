#include "arpeggiator.h"
#include "gui/ui/sound_editor.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"
#include "model/clip/instrument_clip.h"


namespace menu_item::submenu {

void Arpeggiator::beginSession(MenuItem* navigatedBackwardFrom) {

	soundEditor.currentArpSettings = soundEditor.editingKit()
	                                     ? &((SoundDrum*)soundEditor.currentSound)->arpSettings
	                                     : &((InstrumentClip*)currentSong->currentClip)->arpSettings;
	Submenu::beginSession(navigatedBackwardFrom);
}

}
