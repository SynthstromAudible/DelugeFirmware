/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace deluge::gui::browser {

/// The delimiter used when giving a name its first numeric suffix ("MYTRACK" -> "MYTRACK 2").
/// Deliberately NOT display-dependent: a name must never depend on which Deluge saved it.
inline constexpr char kNumericSuffixDelimiter = ' ';

/// Steps past `filePrefix` and any zero-padding, yielding the part Browser::getSlot() expects:
///   "SONG001" -> "1"   "SONG010" -> "10"   "SONG185A" -> "185A"   "SONG000" -> "0"
/// Returns nullptr if `name` does not carry the prefix.
///
/// The zero-skipping is load-bearing, not cosmetic: getSlot() reads a leading '0' as a complete one-digit slot and
/// then treats the *next* digit as a subslot letter, so it rejects "001" outright (slot = -1). Vanilla cards write
/// songs 1-99 zero-padded, so without this the 7SEG renders "SONG001" as a scrolling full name instead of "1", and
/// slot navigation dies for those songs.
char const* numberPartOf(char const* name, char const* filePrefix);

/// @brief What a folder holds of a name's family: the variations nextDefaultName() must not hand out.
///
/// Built from the name being saved over before the folder is read, then fed every file name the read
/// passes, so it sees the whole folder however the listing culls it. The listing keeps only a window of
/// names around the file being saved, and a member of the family can sit any distance from it in sort
/// order: probing candidates against that window reported names missing that were really there.
class NameFamilySurvey {
public:
	/// @param currentName The name being saved over, without extension: "SONG185", "MY PROJECT 11".
	/// @param slotPrefix  As for nextDefaultName().
	NameFamilySurvey(std::string_view currentName, std::string_view slotPrefix);

	/// @brief Fold in one name in the folder.
	/// @param fileName With or without its extension. Names differing only in case are the same name.
	void observe(std::string_view fileName);

	/// @brief Forget everything observed, for a read that starts over.
	void restart() {
		highestNumber_ = 0;
		takenLetters_ = 0;
	}

	/// The highest N among "<base><delimiter>N" names observed, 0 if none.
	[[nodiscard]] int32_t highestNumber() const { return highestNumber_; }
	/// A bit per observed "<stem><letter>" name, bit 0 for 'A'.
	[[nodiscard]] uint32_t takenLetters() const { return takenLetters_; }

private:
	std::string letterStem_;   ///< "SONG185" for a slot-form name, else empty
	std::string numberPrefix_; ///< "MY PROJECT " for any other name, else empty
	int32_t highestNumber_{};
	uint32_t takenLetters_{};
};

/// Derives the default name for the next variation of `currentName`.
///
/// `currentName` and the result are real on-card names: no extension, and no display-specific mangling (always
/// "SONG185", never "185").
///
/// `slotPrefix` is the prefix that earns letter-suffix treatment - "SONG" for songs, and empty for everything else.
/// Presets deliberately do NOT get letter suffixes: that would change current OLED preset behaviour. Pass "" and they
/// take the numeric path.
///
/// Two forms:
///   <slotPrefix><digits>[letter]  -> first free letter after the current one:  SONG185 -> SONG185A -> SONG185B
///   anything else                 -> one past the highest number:  MYTRACK -> "MYTRACK 2"
///                                                                  TRACK_1 -> TRACK_2 (an existing delimiter is
///                                                                  reused)
///
/// Numbers count up from the family's highest member rather than filling gaps, so a deleted "MYTRACK 3" is not handed
/// back. The name being saved over is on the card, so its own number is a floor even if `family` saw nothing.
///
/// Returns `currentName` unchanged when no free variation exists (letters past Z, numbers past 9999).
std::string nextDefaultName(std::string_view currentName, std::string_view slotPrefix, NameFamilySurvey const& family);

/// @brief The slot name after @p lastName -- "SYNT7" after "SYNT6" -- as Browser::getUnusedSlot names a new
///        instrument.
/// @param lastName The highest-sorting entry's filename without extension, or empty when the folder has none.
/// @param thingName The slot prefix, e.g. "SYNT" or "KIT".
/// @return @p thingName and the number after the digits that follow it in @p lastName, padded to as many digits as
///         @p lastName had; @p thingName + "1" when there is no entry or it has no digits there.
[[nodiscard]] std::string nextSlotName(std::string_view lastName, std::string_view thingName);

} // namespace deluge::gui::browser
