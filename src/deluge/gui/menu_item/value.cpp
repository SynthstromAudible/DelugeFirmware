#include "gui/menu_item/value.h"
#include "definitions_cxx.hpp"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "processing/sound/sound_drum.h"

template <typename T>

ModelStackWithThreeMainThings*
deluge::gui::menu_item::Value<T>::getModelStackFromSoundDrumForValue(void* memory, SoundDrum* soundDrum) {
	InstrumentClip* clip = getCurrentInstrumentClip();
	int32_t noteRowIndex;
	NoteRow* noteRow = clip->getNoteRowForDrum(soundDrum, &noteRowIndex);
	return setupModelStackWithThreeMainThingsIncludingNoteRow(memory, currentSong, getCurrentClip(), noteRowIndex,
	                                                          noteRow, soundDrum, &noteRow->paramManager);
}
