#include "model/scale/preset_scales.h"

std::array<char const*, NUM_PRESET_SCALES> presetScaleNames = {
#define DEF(id, name, notes) name,
    DEF_SCALES()
#undef DEF
};

const uint8_t presetScaleNotes[NUM_PRESET_SCALES][7] = {
#define DEF(id, name, notes) notes,
    DEF_SCALES()
#undef DEF
};
