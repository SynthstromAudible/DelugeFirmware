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
#include "instruments/classic/cables.h"
#include "instruments/classic/params.h"
#include "instruments/classic/words.h"
#include "plugin/remote_controls_publisher.h"
#include "plugin/types/remote_controls.h"

#include <algorithm>
#include <array>
#include <span>

namespace deluge::instruments::classic {

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
/// @return The slot: @p target, with a mod-button knob's press.
[[nodiscard]] constexpr plugin::RemoteSlot slot_for(plugin::ParamId target, const PressInputs& in) {
	namespace setting = effects::setting;
	plugin::RemoteSlot slot{.param = target, .press_param = std::nullopt};
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

namespace detail {
/// @brief @p pages with mod button 6's lower knob on @p lower (its press is that param's).
[[nodiscard]] constexpr std::array<plugin::RemotePage, 8> with_button6_lower(std::array<plugin::RemotePage, 8> pages,
                                                                             plugin::ParamId lower) {
	pages[6].slots[0] = slot_for(lower, {});
	return pages;
}
} // namespace detail

/// @brief A sample sound's device pages: the synth's, with mode 6's lower knob on pitch adjust (which also keeps its
///        value from being zeroed for having no knob).
inline constexpr std::array<plugin::RemotePage, 8> kSamplePages =
    detail::with_button6_lower(kDevicePages, param::kPitch);

/// @brief The filter knob's cycle on mod button 1: low-pass, high-pass, EQ (page ids 0, 1, 2).
inline constexpr std::array<plugin::RemotePage, 3> kFilterCycle{{
    detail::knob_page(1, 0, param::kLpfResonance, param::kLpfFrequency),
    detail::knob_page(1, 1, param::kHpfResonance, param::kHpfFrequency),
    detail::knob_page(1, 2, param::kBass, param::kTreble),
}};

/// @brief One mod button's two knobs, lower first. A cable's depth is its depth id (is_cable_depth), so no remap row,
///        which names plain params only, matches it.
struct KnobPair {
	plugin::ParamId lower; ///< the lower knob's target
	plugin::ParamId upper; ///< the upper knob's target
	[[nodiscard]] constexpr bool operator==(const KnobPair&) const = default;
};

/// @brief Every mod button's knob pair, button by button (one per page of kDevicePages).
using KnobPairs = std::array<KnobPair, 8>;
static_assert(KnobPairs{}.size() == kDevicePages.size());

/// @brief One knob pair a synth mode change rewrites: a pair whose lower knob is in @c lower_from and whose upper
///        knob is in @c upper_from becomes (@c lower_to, @c upper_to). Cable depths never match.
struct KnobRemap {
	std::span<const plugin::ParamId> lower_from;
	std::span<const plugin::ParamId> upper_from;
	plugin::ParamId lower_to;
	plugin::ParamId upper_to;
};

namespace detail {
inline constexpr std::array kLpfResonance{param::kLpfResonance};
inline constexpr std::array kHpfResonance{param::kHpfResonance};
inline constexpr std::array kBass{param::kBass};
/// Every filter knob's upper knob: a resonance or the bass pairs with any of them, a mixed pair included.
inline constexpr std::array kFilterUppers{param::kLpfFrequency, param::kHpfFrequency, param::kTreble};
inline constexpr std::array kModulator2Volume{param::kModulator2Volume};
inline constexpr std::array kModulator1Volume{param::kModulator1Volume};
} // namespace detail

/// @brief The rows applied going into FM: the filters' knobs lend themselves to the modulators' volumes (the second
///        modulator's on the lower knob, the first's on the upper) while FM has no filters, one row per filter
///        (low-pass, high-pass, EQ). Only the order with the resonance (or the bass) on the lower knob matches; a pair
///        with the cutoff on the lower knob stays. Delete them (and give FM a modulator page) when FM gains filters.
inline constexpr std::array kIntoFm{
    KnobRemap{.lower_from = detail::kLpfResonance,
              .upper_from = detail::kFilterUppers,
              .lower_to = param::kModulator2Volume,
              .upper_to = param::kModulator1Volume},
    KnobRemap{.lower_from = detail::kHpfResonance,
              .upper_from = detail::kFilterUppers,
              .lower_to = param::kModulator2Volume,
              .upper_to = param::kModulator1Volume},
    KnobRemap{.lower_from = detail::kBass,
              .upper_from = detail::kFilterUppers,
              .lower_to = param::kModulator2Volume,
              .upper_to = param::kModulator1Volume},
};

/// @brief The rows applied leaving FM: the modulators' volumes, on any button, go back to the low-pass filter. A
///        high-pass or EQ pair that went into FM comes back as the low-pass pair.
inline constexpr std::array kOutOfFm{
    KnobRemap{.lower_from = detail::kModulator2Volume,
              .upper_from = detail::kModulator1Volume,
              .lower_to = param::kLpfResonance,
              .upper_to = param::kLpfFrequency},
};

/// @brief @p knobs after a change from @p from to @p to: kIntoFm going into FM, kOutOfFm leaving it, and into or out of
///        DX7 mode a knob on oscillator A's volume, pulse width or pitch (or a cable's depth onto it) moves to the
///        DX7's own and back (kDx7Counterparts). Each pair takes the first row that matches it.
/// @param from  The synth mode before the change.
/// @param to    The synth mode after it.
/// @param knobs The sound's knobs, one pair per mod button.
/// @return The knobs the sound takes.
[[nodiscard]] KnobPairs remap_for_mode(SynthMode from, SynthMode to, KnobPairs knobs);

/// @brief Mode 7's knobs after oscillator @p osc (0 or 1) claims a wavetable. Oscillator 1 takes its wave index on the
///        upper knob and the first local LFO's depth on it on the lower knob, which keeps the second oscillator's wave
///        index when it holds that; oscillator 2 takes its wave index on the lower knob.
/// @param knobs The sound's knobs, one pair per mod button.
/// @param osc   The oscillator that claimed the wavetable, 0 or 1.
/// @return The knobs the sound takes.
[[nodiscard]] KnobPairs wavetable_claimed(KnobPairs knobs, uint8_t osc);

/// @brief Mode 7's knobs after oscillator @p osc (0 or 1) loads a sample in place of its wavetable: a knob on that
///        oscillator's wave index (or the depth of a cable to it) goes back to the bitcrusher (lower) or the sample
///        rate reduction (upper).
/// @param knobs The sound's knobs, one pair per mod button.
/// @param osc   The oscillator that now holds a sample, 0 or 1.
/// @return The knobs the sound takes.
[[nodiscard]] KnobPairs wavetable_released(KnobPairs knobs, uint8_t osc);

/// @brief The instrument's remote-controls record. Its device pages come from get and count (kDevicePages), never from
/// find. Its find serves the sound's filter cycle on any section: page ids 0 to 2 (group 0) are kFilterCycle, and ids 3
/// to 5 (group 1) are the same pages with the slots swapped, so find(s, 0) is the low-pass page on every section and
/// never that section's device page. So a sound's cycle on any section is the filter knob's: low-pass, high-pass, EQ
/// with the cutoff on the upper knob (group 0), then the same with it on the lower knob (group 1), as a sound may hold
/// its filter knob on either knob of any button. Each slot's press comes from decorate, which is slot_for with the
/// sound's FM mode read from the owner's state.
extern const plugin::RemoteControls
    kRemoteControls; // NOLINT(bugprone-dynamic-static-initializers): constant-initialized

} // namespace deluge::instruments::classic
