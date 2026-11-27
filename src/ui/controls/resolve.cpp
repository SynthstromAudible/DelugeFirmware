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

#include "ui/controls/resolve.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace deluge::ui::controls {

namespace {

using plugin::PressAction;

/// The slot at @p slot of @p pages's current page, or nullptr out of range.
const Slot* slot_at(const SectionPages& pages, int32_t slot) {
	if (slot < 0 || std::cmp_greater_equal(slot, pages.current.slots.size())) {
		return nullptr;
	}
	return &pages.current.slots[static_cast<size_t>(slot)];
}

/// The cycle's first page of the current page's group whose id follows the current page's, wrapping round: where a
/// press goes from a page the cycle lacks (a mod FX view its type has no param for). Every slot moves to it.
Target following_page(const SectionPages& pages) {
	const Page* first = nullptr;
	const Page* after = nullptr;
	for (const Page& p : pages.cycle) {
		if (p.group != pages.current.group) {
			continue;
		}
		if (first == nullptr || p.page_id < first->page_id) {
			first = &p;
		}
		if (p.page_id > pages.current.page_id && (after == nullptr || p.page_id < after->page_id)) {
			after = &p;
		}
	}
	const Page* to = after != nullptr ? after : first;
	if (to == nullptr) {
		return {};
	}
	return SelectPage{.section = pages.current.section, .page_id = to->page_id, .slots = to->slots};
}

/// The page after the one holding the current page's slot @p k, in its group: the pressed slot moves to it, and each
/// other slot that sits where the found page has it moves with it. A slot the user mapped elsewhere stays.
Target next_page(const SectionPages& pages, size_t k) {
	const std::vector<Page>& cycle = pages.cycle;
	const std::optional<project::ParamTarget>& pressed = pages.current.slots[k].target;
	if (!pressed) {
		return {};
	}
	const auto found = std::ranges::find_if(cycle, [&](const Page& p) { return p.slots[k].target == pressed; });
	if (found == cycle.end()) {
		return following_page(pages);
	}
	const auto i = static_cast<size_t>(found - cycle.begin());
	size_t j = i;
	do {
		j = (j + 1) % cycle.size();
	} while (cycle[j].group != cycle[i].group);
	SelectPage out{.section = pages.current.section, .page_id = cycle[j].page_id, .slots = pages.current.slots};
	for (size_t p = 0; p < out.slots.size(); p++) {
		if (p == k || pages.current.slots[p].target == cycle[i].slots[p].target) {
			out.slots[p] = cycle[j].slots[p];
		}
	}
	return out;
}

/// The other group's entry page.
Target toggle_group(const SectionPages& pages) {
	if (pages.current.group > 1) {
		return {};
	}
	const uint32_t entry = pages.group_entry[1 - pages.current.group];
	const auto found = std::ranges::find_if(pages.cycle, [&](const Page& p) { return p.page_id == entry; });
	if (found == pages.cycle.end()) {
		return {};
	}
	return SelectPage{.section = pages.current.section, .page_id = found->page_id, .slots = found->slots};
}

} // namespace

Target resolve_turn(const KnobFocus& focus, const SectionPages& pages, int32_t slot) {
	const Slot* at = slot_at(pages, slot);
	const std::optional<project::ParamAddress> owner = knob_owner(focus);
	if (at == nullptr || !at->target || !owner) {
		return {};
	}
	return ParamWrite{.address = project::ParamAddress::of(owner->owner, *at->target, owner->clip_id)};
}

Target resolve_press(const KnobFocus& focus, const SectionPages& pages, int32_t slot, bool on) {
	const Slot* at = slot_at(pages, slot);
	if (at == nullptr) {
		return {};
	}
	if (!on) {
		switch (at->press) {
		case PressAction::HOLD_STUTTER:
			return HostCommand::STUTTER_END;
		case PressAction::MIDI_CC_SELECT:
			return HostCommand::MIDI_CC_SELECT_OFF;
		default:
			return {};
		}
	}
	switch (at->press) {
	case PressAction::STEP_SETTING: {
		const std::optional<project::ParamAddress> owner = knob_owner(focus);
		if (!owner || !at->press_param) {
			return {};
		}
		return StepSetting{
		    .address = project::ParamAddress::of(
		        owner->owner, {.space = project::ParamSpace::ENGINE, .id = *at->press_param}, owner->clip_id)};
	}
	case PressAction::HOLD_STUTTER:
		return HostCommand::STUTTER_BEGIN;
	case PressAction::MIDI_CC_SELECT:
		return HostCommand::MIDI_CC_SELECT_ON;
	case PressAction::REVERB_PRESET:
		return HostCommand::REVERB_PRESET;
	case PressAction::NEXT_PAGE:
		return next_page(pages, static_cast<size_t>(slot));
	case PressAction::TOGGLE_GROUP:
		return toggle_group(pages);
	case PressAction::NONE:
		break;
	}
	return {};
}

} // namespace deluge::ui::controls
