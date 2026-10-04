#pragma once

#include "serialization/value.h"

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace deluge::serialization {

/// @brief The children named @p name of @p container, in order, from either array encoding: XML gives a Map with one
/// entry per child (`<sections><section/><section/></sections>`), JSON an Array of single-key boxes
/// (`"sections": [{"section": {…}}, …]`).
/// @return The children, or none if @p container is a scalar.
[[nodiscard]] inline std::vector<const Value*> elements_named(const Value& container, std::string_view name) {
	std::vector<const Value*> found;
	if (container.isMap()) {
		for (const auto& [key, child] : container.entries()) {
			if (key == name) {
				found.push_back(&child);
			}
		}
	}
	else if (container.isArray()) {
		for (const Value& item : container.items()) {
			if (item.isMap()) {
				if (const Value* child = item.find(name)) {
					found.push_back(child);
				}
			}
		}
	}
	return found;
}

/// @brief A scalar's text, or nullptr if @p node is missing or not a scalar.
[[nodiscard]] inline const std::string* text_of(const Value* node) {
	return (node != nullptr && node->isScalar()) ? &node->str() : nullptr;
}

/// @brief A scalar as a decimal int32, read as the song reader reads it: text it cannot parse reads as 0.
/// @return The value, or nullopt if @p node is missing or not a scalar.
[[nodiscard]] inline std::optional<int32_t> int_of(const Value* node) {
	const std::string* text = text_of(node);
	if (text == nullptr) {
		return std::nullopt;
	}
	int32_t value = 0;
	std::from_chars(text->data(), text->data() + text->size(), value, 10);
	return value;
}

/// @brief A `0x`-prefixed hexadecimal scalar.
/// @return The value, or nullopt if @p node is missing, not a scalar, or not hex.
[[nodiscard]] inline std::optional<uint32_t> hex_of(const Value* node) {
	const std::string* text = text_of(node);
	if (text == nullptr || text->size() < 2 || (*text)[0] != '0' || (*text)[1] != 'x') {
		return std::nullopt;
	}
	uint32_t value = 0;
	auto [ptr, ec] = std::from_chars(text->data() + 2, text->data() + text->size(), value, 16);
	if (ec != std::errc{}) {
		return std::nullopt;
	}
	return value;
}

} // namespace deluge::serialization
