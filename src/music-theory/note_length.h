#pragma once

/// @file
/// @brief Note lengths named as the menus and popups show them: "16th-notes", "2-bar", "4 bars (per column)".

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace deluge::music {

/// @brief A note length's name, held by value.
///
/// The buffer is wider than any name the menus produce (the longest, at magnitude 31 with a 6-character suffix
/// twice, is 10 digits + "th" + 12 = 24 characters); a longer suffix is cut at the buffer's end.
struct NoteLengthName {
	static constexpr size_t kCapacity = 48; ///< characters the name may hold
	std::array<char, kCapacity> text{};     ///< the name's characters, not terminated
	size_t size{0};                         ///< how many of text are the name
	/// @brief The name.
	/// @return A view into this object; it lasts as long as the object does.
	[[nodiscard]] std::string_view view() const { return {text.data(), size}; }
};

/// @brief The magnitude of a note @p note_length ticks long, against @p tick_magnitude: 0 is one bar, positive is
///        that power of two bars, negative is that power-of-two division of a bar.
/// @param note_length   The length in ticks at the base resolution, where 3 is the shortest (a 256th's base).
/// @param tick_magnitude The song's input tick magnitude.
/// @return The magnitude.
[[nodiscard]] int32_t note_magnitude(uint32_t note_length, int32_t tick_magnitude);

/// @brief The name of a note of magnitude @p magnitude.
/// @param magnitude  A note magnitude from note_magnitude(), -31 to 31.
/// @param suffix     Appended to a division's name ("-notes", "-tplts"); empty for none.
/// @param per_column A bar length reads "N bar(s) (per column)" instead of "N-bar".
/// @return The name.
[[nodiscard]] NoteLengthName note_length_name(int32_t magnitude, std::string_view suffix, bool per_column);

/// @brief Which of a sync value's two printed forms to give.
enum class SyncNameForm : uint8_t {
	Full,            ///< the value page's: "16th-notes", "8th-tplts", "2-bar-dtted"
	HorizontalLabel, ///< the horizontal menu's column label: the length only, without '-'
};

/// @brief A combined sync value's name, as the sync-level menus print it.
/// @param combined_value A combined sync value (level and type), 0 to kMaxCombinedSync.
/// @param tick_magnitude The song's input tick magnitude.
/// @param form           Which printed form to give.
/// @return The name.
[[nodiscard]] NoteLengthName sync_value_name(int32_t combined_value, int32_t tick_magnitude, SyncNameForm form);

} // namespace deluge::music
