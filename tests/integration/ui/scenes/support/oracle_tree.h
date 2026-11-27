#pragma once

/// @file
/// @brief A menu tree as the reference tables key it: every node under a root by its child positions.

#include "host/chains/params.h"
#include "instruments/classic/params.h"
#include "plugin/types/param_kind.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/node.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace deluge::ui::scenes::spec {

/// @brief One node of a root's tree, and where it sits.
struct TreeEntry {
	std::string path; ///< `<root label>/<child index>/...`; the root's own is the label alone
	const menu::Node* node;
	const menu::Node* parent; ///< the submenu that lists it; null for the root
	int32_t depth;
	/// The depth a legacy submenu would have open around it: one less than its own, 0 for the root's children.
	[[nodiscard]] int32_t navigation_depth() const { return depth > 0 ? depth - 1 : 0; }
};

/// @brief Every node under @p root by `Node::children`, pre-order, as the reference tables key it.
inline std::vector<TreeEntry> tree_entries(std::string_view label, const menu::Node& root) {
	std::vector<TreeEntry> out;
	const auto walk = [&out](auto& self, const menu::Node& node, std::string path, const menu::Node* parent,
	                         int32_t depth) -> void {
		out.push_back({path, &node, parent, depth});
		if (node.kind != menu::Kind::Submenu) {
			return;
		}
		for (size_t i = 0; i < node.children.size(); i++) {
			if (node.children[i] != nullptr) {
				self(self, *node.children[i], path + "/" + std::to_string(i), &node, depth + 1);
			}
		}
	};
	walk(walk, root, std::string{label}, nullptr, 0);
	return out;
}

/// @brief The path of the first entry of @p entries whose node is @p node, or empty.
inline std::string first_path_of(const std::vector<TreeEntry>& entries, const menu::Node& node) {
	for (const TreeEntry& e : entries) {
		if (e.node == &node) {
			return e.path;
		}
	}
	return {};
}

/// @brief Whether a leaf's `ParamRef` is the legacy item's own param: it is when the node binds a param directly. A
///        node with a get/set hook, or a target that is not a param, carries a learn-only `ParamRef`, which the learn
///        rows check instead.
inline bool param_comparable(const menu::Node& node) {
	return node.kind != menu::Kind::Submenu && node.kind != menu::Kind::Custom && node.bind.param.valid()
	       && node.bind.get == nullptr
	       && !(node.bind.target.valid() && node.bind.target.kind != deluge::ui::scenes::bind::Target::Kind::PARAM);
}

/// @brief A node's param in the runtime's numbering, as main's reference rows record it: its collection's kind and its
///        index there.
struct RuntimeParam {
	plugin::ParamKind kind{plugin::ParamKind::NONE};
	uint8_t index{255};
};

/// @brief @p p in the runtime's numbering: a sound's patched or unpatched set, or a chain's; NONE and 255 for no param.
inline RuntimeParam runtime_of(const menu::ParamRef& p) {
	if (!p.id) {
		return {};
	}
	if (p.table == deluge::ui::scenes::bind::ParamTable::CHAIN) {
		return {plugin::ParamKind::UNPATCHED_GLOBAL, host::chains::find_runtime_index(*p.id).value_or(255)};
	}
	const std::optional<instruments::classic::RuntimeSlot> slot = instruments::classic::runtime_slot(*p.id);
	if (!slot) {
		return {};
	}
	return {slot->set == instruments::classic::RuntimeSet::PATCHED ? plugin::ParamKind::PATCHED
	                                                               : plugin::ParamKind::UNPATCHED_SOUND,
	        slot->index};
}

/// @brief A table value as a number, or nothing for a missing row or a value that is not one.
inline std::optional<int32_t> int_of(const std::vector<std::string>* values, size_t at = 0) {
	if (values == nullptr || values->size() <= at) {
		return std::nullopt;
	}
	try {
		return std::stoi((*values)[at]);
	} catch (...) {
		return std::nullopt;
	}
}

/// @brief The three values a write is probed at: the range's ends and its middle.
inline std::array<int32_t, 3> probe_values(const menu::IntRange& r) {
	return {r.min, static_cast<int32_t>((static_cast<int64_t>(r.min) + r.max) / 2), r.max};
}

/// @brief `probe_values` without repeats: a range of two values has its middle on its first.
inline std::vector<int32_t> distinct_probe_values(const menu::IntRange& r) {
	std::vector<int32_t> out;
	for (const int32_t v : probe_values(r)) {
		if (std::find(out.begin(), out.end(), v) == out.end()) {
			out.push_back(v);
		}
	}
	return out;
}

/// @brief A value inside @p r that is not @p v, so a write that stores nothing shows; nothing for a one-value range.
inline std::optional<int32_t> sentinel_for(const menu::IntRange& r, int32_t v) {
	if (r.min == r.max) {
		return std::nullopt;
	}
	return v == r.min ? r.max : r.min;
}

} // namespace deluge::ui::scenes::spec
