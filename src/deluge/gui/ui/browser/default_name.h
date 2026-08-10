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

#include <string>
#include <string_view>

namespace deluge::gui::browser {

/// Read-only view over the browser's file list, so name derivation can be tested without a card, a display, or the
/// browser's static state. Implemented over Browser::fileItems in the firmware, and over a vector in tests.
class FileListView {
public:
	virtual ~FileListView() = default;
	/// True if the list holds this exact name. The name includes its extension, matching FileItem::displayName (the
	/// CStringArray sort key).
	virtual bool contains(char const* nameWithExtension) const = 0;
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
///   <slotPrefix><digits>[letter]  -> next unused letter:  SONG185 -> SONG185A -> SONG185B
///   anything else                 -> next unused number:  MYTRACK -> "MYTRACK 2"
///                                                         TRACK_1 -> TRACK_2 (an existing delimiter is reused)
///
/// Returns `currentName` unchanged when no free variation exists (letters exhausted past Z).
std::string nextDefaultName(std::string_view currentName, std::string_view slotPrefix, FileListView const& files);

} // namespace deluge::gui::browser
