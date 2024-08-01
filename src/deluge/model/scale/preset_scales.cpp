#include "model/scale/preset_scales.h"
#include "model/scale/note_set.h"

std::array<char const*, NUM_PRESET_SCALES> presetScaleNames = {
#define DEF(id, name, notes) name,
    DEF_SCALES()
#undef DEF
};

const NoteSet presetScaleNotes[NUM_PRESET_SCALES] = {
#define DEF(id, name, notes) notes,
    DEF_SCALES()
#undef DEF
};
