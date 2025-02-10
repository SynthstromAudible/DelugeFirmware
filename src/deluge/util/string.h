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

} // namespace deluge::string
