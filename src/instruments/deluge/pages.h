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

#pragma once

/// @file
/// @brief The Deluge instrument's remote-controls pages: the knobs a new sound starts with, one page per mod button,
///        and the filter knob's cycle.

#include "effects/setting_params.h"
#include "instruments/deluge/cables.h"
#include "instruments/deluge/params.h"
#include "plugin-api/remote_controls_publisher.h"
#include "plugin-api/types/remote_controls.h"

#include <algorithm>
#include <array>

namespace deluge::instrument {

static_assert(std::ranges::all_of(kParams,
                                  [](const plugin::ParamInfo& p) { return p.id.value < effects::kFirstSettingId; }),
              "the instrument's param ids stay below the setting ids");
static_assert(kCableDepthFlag > effects::kLastSettingId, "a cable depth's id stays above the setting ids");

/// @brief What changes a slot's press.
struct PressInputs {
	bool fm{false};        ///< the sound is FM: its filter knob does not cycle
	bool alt_delay{false}; ///< the alternative delay knobs: the delay presses step the sync settings
};

/// @brief Whether @p id is the depth of a cable from the sidechain alone (not a depth-of-depth).
/// @param id A param id of the instrument's table.
/// @return True for a level-1 cable whose source is the sidechain.
[[nodiscard]] constexpr bool is_sidechain_cable(plugin::ParamId id) {
	if (!is_cable_depth(id) || detail::depth_level(id.value) != 1) {
		return false;
	}
	const std::optional<CableRoute> route = cable_route(id);
	return route && route->source == PatchSource::SIDECHAIN;
}

/// @brief The slot that turns @p target and what its press does.
/// @param target The param the knob moves.
/// @param in     What changes the press.
/// @return The slot: the kind names the sound's collection holding @p target (a cable's depth, a patched param, or an
///         unpatched one); the press is a mod-button knob's press.
[[nodiscard]] constexpr plugin::RemoteSlot slot_for(plugin::ParamId target, const PressInputs& in) {
	namespace setting = effects::setting;
	plugin::RemoteSlot slot{.param = target, .press_param = std::nullopt};
	if (is_cable_depth(target)) {
		slot.kind = plugin::ParamKind::PATCH_CABLE;
	}
	else {
		slot.kind = target.value < kUnpatchedFirstId ? plugin::ParamKind::PATCHED : plugin::ParamKind::UNPATCHED_SOUND;
	}
	const auto step = [&slot](plugin::ParamId setting_id) {
		slot.press = plugin::PressAction::STEP_SETTING;
		slot.press_param = setting_id;
	};
	if (target == param::kStutterRate) {
		slot.press = plugin::PressAction::HOLD_STUTTER;
	}
	else if (target == param::kDelayRate) {
		step(in.alt_delay ? setting::kDelaySyncType : setting::kDelayPingPong);
	}
	else if (target == param::kDelayFeedback) {
		step(in.alt_delay ? setting::kDelaySyncLevel : setting::kDelayType);
	}
	else if (target == param::kLpfResonance) {
		step(setting::kLpfMode);
	}
	else if (target == param::kHpfResonance) {
		step(setting::kHpfMode);
	}
	else if (target == param::kReverbAmount) {
		slot.press = plugin::PressAction::REVERB_PRESET;
	}
	else if (is_sidechain_cable(target)) {
		step(setting::kSidechainSyncLevel);
	}
	else if ((target == param::kLpfFrequency || target == param::kHpfFrequency || target == param::kTreble) && !in.fm) {
		slot.press = plugin::PressAction::NEXT_PAGE;
	}
	return slot;
}

namespace detail {
/// @brief A page of mod button @p section whose lower knob moves @p lower and upper knob @p upper.
[[nodiscard]] constexpr plugin::RemotePage knob_page(uint32_t section, uint32_t page_id, plugin::ParamId lower,
                                                     plugin::ParamId upper) {
	plugin::RemotePage page{.section = section, .page_id = page_id, .name = {}};
	page.slots[0] = slot_for(lower, {});
	page.slots[1] = slot_for(upper, {});
	return page;
}
} // namespace detail

/// @brief The default knobs, one page per mod button (section = button; slot 0 the lower knob, slot 1 the upper).
/// A sound's own knobs are its saved ones, not these.
inline constexpr std::array<plugin::RemotePage, 8> kDevicePages{{
    detail::knob_page(0, 0, param::kPan, param::kVolumePostFX),
    detail::knob_page(1, 0, param::kLpfResonance, param::kLpfFrequency),
    detail::knob_page(2, 0, param::kEnv1Release, param::kEnv1Attack),
    detail::knob_page(3, 0, param::kDelayFeedback, param::kDelayRate),
    detail::knob_page(4, 0, param::kReverbAmount,
                      *cable_depth_id(PatchSource::SIDECHAIN, param::kVolumePostReverbSend)),
    detail::knob_page(5, 0, *cable_depth_id(PatchSource::LFO_GLOBAL_1, param::kPitch), param::kLfo1Rate),
    detail::knob_page(6, 0, param::kPortamento, param::kStutterRate),
    detail::knob_page(7, 0, param::kBitcrushAmount, param::kSampleRateReduction),
}};

/// @brief The filter knob's cycle on mod button 1: low-pass, high-pass, EQ (page ids 0, 1, 2).
inline constexpr std::array<plugin::RemotePage, 3> kFilterCycle{{
    detail::knob_page(1, 0, param::kLpfResonance, param::kLpfFrequency),
    detail::knob_page(1, 1, param::kHpfResonance, param::kHpfFrequency),
    detail::knob_page(1, 2, param::kBass, param::kTreble),
}};

/// @brief The instrument's remote-controls record. Its device pages are kDevicePages; its find serves the sound's
/// filter cycle on any section: page ids 0 to 2 (group 0) are kFilterCycle, ids 3 to 5 (group 1) the same with the
/// slots swapped. Each slot's press, by decorate, is slot_for with the sound's FM mode read from the owner's state.
extern const plugin::RemoteControls
    kRemoteControls; // NOLINT(bugprone-dynamic-static-initializers): constant-initialized

} // namespace deluge::instrument
