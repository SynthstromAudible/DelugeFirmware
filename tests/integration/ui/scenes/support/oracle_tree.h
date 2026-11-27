#pragma once

/// @file
/// @brief A menu tree as the reference tables key it: every node under a root by its child positions.

#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/nodes/settings.h"
#include "ui/scenes/menu/nodes/sound_dx7.h"
#include "ui/scenes/menu/nodes/sound_master.h"
#include "ui/scenes/menu/shortcuts.h"

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

/// @brief The port's own nodes, which `main` never had, so no reference table keys them: the DX7's columns, OSC1's
///        in DX7 mode (`nodes/sound_dx7.h`), appended after `main`'s own so that no row of `main`'s moves. Each is a
///        recorded difference (tests/ui-scripts/parity/DIFFERENCES.md).
inline constexpr std::array<const menu::Node*, 5> kPortOwnNodes{&menu::nodes::dx7_engine, &menu::nodes::dx7_transpose,
                                                                &menu::nodes::dx7_amp_mod, &menu::nodes::dx7_phase,
                                                                &menu::nodes::dx7_level};

/// @brief Whether @p node is one of the port's own (kPortOwnNodes).
inline bool port_own(const menu::Node& node) {
	return std::ranges::find(kPortOwnNodes, &node) != kPortOwnNodes.end();
}

/// @brief A child of `main`'s that the port drops, by its parent and its index among `main`'s children.
struct MainOnlyChild {
	const menu::Node* parent;
	size_t index;
	std::string_view why;
};

/// @brief `main`'s children that the port drops on purpose; each is a recorded difference
///        (tests/ui-scripts/parity/DIFFERENCES.md). The reference rows keep `main`'s positions, so the port's later
///        siblings are keyed one further on.
inline constexpr std::array<MainOnlyChild, 1> kMainOnlyChildren{
    {{.parent = &menu::nodes::runtime_features, .index = 11, .why = "the DX7 engine feature: the DX7 is built in"}}};

/// @brief How many of `main`'s children of @p parent the port drops.
inline size_t main_only_under(const menu::Node& parent) {
	return static_cast<size_t>(
	    std::ranges::count_if(kMainOnlyChildren, [&](const MainOnlyChild& c) { return c.parent == &parent; }));
}

/// @brief Whether `main`'s child @p index of @p parent is one the port drops.
inline bool main_only(const menu::Node& parent, size_t index) {
	return std::ranges::any_of(kMainOnlyChildren,
	                           [&](const MainOnlyChild& c) { return c.parent == &parent && c.index == index; });
}

/// @brief How many children `main` lists under @p node: its children but the port's own, and the ones it drops.
inline size_t main_children(const menu::Node& node) {
	return static_cast<size_t>(std::ranges::count_if(
	           node.children, [](const menu::Node* child) { return child != nullptr && !port_own(*child); }))
	       + main_only_under(node);
}

/// @brief Every node under @p root by `Node::children`, pre-order, as the reference tables key it: the port's own
///        nodes (kPortOwnNodes) left out.
inline std::vector<TreeEntry> tree_entries(std::string_view label, const menu::Node& root) {
	std::vector<TreeEntry> out;
	const auto walk = [&out](auto& self, const menu::Node& node, std::string path, const menu::Node* parent,
	                         int32_t depth) -> void {
		out.push_back({path, &node, parent, depth});
		if (node.kind != menu::Kind::Submenu) {
			return;
		}
		size_t main_index = 0; // the child's position among `main`'s, past the ones the port drops
		for (const menu::Node* child : node.children) {
			if (child == nullptr || port_own(*child)) {
				continue;
			}
			while (main_only(node, main_index)) {
				main_index++;
			}
			self(self, *child, path + "/" + std::to_string(main_index), &node, depth + 1);
			main_index++;
		}
	};
	walk(walk, root, std::string{label}, nullptr, 0);
	return out;
}

/// @brief A choice list the port shows otherwise than `main`, on purpose: the DX7 is a sound's engine, so the synth
///        mode lists it after ring mod, and oscillator 1's type (the one `main` listed it on) does not. Each is a
///        recorded difference
///        (tests/ui-scripts/parity/DIFFERENCES.md).
struct ChoiceDifference {
	std::string_view path;   ///< the setting row whose list differs
	std::string_view choice; ///< the choice's shown name
	std::string_view after;  ///< added after this choice; empty: the choice is dropped
};
inline constexpr std::array<ChoiceDifference, 2> kChoiceDifferences{{
    {.path = "mode", .choice = "Dx7", .after = "Ringmod"},
    {.path = "osc1/type", .choice = "Dx7", .after = ""},
}};

/// @brief The choices the port lists where @p node shows @p main's: @p main with kChoiceDifferences applied.
inline std::vector<std::string> port_choices(const menu::Node& node, std::vector<std::string> main) {
	for (const ChoiceDifference& d : kChoiceDifferences) {
		if (!node.bind.target.valid() || node.bind.target.path != d.path) {
			continue;
		}
		if (d.after.empty()) {
			std::erase(main, std::string{d.choice});
		}
		else if (const auto at = std::ranges::find(main, std::string{d.after}); at != main.end()) {
			main.insert(at + 1, std::string{d.choice});
		}
	}
	return main;
}

/// @brief A node the port shows where `main` hid it, on purpose; each is a recorded difference
///        (tests/ui-scripts/parity/DIFFERENCES.md). The synth mode shows on every sound, the way into the DX7 and FM
///        whatever the oscillators play: `main` hid it while an oscillator was past a wavetable (`SynthModeSelection`).
inline constexpr std::array<const menu::Node*, 1> kShownWhereMainHid{&menu::nodes::synth_mode};

/// @brief Whether the port showing @p node (@p port_shows) where `main` answered @p main_shows is a named difference:
///        a node of kShownWhereMainHid, shown where `main` hid it.
inline bool shows_where_main_hid(const menu::Node& node, bool port_shows, bool main_shows) {
	return port_shows && !main_shows && std::ranges::find(kShownWhereMainHid, &node) != kShownWhereMainHid.end();
}

/// @brief Whether pad (@p x, @p y) is a sound shortcut to a node of kShownWhereMainHid, in either layout.
inline bool pad_shown_where_main_hid(uint8_t x, uint8_t y) {
	for (const deluge::project::ShortcutsVersion version :
	     {deluge::project::ShortcutsVersion::V1, deluge::project::ShortcutsVersion::V3}) {
		const menu::Node* node = menu::shortcut_node(menu::MenuContext::Root::Sound, version, false, x, y);
		if (node != nullptr && std::ranges::find(kShownWhereMainHid, node) != kShownWhereMainHid.end()) {
			return true;
		}
	}
	return false;
}

/// @brief The paths `main`'s dropped children (kMainOnlyChildren) have under @p root, keyed as the tables key them.
inline std::vector<std::string> main_only_paths(std::string_view label, const menu::Node& root) {
	std::vector<std::string> out;
	for (const TreeEntry& e : tree_entries(label, root)) {
		for (const MainOnlyChild& c : kMainOnlyChildren) {
			if (c.parent == e.node) {
				out.push_back(e.path + "/" + std::to_string(c.index));
			}
		}
	}
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
