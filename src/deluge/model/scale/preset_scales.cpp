#include "model/scale/preset_scales.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "model/scale/note_set.h"
#include "util/cfunctions.h"
#include "util/d_string.h"

std::array<char const*, NUM_SCALELIKE> scalelikeNames = {
#define DEF(id, name, notes, mode) name,
    DEF_SCALES()
#undef DEF
    // clang-format off
    "USER",
    "RANDOM",
    "NONE"
    // clang-format on
};

const NoteSet presetScaleNotes[NUM_PRESET_SCALES] = {
#define DEF(id, name, notes, mode) notes,
    DEF_SCALES()
#undef DEF
};

const uint8_t presetScaleModes[NUM_PRESET_SCALES] = {
#define DEF(id, name, notes, mode) mode,
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

const uint8_t getScaleMode(Scale scale) {
	if (scale < NUM_SCALELIKE) {
		return presetScaleModes[scale];
	}
	else {
		return 1;
	}
}

/* Calculate relative major key accidental preference */
//                                    C    Db   D    Eb   E    F    F#   G    Ab   A    Bb   B
const uint8_t majorAccidental[12] = {129, 129, '#', 129, '#', 129, '#', '#', 129, '#', 129, '#'};
const uint8_t noteLetter[12] = {'C', 'C', 'D', 'D', 'E', 'F', 'F', 'G', 'G', 'A', 'A', 'B'};
const uint8_t noteIsAltered[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
const uint8_t adjustScale2RelativeMajor[] = {0, 0, 2, 4, 5, 7, 9, 11};

const uint8_t getAccidental(int32_t rootNoteCode, Scale scale) {
	if (rootNoteCode >= 0 && scale < NUM_SCALELIKE) {
		int8_t mode = getScaleMode(scale);
		rootNoteCode -= adjustScale2RelativeMajor[mode];
		if (rootNoteCode < 0) {
			rootNoteCode += 12;
		}
		int32_t majorRoot = rootNoteCode % 12;
		return majorAccidental[majorRoot];
	}
	else {
		return '.';
	}
}

void noteCodeToString(int32_t noteCode, char* buffer,
                      bool appendOctaveNo,  // defaults to true
                      int32_t rootNoteCode, // defaults to -1, becomes noteCode
                      Scale scale) {        // defaults to MAJOR
	char* thisChar = buffer;
	int32_t octave = (noteCode) / 12 - 2;
	int32_t n = (uint16_t)(noteCode + 120) % (uint8_t)12;
	if (rootNoteCode == -1) {
		rootNoteCode = noteCode;
	}
	uint8_t accidental = getAccidental(rootNoteCode, scale);
	if (noteIsAltered[n]) { // actually: if code is a black key on the piano?
		if ((accidental == '#')) {
			*thisChar++ = noteLetter[n];
			*thisChar = accidental;
		}
		else {
			*thisChar++ = noteLetter[n + 1];
			*thisChar = accidental;
		}
	}
	else {
		*thisChar = noteLetter[n];
	}
	thisChar++;
	if (appendOctaveNo) {
		intToString(octave, thisChar, 1);
	}
	else {
		*thisChar = 0;
	}
	//	D_PRINTLN(",noteCodeToString,%s", buffer);
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
