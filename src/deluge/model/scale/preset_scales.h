#pragma once

#include "model/scale/note_set.h"

#include <array>
#include <cstdint>

/* This "defines" all preset scales in one place, so the various tables they inhabit
 * automatically stay in sync.
 *
 * NOTE: songs currently store scales as modeNotes, making them reasonably robust to
 * redefinitions, but the default scale is stored in the flash storage as an index.
 * See OFFSET_6|5_NOTE_SCALE for how to deal with that.
 *
 * The actual definitions expand this macro, picking the thing they need:
 * - enum PresetScale
 * - presetScaleNames
 * - presetScaleNotes
 *
 * Use by defining DEF(id, name, notes) before invoking DEF_SCALES() and undefining it
 * after.
 *
 * DEF_NOTES keeps the notes array intact on the trip through the preprocessor, since
 * cpp doesn't understand arrays, seeing { and } as a normal tokens.
 */
#define DEF_NOTES(...) NoteSet({__VA_ARGS__})

// Note: Value of note intervals taken from here: https://www.apassion4jazz.net/scales2.html
#define DEF_SCALES()                                                                                                   \
	/* ============================== 7-note scales ============================== */                                  \
	/* ------------- ORIGINAL DELUGE SCALES: modes of the major scale ------------ */                                  \
	/* MAJO Major (Ionian) */                                                                                          \
	DEF(MAJOR_SCALE, "MAJOR", DEF_NOTES(0, 2, 4, 5, 7, 9, 11))                                                         \
	/* MINO Natural Minor (Aeolian) */                                                                                 \
	DEF(MINOR_SCALE, "MINOR", DEF_NOTES(0, 2, 3, 5, 7, 8, 10))                                                         \
	/* DORI Dorian (minor with raised 6th) */                                                                          \
	DEF(DORIAN_SCALE, "DORIAN", DEF_NOTES(0, 2, 3, 5, 7, 9, 10))                                                       \
	/* PHRY Phrygian (minor with flattened 2nd) */                                                                     \
	DEF(PHRYGIAN_SCALE, "PHRYGIAN", DEF_NOTES(0, 1, 3, 5, 7, 8, 10))                                                   \
	/* LYDI Lydian (major with raised 4th) */                                                                          \
	DEF(LYDIAN_SCALE, "LYDIAN", DEF_NOTES(0, 2, 4, 6, 7, 9, 11))                                                       \
	/* MIXO Mixolydian (major with flattened 7th) */                                                                   \
	DEF(MIXOLYDIAN_SCALE, "MIXOLYDIAN", DEF_NOTES(0, 2, 4, 5, 7, 9, 10))                                               \
	/* LOCR Locrian (minor with flattened 2nd and 5th) */                                                              \
	DEF(LOCRIAN_SCALE, "LOCRIAN", DEF_NOTES(0, 1, 3, 5, 6, 8, 10))                                                     \
	/* ------------- NEW SCALES START HERE --------------------------------------- */                                  \
	/* MELO Melodic Minor (Ascending) (matches Launchpad scale) */                                                     \
	DEF(MELODIC_MINOR_SCALE, "MELODIC MINOR", DEF_NOTES(0, 2, 3, 5, 7, 9, 11))                                         \
	/* HARM Harmonic Minor (matches Launchpad and Lumi scale) */                                                       \
	DEF(HARMONIC_MINOR_SCALE, "HARMONIC MINOR", DEF_NOTES(0, 2, 3, 5, 7, 8, 11))                                       \
	/* Exotic scales */                                                                                                \
	/* HUNG Hungarian Minor (matches Launchpad scale) */                                                               \
	DEF(HUNGARIAN_MINOR_SCALE, "HUNGARIAN MINOR", DEF_NOTES(0, 2, 3, 6, 7, 8, 11))                                     \
	/* MARV Marva (matches Launchpad scale) */                                                                         \
	DEF(MARVA_SCALE, "MARVA", DEF_NOTES(0, 1, 4, 6, 7, 9, 11))                                                         \
	/* ARAB Arabian (matches Lumi's ARABIC_B scale) */                                                                 \
	DEF(ARABIAN_SCALE, "ARABIAN", DEF_NOTES(0, 2, 4, 5, 6, 8, 10))                                                     \
	/* ============================== 6-note scales ============================== */                                  \
	/* WHOL Whole Tone (matches Launchpad and Lumi scale) */                                                           \
	DEF(WHOLE_TONE_SCALE, "WHOLE TONE", DEF_NOTES(0, 2, 4, 6, 8, 10, 0))                                               \
	/* BLUE Blues Minor (matches Launchpad and Lumi BLUES scale) */                                                    \
	DEF(BLUES_SCALE, "BLUES", DEF_NOTES(0, 3, 5, 6, 7, 10, 0))                                                         \
	/* ============================== 5-note scales ============================== */                                  \
	/* PENT Pentatonic Minor (matches Launchpad and Lumi scale) */                                                     \
	DEF(PENTATONIC_MINOR_SCALE, "PENTANOTIC MINOR", DEF_NOTES(0, 3, 5, 7, 10))                                         \
	/* HIRA Hirajoshi (matches Launchpad scale) */                                                                     \
	DEF(HIRAJOSHI_SCALE, "HIRAJOSHI", DEF_NOTES(0, 2, 3, 7, 8))

/** Indexes into presetScaleNames and presetScaleNotes -arrays, and total
 *  number of preset scales.
 */
enum Scale {
#define DEF(id, name, notes) id,
	DEF_SCALES()
#undef DEF
	    NUM_PRESET_SCALES,
	USER_SCALE = NUM_PRESET_SCALES,
	NO_SCALE = 255
};

#define FIRST_6_NOTE_SCALE_INDEX WHOLE_TONE_SCALE
#define FIRST_5_NOTE_SCALE_INDEX PENTATONIC_MINOR_SCALE

extern const NoteSet presetScaleNotes[NUM_PRESET_SCALES];
extern std::array<char const*, NUM_PRESET_SCALES> presetScaleNames;

// These are scale ids / indexes as stored in flash memory, not for the presetScales*
// arrays!
#define OFFICIAL_FIRMWARE_RANDOM_SCALE_INDEX 7
#define OFFICIAL_FIRMWARE_NONE_SCALE_INDEX 8
#define PRESET_SCALE_RANDOM 254
#define PRESET_SCALE_NONE 255
// These offsets allow us to introduce new 7, 6 and 5 note scales in between the existing
// ones keeping the decreasing order and without breaking backwards compatibility for
// defaults stored in flash memory.
#define OFFSET_6_NOTE_SCALE 64
#define OFFSET_5_NOTE_SCALE 128

const char* getScaleName(Scale scale);

Scale getScale(NoteSet notes);

bool isUserScale(NoteSet notes);
