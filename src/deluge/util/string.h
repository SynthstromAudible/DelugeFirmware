#pragma once
#include <cstdint>
#include <expected>
#include <string>
#include <system_error>

namespace deluge {
std::expected<char*, std::errc> to_chars(char* first, char* last, float value, int precision);
} // namespace deluge

namespace deluge::string {

/// @brief convert an integer into a string, left-padding with '0's
std::string fromInt(int32_t number, size_t min_num_digits = 1);
std::string fromFloat(float number, int32_t precision);
std::string fromSlot(int32_t slot, int32_t sub_slot, size_t min_num_digits = 1);
std::string fromNoteCode(int32_t noteCode, size_t* getLengthWithoutDot = nullptr, bool appendOctaveNo = true);

bool caselessEquals(std::string_view a, std::string_view b);

/// @brief Whether @p text is well-formed UTF-8.
///
/// Strict, in the Unicode Table 3-7 sense: an overlong encoding, a surrogate (U+D800..U+DFFF), a code
/// point above U+10FFFF, a truncated sequence and a stray continuation byte are all rejected. That is
/// deliberately the same rule Rust's `core::str::from_utf8` applies, because the one caller that
/// matters -- the async audio loader's admission check -- has to agree with the Rust side about which
/// paths it will accept, or it asks for loads that are refused forever.
///
/// A FAT long filename is UCS-2 on the card, so the byte string a path arrives as here need not be
/// valid UTF-8 at all; nothing else in this codebase requires it to be.
///
/// @param text The bytes to check (not required to be NUL-terminated).
/// @return true if every byte sequence in @p text is well-formed UTF-8 (vacuously true when empty).
[[nodiscard]] bool isValidUtf8(std::string_view text);

} // namespace deluge::string
