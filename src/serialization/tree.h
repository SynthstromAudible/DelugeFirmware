#pragma once

#include "serialization/value.h"

#include <algorithm>
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
/// @param container A Map or Array node.
/// @param name      The child name to match.
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

/// @brief One child of a list whose children have different names, with its name.
struct NamedChild {
	std::string_view name;
	const Value* node;
};

/// @brief Every child of @p container with its name, in order, from either array encoding (see elements_named): the
/// lists whose elements are of several kinds, such as a song's outputs (`sound`, `kit`, `midi`…) or clips.
/// @param container A Map or Array node.
/// @return The named children; empty if @p container is a scalar.
[[nodiscard]] inline std::vector<NamedChild> children(const Value& container) {
	std::vector<NamedChild> found;
	if (container.isMap()) {
		for (const auto& [key, child] : container.entries()) {
			found.push_back({key, &child});
		}
	}
	else if (container.isArray()) {
		for (const Value& item : container.items()) {
			if (item.isMap() && !item.entries().empty()) {
				found.push_back({item.entries().front().first, &item.entries().front().second});
			}
		}
	}
	return found;
}

/// @brief A scalar's text, or nullptr if @p node is missing or not a scalar.
/// @param node The node to read; may be null.
/// @return The scalar's text, or nullptr.
[[nodiscard]] inline const std::string* text_of(const Value* node) {
	return (node != nullptr && node->isScalar()) ? &node->str() : nullptr;
}

/// @brief A scalar as a decimal int32, read as the song reader reads it: text it cannot parse reads as 0.
/// @param node The node to read; may be null.
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
/// @param node The node to read; may be null.
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

/// @brief The slash-separated path of the first entry where @p a and @p b differ (keys, their order, array lengths,
/// scalar text), or empty when they are the same tree. Scalar kinds are ignored: every scalar an XML file holds is
/// text. "/" means the roots themselves differ.
/// @param a      The first tree.
/// @param b      The second tree.
/// @param prefix Path of @p a and @p b within the enclosing tree; empty at the root.
/// @return The path of the first difference, or an empty string if the trees are equal.
[[nodiscard]] inline std::string first_difference(const Value& a, const Value& b, const std::string& prefix = {}) {
	if (a.type() != b.type()) {
		return prefix.empty() ? "/" : prefix;
	}
	if (a.isScalar()) {
		return a.str() == b.str() ? std::string{} : (prefix.empty() ? "/" : prefix);
	}
	if (a.isArray()) {
		const size_t n = std::max(a.items().size(), b.items().size());
		for (size_t i = 0; i < n; i++) {
			const std::string path = prefix + "/" + std::to_string(i);
			if (i >= a.items().size() || i >= b.items().size()) {
				return path;
			}
			if (std::string d = first_difference(a.items()[i], b.items()[i], path); !d.empty()) {
				return d;
			}
		}
		return {};
	}
	const auto& ea = a.entries();
	const auto& eb = b.entries();
	const size_t n = std::max(ea.size(), eb.size());
	for (size_t i = 0; i < n; i++) {
		const std::string& key = i < ea.size() ? ea[i].first : eb[i].first;
		const std::string path = prefix.empty() ? key : prefix + "/" + key;
		if (i >= ea.size() || i >= eb.size() || ea[i].first != eb[i].first) {
			return path;
		}
		if (std::string d = first_difference(ea[i].second, eb[i].second, path); !d.empty()) {
			return d;
		}
	}
	return {};
}

} // namespace deluge::serialization
