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

#include "effects/global_effectable/pages.h"

#include "effects/global_effectable/params.h"
#include "effects/setting_params.h"

#include <array>

namespace deluge::global_effectable {

namespace {

namespace ge = param;
namespace setting = effects::setting;
using plugin::ParamId;
using plugin::PressAction;
using plugin::RemotePage;
using plugin::RemoteSlot;

/// A slot that turns the chain's param @p id.
RemoteSlot knob(ParamId id, PressAction press = PressAction::NONE, std::optional<ParamId> press_param = std::nullopt) {
	return {.param = id, .kind = plugin::ParamKind::UNPATCHED_GLOBAL, .press = press, .press_param = press_param};
}

/// A slot that turns a compressor setting, pressing as @p press.
RemoteSlot comp_knob(ParamId id, PressAction press) {
	return {.param = id, .kind = plugin::ParamKind::NONE, .press = press, .press_param = std::nullopt};
}

RemoteSlot step(ParamId id, ParamId setting_id) {
	return knob(id, PressAction::STEP_SETTING, setting_id);
}

RemotePage page(uint32_t section, uint32_t page_id, uint8_t group, RemoteSlot lower, RemoteSlot upper) {
	RemotePage out{.section = section, .page_id = page_id, .group = group, .name = {}};
	out.slots[0] = lower;
	out.slots[1] = upper;
	return out;
}

} // namespace

bool mod_fx_page_valid(effects::ModFXType type, uint32_t mod_fx_param) {
	switch (mod_fx_param) {
	case 0: // depth
		return type != effects::ModFXType::FLANGER;
	case 2: // offset
		return type == effects::ModFXType::CHORUS || type == effects::ModFXType::CHORUS_STEREO
		       || type == effects::ModFXType::GRAIN;
	default:
		return type != effects::ModFXType::CHORUS && type != effects::ModFXType::CHORUS_STEREO;
	}
}

std::vector<RemotePage> section_pages(uint32_t section, const PageInputs& in) {
	switch (section) {
	case 0:
		return {page(0, 0, 0, knob(ge::kPan), knob(ge::kVolume))};
	case 1:
		return {
		    page(1, 0, 0, step(ge::kLpfResonance, setting::kLpfMode), knob(ge::kLpfFrequency, PressAction::NEXT_PAGE)),
		    page(1, 1, 0, step(ge::kHpfResonance, setting::kHpfMode), knob(ge::kHpfFrequency, PressAction::NEXT_PAGE)),
		    page(1, 2, 0, knob(ge::kBass), knob(ge::kTreble, PressAction::NEXT_PAGE)),
		};
	case 3:
		return {page(3, 0, 0, step(ge::kDelayFeedback, in.alt_delay ? setting::kDelaySyncLevel : setting::kDelayType),
		             step(ge::kDelayRate, in.alt_delay ? setting::kDelaySyncType : setting::kDelayPingPong))};
	case 4: {
		const RemoteSlot reverb = knob(ge::kReverbAmount, PressAction::REVERB_PRESET);
		if (!in.song) {
			return {page(4, 0, 0, reverb, step(ge::kSidechainCompressorVolume, setting::kSidechainSyncLevel))};
		}
		const RemoteSlot threshold = comp_knob(setting::kCompThreshold, PressAction::TOGGLE_GROUP);
		std::vector<RemotePage> pages{page(4, 0, 0, reverb, threshold)};
		constexpr std::array<ParamId, 5> kComp{setting::kCompRatio, setting::kCompAttack, setting::kCompRelease,
		                                       setting::kCompHpf, setting::kCompBlend};
		for (uint32_t i = 0; i < kComp.size(); i++) {
			pages.push_back(page(4, i, 1, comp_knob(kComp[i], PressAction::NEXT_PAGE), threshold));
		}
		return pages;
	}
	case 5: {
		constexpr std::array<ParamId, 3> kLower{ge::kModFXDepth, ge::kModFXFeedback, ge::kModFXOffset};
		std::vector<RemotePage> pages;
		for (uint32_t i = 0; i < kLower.size(); i++) {
			if (mod_fx_page_valid(in.mod_fx_type, i)) {
				pages.push_back(
				    page(5, i, 0, knob(kLower[i], PressAction::NEXT_PAGE), step(ge::kModFXRate, setting::kModFXType)));
			}
		}
		return pages;
	}
	case 6:
		return {page(6, 0, 0, in.song ? RemoteSlot{} : knob(ge::kPitchAdjust),
		             knob(ge::kStutterRate, PressAction::HOLD_STUTTER))};
	case 7:
		return {page(7, 0, 0, knob(ge::kBitcrushAmount), knob(ge::kSampleRateReduction))};
	default: // 2: the envelopes, which a chain has none of
		return {};
	}
}

} // namespace deluge::global_effectable
