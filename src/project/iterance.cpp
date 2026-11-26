#include "project/iterance.h"

#include "base/divide.h"

#include <algorithm>

namespace deluge::project {

bool Iterance::passes(uint32_t repeat, bool ending) const {
	if (divisor == 0) {
		if (steps.none()) {
			return true; // OFF: every repeat
		}
		return steps == kIteranceFirst.steps ? repeat == 0 : ending;
	}
	const auto step = base::divide(int64_t{repeat}, int64_t{divisor}).remainder;
	return steps[static_cast<std::size_t>(step)];
}

namespace {

/// @brief Appends text to a label, keeping room for its terminator.
class LabelWriter {
public:
	explicit LabelWriter(std::span<char> out) : out_{out} {}

	void put(std::string_view text) {
		for (const char c : text) {
			if (length_ + 1 >= out_.size()) {
				return;
			}
			out_[length_++] = c;
		}
	}

	/// @brief Appends a step or a divisor, 0 to 8, as its digit.
	void put_digit(int32_t value) {
		const char digit = static_cast<char>('0' + std::clamp(value, 0, 9));
		put(std::string_view{&digit, 1});
	}

	/// @brief Terminates the label.
	/// @return Its length.
	std::size_t finish() {
		if (!out_.empty()) {
			out_[length_] = '\0';
		}
		return length_;
	}

private:
	std::span<char> out_;
	std::size_t length_ = 0;
};

/// @brief The step an x-of-y preset shows: its highest playing step, counting from 1; 0 when none plays.
int32_t active_step(const Iterance& iterance) {
	for (int32_t i = iterance.divisor - 1; i >= 0; --i) {
		if (iterance.steps[static_cast<std::size_t>(i)]) {
			return i + 1;
		}
	}
	return 0;
}

} // namespace

std::size_t format_preset(int32_t index, std::span<char> out, std::string_view prefix, std::string_view step_separator,
                          Iterance::Label label) {
	LabelWriter writer{out};
	writer.put(prefix);
	if (index == kCustomIterancePreset) {
		writer.put("CUSTOM");
	}
	else if (index == kIterancePresetFirst) {
		writer.put(label == Iterance::Label::LONG ? "FIRST" : "1ST");
	}
	else if (index == kIterancePresetLast) {
		writer.put("LAST");
	}
	else if (index > 0 && index <= kIterancePresetCount) {
		const Iterance& preset = kIterancePresets[static_cast<std::size_t>(index - 1)];
		writer.put_digit(active_step(preset));
		writer.put(step_separator);
		writer.put_digit(preset.divisor);
	}
	else {
		writer.put("OFF");
	}
	return writer.finish();
}

} // namespace deluge::project
