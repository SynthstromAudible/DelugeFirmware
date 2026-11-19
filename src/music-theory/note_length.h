#pragma once

/// @file
/// @brief Note lengths named as the menus and popups show them: "16th-notes", "2-bar", "4 bars (per column)".

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace deluge::music {

/// @brief A note length's name, held by value.
///
/// The buffer is wider than any name the menus produce (the longest, "2147483648 bars (per column)", is 28
/// characters; a division's name with its suffix is at most 12 plus the suffix); text past the capacity is cut.
class NoteLengthName {
public:
	static constexpr size_t kCapacity = 48; ///< characters the name may hold

	/// @brief Appends @p text, cut at the buffer's end.
	/// @param text The characters to add.
	void append(std::string_view text) {
		const size_t count = std::min(kCapacity - size_, text.size());
		std::copy_n(text.data(), count, text_.data() + size_);
		size_ += count;
	}

	/// @brief Appends @p value in decimal, cut at the buffer's end.
	/// @param value The number to add.
	void append_number(uint32_t value) {
		std::array<char, 10> digits{};
		const auto result = std::to_chars(digits.data(), digits.data() + digits.size(), value);
		append({digits.data(), static_cast<size_t>(result.ptr - digits.data())});
	}

	/// @brief The name.
	/// @return A view into this object; it lasts as long as the object does.
	[[nodiscard]] std::string_view view() const { return {text_.data(), size_}; }

private:
	std::array<char, kCapacity> text_{}; ///< the name's characters, not terminated
	size_t size_{0};                     ///< how many of text_ are the name, at most kCapacity
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
