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
/// @brief The horizontal-menu chains. See `chains.h`.

#include "scenes/menu/chains.h"

#include "scenes/menu/menu_state.h"
#include "scenes/menu/nodes/nodes.h"

#include "scenes/menu/bindings.h"

#include <algorithm>
#include <span>

namespace deluge::scenes::menu {

namespace {

/// @brief The most columns any group lists between its members, the run `render_horizontal` materialises for it.
consteval size_t largest_group_columns() {
	size_t most = 0;
	for (const Node* group : nodes::kAllGroups) {
		size_t columns = 0;
		for (const Node* member : group->children) {
			columns += member->children.size();
		}
		most = std::max(most, columns);
	}
	return most;
}
static_assert(largest_group_columns() <= kMaxMenuColumns, "a group lists more columns than a page materialises");

// `SoundEditor::getCurrentHorizontalMenusChain`'s five chains (`gui/ui/menus.cpp`,
// `horizontalMenusChainFor*`), each `HorizontalMenuGroup` spelled as its member pages in order.
constexpr const Node* kSoundChain[] = {
    &nodes::recorder,  &nodes::sound_master,  &nodes::source_group, &nodes::voice_group, &nodes::env_group,
    &nodes::lfo_group, &nodes::filters_group, &nodes::eq,           &nodes::mod_fx,      &nodes::reverb_group,
    &nodes::delay,     &nodes::distortion,    &nodes::sidechain,    &nodes::audio_comp,  &nodes::stutter,
    &nodes::arp_group, &nodes::randomizer};
constexpr const Node* kKitChain[] = {&nodes::kit_clip_master,   &nodes::global_filters_group, &nodes::eq,
                                     &nodes::global_mod_fx,     &nodes::global_reverb_group,  &nodes::global_delay,
                                     &nodes::global_distortion, &nodes::global_sidechain,     &nodes::audio_comp,
                                     &nodes::stutter,           &nodes::arp_group_kit,        &nodes::randomizer};
constexpr const Node* kSongChain[] = {&nodes::song_master,       &nodes::global_filters_group, &nodes::eq,
                                      &nodes::global_mod_fx,     &nodes::global_reverb_group,  &nodes::global_delay,
                                      &nodes::global_distortion, &nodes::audio_comp,           &nodes::stutter};
constexpr const Node* kAudioClipChain[] = {&nodes::song_master,
                                           &nodes::audio_clip_sample,
                                           &nodes::global_filters_group,
                                           &nodes::eq,
                                           &nodes::global_mod_fx,
                                           &nodes::global_reverb_group,
                                           &nodes::global_delay,
                                           &nodes::audio_clip_distortion,
                                           &nodes::global_sidechain,
                                           &nodes::audio_comp,
                                           &nodes::stutter};
constexpr const Node* kMidiOrCvChain[] = {&nodes::arp_group_midi, &nodes::randomizer};

} // namespace

std::span<const Node* const> chain_for(MenuContext::Root root) {
	switch (root) {
	case MenuContext::Root::Sound:
	case MenuContext::Root::Drum:
	case MenuContext::Root::MidiDrum:
	case MenuContext::Root::GateDrum:
		return kSoundChain;
	case MenuContext::Root::KitGlobalFx:
		return kKitChain;
	case MenuContext::Root::Song:
	case MenuContext::Root::Performance:
		return kSongChain;
	case MenuContext::Root::AudioClip:
		return kAudioClipChain;
	case MenuContext::Root::MidiOrCv:
		return kMidiOrCvChain;
	default:
		return {};
	}
}

const Node* horizontal_page_listing(const Node& node, const Node& item) {
	if (node.kind != Kind::Submenu) {
		return nullptr;
	}
	if (node.layout == Layout::Horizontal) {
		for (const Node* kid : node.children) {
			if (kid == &item) {
				return &node;
			}
		}
	}
	for (const Node* kid : node.children) {
		if (kid == nullptr) {
			continue;
		}
		if (const Node* found = horizontal_page_listing(*kid, item); found != nullptr) {
			return found;
		}
	}
	return nullptr;
}

bool renders_horizontal(const Node& node, const MenuContext& ctx) {
	// With the feature off a horizontal page is a vertical list.
	return node.kind == Kind::Submenu && node.layout == Layout::Horizontal && ctx.horizontal_menus;
}

const Node* shortcut_landing_page(const MenuContext& ctx, const Node& target) {
	// `getCurrentHorizontalMenusChain` answers nothing with the feature off, so the item opens alone.
	if (!ctx.horizontal_menus) {
		return nullptr;
	}
	return shortcut_landing_page(ctx.root, target);
}

bool page_lists(const Node& page, const Node& column) {
	if (page.kind != Kind::Submenu) {
		return false;
	}
	// A group lists what its member pages list, not the member pages (`HorizontalMenuGroup::hasItem`,
	// `horizontal_menu_group.cpp:209-211`).
	if (page.is_group) {
		return std::ranges::any_of(page.children,
		                           [&](const Node* m) { return m != nullptr && page_lists(*m, column); });
	}
	return std::ranges::contains(page.children, &column);
}

const Node* shortcut_landing_page(MenuContext::Root root, const Node& target) {
	// `maybeGetParentMenu`'s two early answers: the start markers and the audio clip's waveform go
	// straight to the marker editor, the file selectors straight to the browser.
	constexpr const Node* kOpensItself[] = {&nodes::sample0_start, &nodes::sample1_start, &nodes::audio_clip_waveform,
	                                        &nodes::file0_selector, &nodes::file1_selector};
	if (std::ranges::find(kOpensItself, &target) != std::ranges::end(kOpensItself)) {
		return nullptr;
	}
	// `maybeGetParentMenu`'s search: the first chain page that lists the item.
	for (const Node* page : chain_for(root)) {
		if (page_lists(*page, target)) {
			return page;
		}
	}
	return nullptr;
}

bool shortcut_applies(const Node& target, const MenuContext& ctx) {
	if (&target == &nodes::file0_selector || &target == &nodes::file1_selector) {
		return true;
	}
	return bindings::relevant(target, ctx);
}

} // namespace deluge::scenes::menu
