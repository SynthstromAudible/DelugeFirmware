#pragma once

/// @file
/// @brief Notes named as the menus and popups spell them: a letter, an accidental on the black keys, and an octave
///        number on which MIDI note 60 is C3.

#include <array>
#include <cstddef>
#include <cstdint>

namespace deluge::music {

/// @brief The sign a note's name carries after its letter.
enum class Accidental : uint8_t {
	NONE,  ///< a white key: the letter alone
	SHARP, ///< a black key named with sharps
	FLAT,  ///< a black key named with flats
};

/// @brief One pitch class's spellings.
struct PitchClassName {
	char sharp_letter; ///< the letter the class is named with when spelling with sharps
	char flat_letter;  ///< the letter the class is named with when spelling with flats
	bool black_key;    ///< whether the class is a black key, so its name carries an accidental
};

/// @brief The twelve pitch classes' spellings, from C.
inline constexpr std::array<PitchClassName, 12> kPitchClassNames{{
    {.sharp_letter = 'C', .flat_letter = 'C', .black_key = false},
    {.sharp_letter = 'C', .flat_letter = 'D', .black_key = true},
    {.sharp_letter = 'D', .flat_letter = 'D', .black_key = false},
    {.sharp_letter = 'D', .flat_letter = 'E', .black_key = true},
    {.sharp_letter = 'E', .flat_letter = 'E', .black_key = false},
    {.sharp_letter = 'F', .flat_letter = 'F', .black_key = false},
    {.sharp_letter = 'F', .flat_letter = 'G', .black_key = true},
    {.sharp_letter = 'G', .flat_letter = 'G', .black_key = false},
    {.sharp_letter = 'G', .flat_letter = 'A', .black_key = true},
    {.sharp_letter = 'A', .flat_letter = 'A', .black_key = false},
    {.sharp_letter = 'A', .flat_letter = 'B', .black_key = true},
    {.sharp_letter = 'B', .flat_letter = 'B', .black_key = false},
}};

/// @brief A note's pitch class, 0 (C) to 11 (B).
/// @param note The note number (MIDI numbering). A note below 0 takes the class it would have counting down from C,
///             so -1 is B.
/// @return The pitch class.
[[nodiscard]] constexpr int32_t pitch_class(int32_t note) {
	return ((note % 12) + 12) % 12;
}

/// @brief The letter of a note's pitch class.
/// @param note The note number (MIDI numbering); a note below 0 is read as pitch_class() reads it.
/// @param sharps Whether to name a black key with sharps (C# is 'C') or with flats (Db is 'D').
/// @return The letter, 'A' to 'G'.
[[nodiscard]] constexpr char note_letter(int32_t note, bool sharps) {
	const PitchClassName& name = kPitchClassNames[static_cast<size_t>(pitch_class(note))];
	return sharps ? name.sharp_letter : name.flat_letter;
}

/// @brief Whether a note's pitch class is a black key, so its name carries an accidental.
/// @param note The note number (MIDI numbering); a note below 0 is read as pitch_class() reads it.
/// @return True for a black key.
[[nodiscard]] constexpr bool is_accidental(int32_t note) {
	return kPitchClassNames[static_cast<size_t>(pitch_class(note))].black_key;
}

/// @brief The octave number a note's name carries: MIDI note 60 is C3, so note 0 is C-2.
/// @param note The note number (MIDI numbering). The octave is the floor of the twelfths, so a note below 0 lands in
///             the octave under C-2 (-1 is B-3).
/// @return The octave number.
[[nodiscard]] constexpr int32_t note_octave(int32_t note) {
	return (note - pitch_class(note)) / 12 - 2;
}

/// @brief A note's name in its parts.
struct NoteName {
	char letter;           ///< the pitch class's letter, 'A' to 'G'
	Accidental accidental; ///< the sign after the letter, NONE on a white key
	int32_t octave;        ///< the octave number, on which MIDI note 60 is C3
};

/// @brief A note's name: its letter, its accidental and its octave.
/// @param note The note number (MIDI numbering); a note below 0 is read as pitch_class() and note_octave() read it.
/// @param sharps Whether to name a black key with sharps or with flats.
/// @return The name's parts.
[[nodiscard]] constexpr NoteName note_name(int32_t note, bool sharps) {
	Accidental accidental = Accidental::NONE;
	if (is_accidental(note)) {
		accidental = sharps ? Accidental::SHARP : Accidental::FLAT;
	}
	return {.letter = note_letter(note, sharps), .accidental = accidental, .octave = note_octave(note)};
}

} // namespace deluge::music
