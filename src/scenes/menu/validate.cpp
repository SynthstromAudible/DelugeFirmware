/*
 * Copyright © 2026 Synthstrom Audible Limited
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

/// @file
/// @brief The menu-tree validator. See `validate.h` for the rule list and the `Issue` shape.
///
/// **Rule-by-rule, against spec E §4.4's sentence** ("Every node reachable from a root; every
/// `Submenu` non-empty under at least one context; every `ParamRef` resolves for its
/// `MenuContext`; every `Enum` has `names` or `dynamic_names`; every shortcut cell points at a
/// node; no duplicate `(name, parent)`; `Custom` count <= 10"):
///
///   * *reachable* -> `Issue::Kind::Unreachable`. `assign_node_ids` reports how many nodes it could
///     number and stops at `kMaxNodes` (`node_id.h`); this walk counts the reachable nodes itself
///     and flags the first one left without an id.
///   * *non-empty submenu* -> `EmptySubmenu`, quantified over the supplied contexts in which the
///     submenu is itself relevant (see the rule's own comment for why that qualifier is there).
///   * *`ParamRef` resolves* -> `BadParamRef`. Structural, not model-backed: the kind must be one
///     `bindings.cpp` can serve and the index must be inside that kind's parameter space
///     (`modulation/params/param.h:253-260`). Whether a *particular* song's param manager holds the
///     collection is `bindings::read`/`write`'s own `serves_param` test, at the point of use, and is
///     not something a static tree check can decide.
///   * *`Enum` names* -> `EnumWithoutNames`.
///   * *shortcut cells* -> `ShortcutToNothing`, over every `MenuContext::Root` and both shift
///     layers.
///   * *no duplicate `(name, parent)`* -> `DuplicateName`.
///   * *`Custom` <= 10* -> `TooManyCustom`, counting distinct `CustomItem` instances
///     (`validate.h`'s `kMaxCustomItems`).
///
/// plus, beyond §4.4: `MissingTitle` and `NullCustom` (a node that would render nothing),
/// `PlaceholderCustom` (a node that renders a stand-in for an item E2 still owes -- suppressible,
/// and only ever suppressed by an E1 spec, `ValidateOptions::allow_placeholder_custom`), and the
/// two `FlagMismatch` audits (task 6's ruling 4).
///
/// **No heap, no model.** The walk uses two fixed `kMaxNodes`-wide arrays of node pointers -- the
/// reachable set and `collect`'s explicit work stack -- so the frame is `2 * kMaxNodes *
/// sizeof(const Node*)`: 8 KB on the 32-bit target, 16 KB on a 64-bit host. Fine for a spec, and
/// the reason nothing here is recursive; check it against the device stack before Task 12 wires
/// this into boot.

#include "scenes/menu/validate.h"

#include "scenes/menu/node_id.h"
#include "scenes/menu/shortcuts.h"

#include "gfx/grid/pad_grid.h" // kMaxGridCols / kMaxGridRows -- the widest pad grid to sweep
#include "modulation/params/param.h"

#include <algorithm>

namespace deluge::scenes::menu {

namespace {

namespace params = deluge::modulation::params;

using Issues = deluge::ui::shell::FixedVector<Issue, 64>;

void add(Issues& out, Issue::Kind kind, const Node* node, uint16_t detail = 0) {
	(void)out.push_back(Issue{kind, node, detail});
}

/// @brief The reachable set, in `assign_node_ids`' own pre-order.
struct Reachable {
	const Node* nodes[kMaxNodes]{};
	size_t count = 0;

	bool contains(const Node* n) const { return std::find(nodes, nodes + count, n) != nodes + count; }
	bool push(const Node* n) {
		if (count == kMaxNodes) {
			return false;
		}
		nodes[count++] = n;
		return true;
	}
};

/// @brief Collect every node reachable from @p roots, depth-first, pre-order, declaration order --
///        the same walk `assign_node_ids` does, so the two counts are comparable.
///
/// @return False if the walk hit `kMaxNodes` and had to stop (the tree is bigger than the registry).
bool collect(std::span<const Node* const> roots, Reachable& seen) {
	// An explicit stack rather than recursion: a malformed table could otherwise blow the real
	// stack, and this file is meant to survive being handed a bad tree.
	const Node* stack[kMaxNodes];
	size_t top = 0;
	for (size_t i = roots.size(); i > 0; i--) {
		if (top == kMaxNodes) {
			return false;
		}
		stack[top++] = roots[i - 1];
	}
	while (top > 0) {
		const Node* n = stack[--top];
		if (n == nullptr || seen.contains(n)) {
			continue; // a shared node is one node, visited once
		}
		if (!seen.push(n)) {
			return false;
		}
		for (size_t i = n->children.size(); i > 0; i--) {
			if (top == kMaxNodes) {
				return false;
			}
			stack[top++] = n->children[i - 1];
		}
	}
	return true;
}

/// @brief Is @p param a pair `bindings.cpp` could resolve? (`modulation/params/param.h:253-260`.)
bool param_in_range(const ParamRef& param) {
	switch (param.kind) {
	case params::Kind::PATCHED:
		return param.index < params::kNumParams;
	case params::Kind::UNPATCHED_SOUND:
	case params::Kind::UNPATCHED_GLOBAL:
		return param.index < params::kMaxNumUnpatchedParams;
	default:
		return false;
	}
}

/// @brief Does @p node hold a value at all -- i.e. is a missing binding a bug?
bool is_value_kind(Kind k) {
	switch (k) {
	case Kind::Toggle:
	case Kind::Enum:
	case Kind::Int:
	case Kind::Decimal:
	case Kind::Bipolar:
	case Kind::SyncLevel:
		return true;
	default:
		return false;
	}
}

/// @brief How many of @p node's declared children @p ctx admits.
size_t visible_count(const Node& node, const MenuContext& ctx) {
	size_t n = 0;
	for (const Node* kid : node.children) {
		if (kid != nullptr && (kid->relevant == nullptr || kid->relevant(ctx))) {
			n++;
		}
	}
	return n;
}

void check_flag_set(Issues& out, const Reachable& seen, std::span<const Node* const> expected, Flag flag,
                    bool (*has_flag)(const Node&)) {
	const auto detail = static_cast<uint16_t>(flag);
	// Set, but not expected.
	for (size_t i = 0; i < seen.count; i++) {
		const Node* n = seen.nodes[i];
		if (has_flag(*n) && std::find(expected.begin(), expected.end(), n) == expected.end()) {
			add(out, Issue::Kind::FlagMismatch, n, detail);
		}
	}
	// Expected, but not set (or not even in the tree).
	for (const Node* n : expected) {
		if (n == nullptr || !seen.contains(n) || !has_flag(*n)) {
			add(out, Issue::Kind::FlagMismatch, n, detail);
		}
	}
}

bool flag_range_dependent(const Node& n) {
	return n.range_dependent;
}
bool flag_half_precision(const Node& n) {
	return n.bind.param.valid() && n.bind.scaling == Scaling::HalfPrecision;
}

} // namespace

deluge::ui::shell::FixedVector<Issue, 64> validate_tree(std::span<const Node* const> roots,
                                                        std::span<const MenuContext> contexts,
                                                        std::span<const Node* const> expected_range_dependent,
                                                        std::span<const Node* const> expected_half_precision,
                                                        ValidateOptions options) {
	Issues out;

	// Ruling Q7: ids first, so everything downstream (and the `Unreachable` rule below) has them.
	const uint16_t assigned = assign_node_ids(roots);

	Reachable seen;
	const bool complete = collect(roots, seen);

	// --- Every node reachable from a root got an id -------------------------------------------
	if (!complete || assigned != seen.count) {
		const Node* offender = nullptr;
		for (size_t i = 0; i < seen.count && offender == nullptr; i++) {
			if (node_id(*seen.nodes[i]) == 0) {
				offender = seen.nodes[i];
			}
		}
		add(out, Issue::Kind::Unreachable, offender, static_cast<uint16_t>(seen.count));
	}

	// --- Per-node rules -----------------------------------------------------------------------
	const CustomItem* customs[kMaxCustomItems + 1]{};
	size_t custom_count = 0;
	const Node* last_custom = nullptr;

	for (size_t i = 0; i < seen.count; i++) {
		const Node& n = *seen.nodes[i];

		if (n.title == 0) {
			add(out, Issue::Kind::MissingTitle, &n, n.name);
		}

		if (n.kind == Kind::Enum && n.bind.names.empty() && n.bind.dynamic_names == nullptr) {
			add(out, Issue::Kind::EnumWithoutNames, &n);
		}

		if (n.bind.param.kind != params::Kind::NONE && !param_in_range(n.bind.param)) {
			add(out, Issue::Kind::BadParamRef, &n, n.bind.param.index);
		}
		else if (is_value_kind(n.kind) && !n.bind.param.valid() && n.bind.get == nullptr) {
			add(out, Issue::Kind::BadParamRef, &n, n.bind.param.index);
		}

		if (n.kind == Kind::Custom) {
			if (n.custom == nullptr) {
				add(out, Issue::Kind::NullCustom, &n);
			}
			else {
				if (n.custom->is_placeholder() && !options.allow_placeholder_custom) {
					add(out, Issue::Kind::PlaceholderCustom, &n, n.name);
				}
				last_custom = &n;
				if (std::find(customs, customs + custom_count, n.custom) == customs + custom_count
				    && custom_count < kMaxCustomItems + 1) {
					customs[custom_count++] = n.custom;
				}
			}
		}

		if (n.kind == Kind::Submenu) {
			// A dynamic submenu's children only exist at render time, so it cannot be judged here.
			if (n.dynamic_children == nullptr) {
				// "Non-empty under at least one context" is quantified over the contexts in which
				// the submenu is itself *shown*: a submenu whose own `relevant` hook is false here
				// is not on screen, so its children being filtered out is not an emptiness bug --
				// legacy's `modulator0Menu` / `modulator1Menu` are exactly that shape, an FM-only
				// page whose every child is FM-only too (`submenu/modulator.h:25-28`). When no
				// supplied context shows the submenu at all, there is nothing to quantify over and
				// the check falls back to the structural one: does it declare any children?
				bool populated = false;
				bool ever_shown = false;
				for (const MenuContext& ctx : contexts) {
					if (n.relevant != nullptr && !n.relevant(ctx)) {
						continue;
					}
					ever_shown = true;
					if (visible_count(n, ctx) > 0) {
						populated = true;
						break;
					}
				}
				if (!ever_shown) {
					populated = !n.children.empty();
				}
				if (!populated) {
					add(out, Issue::Kind::EmptySubmenu, &n, static_cast<uint16_t>(n.children.size()));
				}
			}

			// No duplicate `(name, parent)`.
			for (size_t a = 0; a < n.children.size(); a++) {
				if (n.children[a] == nullptr) {
					continue;
				}
				for (size_t b = a + 1; b < n.children.size(); b++) {
					if (n.children[b] != nullptr && n.children[b]->name == n.children[a]->name) {
						add(out, Issue::Kind::DuplicateName, n.children[b], n.children[b]->name);
					}
				}
			}
		}
	}

	if (custom_count > kMaxCustomItems) {
		add(out, Issue::Kind::TooManyCustom, last_custom, static_cast<uint16_t>(custom_count));
	}

	// --- Every shortcut cell points at a node the tree reaches --------------------------------
	for (uint8_t root = 0; root < static_cast<uint8_t>(MenuContext::Root::Count); root++) {
		for (uint8_t layer = 0; layer < 2; layer++) {
			for (uint8_t y = 0; y < deluge::gfx::grid::kMaxGridRows; y++) {
				for (uint8_t x = 0; x < deluge::gfx::grid::kMaxGridCols; x++) {
					const Node* cell = shortcut_node(static_cast<MenuContext::Root>(root), layer != 0, x, y);
					if (cell != nullptr && !seen.contains(cell)) {
						add(out, Issue::Kind::ShortcutToNothing, cell,
						    static_cast<uint16_t>(static_cast<uint16_t>(y) * 256 + x));
					}
				}
			}
		}
	}

	// --- The two flag audits (ruling 4) -------------------------------------------------------
	check_flag_set(out, seen, expected_range_dependent, Flag::RangeDependent, flag_range_dependent);
	check_flag_set(out, seen, expected_half_precision, Flag::HalfPrecision, flag_half_precision);

	return out;
}

} // namespace deluge::scenes::menu
