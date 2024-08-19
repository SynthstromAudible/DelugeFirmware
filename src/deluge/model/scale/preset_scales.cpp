#include "model/scale/preset_scales.h"
#include "model/scale/note_set.h"

std::array<char const*, NUM_SCALELIKE> scalelikeNames = {
#define DEF(id, name, notes) name,
    DEF_SCALES()
#undef DEF
    // clang-format off
    "USER",
    "RANDOM",
    "NONE"
    // clang-format on
};

const NoteSet presetScaleNotes[NUM_PRESET_SCALES] = {
#define DEF(id, name, notes) notes,
    DEF_SCALES()
#undef DEF
};

const char* getScaleName(Scale scale) {
	if (scale < NUM_SCALELIKE) {
		return scalelikeNames[scale];
	}
	else {
		return "ERR";
	}
}

Scale getScale(NoteSet notes) {
	for (int32_t p = 0; p < NUM_PRESET_SCALES; p++) {
		if (notes == presetScaleNotes[p]) {
			return static_cast<Scale>(p);
		}
	}
	return USER_SCALE;
}

bool isUserScale(NoteSet notes) {
	return USER_SCALE == getScale(notes);
}

void ensureNotAllPresetScalesDisabled(std::bitset<NUM_PRESET_SCALES>& disabled) {
	for (uint8_t i = 0; i < NUM_PRESET_SCALES; i++) {
		if (!disabled[i]) {
			return;
		}
	}
	// All disabled. Enable the major scale.
	disabled[MAJOR_SCALE] = false;
}

// These offsets allow us to introduce new 7, 6 and 5 note scales in between the existing
// ones keeping the decreasing order and without breaking backwards compatibility for
// defaults stored in flash memory.
#define FLASH_CODE_OFFSET_6_NOTE_SCALE 64
#define FLASH_CODE_OFFSET_5_NOTE_SCALE 128
#define FLASH_CODE_USER_SCALE 253
#define FLASH_CODE_RANDOM_SCALE 254
#define FLASH_CODE_NO_SCALE 255

Scale flashStorageCodeToScale(uint8_t code) {
	if (code < FLASH_CODE_OFFSET_6_NOTE_SCALE) {
		return static_cast<Scale>(code);
	}
	if (code < FLASH_CODE_OFFSET_5_NOTE_SCALE) {
		return static_cast<Scale>(FIRST_6_NOTE_SCALE_INDEX + code - FLASH_CODE_OFFSET_6_NOTE_SCALE);
	}
	if (code < FLASH_CODE_USER_SCALE) {
		return static_cast<Scale>(FIRST_5_NOTE_SCALE_INDEX + code - FLASH_CODE_OFFSET_5_NOTE_SCALE);
	}
	if (code == FLASH_CODE_USER_SCALE) {
		return USER_SCALE;
	}
	if (code == FLASH_CODE_RANDOM_SCALE) {
		return RANDOM_SCALE;
	}
	return NO_SCALE;
}

uint8_t scaleToFlashStorageCode(Scale scale) {
	if (scale < FIRST_6_NOTE_SCALE_INDEX) {
		return scale;
	}
	if (scale < FIRST_5_NOTE_SCALE_INDEX) {
		return scale - FIRST_6_NOTE_SCALE_INDEX + FLASH_CODE_OFFSET_6_NOTE_SCALE;
	}
	if (scale < USER_SCALE) {
		return scale - FIRST_5_NOTE_SCALE_INDEX + FLASH_CODE_OFFSET_5_NOTE_SCALE;
	}
	if (scale == USER_SCALE) {
		return FLASH_CODE_USER_SCALE;
	}
	if (scale == RANDOM_SCALE) {
		return FLASH_CODE_RANDOM_SCALE;
	}
	return FLASH_CODE_NO_SCALE;
}
