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

/// Read-only view over the files on the card, so name derivation can be tested without a card, a display, or the
/// browser's static state. Implemented over the Browser in the firmware, and over a vector in tests.
class FileListView {
public:
	virtual ~FileListView() = default;

	/// Both queries ask about a whole family of names at once, and both must answer for the *entire* folder.
	///
	/// That shape is deliberate. There used to be a contains() here, answered out of Browser::fileItems, and names
	/// were derived by testing candidates against it one at a time. fileItems holds at most
	/// FILE_ITEMS_MAX_NUM_ELEMENTS entries around the file being saved and is culled to about half of that as a
	/// folder is read, so a candidate any real distance away came back missing whether it existed or not - which is
	/// how "MYSONG 11" came to propose "MYSONG 2" and offer to overwrite it. A question about the family as a whole
	/// cannot be answered from a window by accident: the implementation has to go and look.

	/// The greatest name of the form "<prefix><digits>" on the card (extension included), or empty when there is
	/// none. "Greatest" is by the browser's ordering, in which digit runs compare numerically, so this is the
	/// highest-numbered sibling: "MYTRACK 11.XML" beats "MYTRACK 9.XML".
	virtual std::string highestNumberedName(char const* prefix) const = 0;

	/// Which of "<stem>A" .. "<stem>Z" already exist, as a bitmask with bit 0 meaning 'A'. Names differing only in
	/// case or in extension (.XML / .Json) count as the same letter.
	virtual uint32_t takenLetterSuffixes(char const* stem) const = 0;
};

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
///   <slotPrefix><digits>[letter]  -> next unused letter:   SONG185 -> SONG185A -> SONG185B
///   anything else                 -> highest number plus 1: MYTRACK -> "MYTRACK 2"
///                                                          TRACK_1 -> TRACK_2 (an existing delimiter is reused)
///
/// The numeric form counts up from the highest-numbered sibling rather than filling the lowest gap, so deleting
/// "MYTRACK 3" does not make the next save reuse that name.
///
/// Returns `currentName` unchanged when no free variation exists (letters exhausted past Z, or the number would run
/// past kMaxNumericSuffix).
std::string nextDefaultName(std::string_view currentName, std::string_view slotPrefix, FileListView const& files);

} // namespace deluge::gui::browser
