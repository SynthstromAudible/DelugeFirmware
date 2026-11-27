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

#include "host/chains/pages.h"

#include "effects/plugin.h"
#include "effects/setting_params.h"
#include "host/chains/params.h"

#include <array>
#include <optional>

namespace deluge::host::chains {

namespace {

/// The mod FX views: depth, feedback and offset.
constexpr uint32_t kModFxViews = 3;
/// The highest page id of any section: the song's last compressor page.
constexpr uint32_t kLastPageId = kCompPages;

namespace hc = param;
namespace setting = effects::setting;
using plugin::ParamId;
using plugin::PressAction;
using plugin::RemotePage;
using plugin::RemoteSlot;

/// A slot that turns the chain's param @p id.
RemoteSlot knob(ParamId id, PressAction press = PressAction::NONE, std::optional<ParamId> press_param = std::nullopt) {
	return {.param = id, .press = press, .press_param = press_param};
}

/// A slot that turns a compressor setting, pressing as @p press.
RemoteSlot comp_knob(ParamId id, PressAction press) {
	return {.param = id, .press = press, .press_param = std::nullopt};
}

/// A slot that turns the chain's param @p id, and whose press steps setting @p setting_id.
RemoteSlot step(ParamId id, ParamId setting_id) {
	return knob(id, PressAction::STEP_SETTING, setting_id);
}

/// Page @p page_id of mod button @p section, in group @p group: slot 0 the lower knob, slot 1 the upper.
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

RemotePage mod_fx_page(uint32_t mod_fx_param) {
	constexpr std::array<ParamId, kModFxViews> kLower{hc::kModFXDepth, hc::kModFXFeedback, hc::kModFXOffset};
	constexpr uint32_t kFeedback = 1;
	const uint32_t view = mod_fx_param < kLower.size() ? mod_fx_param : kFeedback;
	return page(kModFxSection, view, 0, knob(kLower[view], PressAction::NEXT_PAGE),
	            step(hc::kModFXRate, setting::kModFXType));
}

namespace {

/// The one page of a section that has a single page; none for the envelopes' button 2.
std::optional<RemotePage> single_page(uint32_t section, const PageInputs& in) {
	switch (section) {
	case kLevelSection:
		return page(kLevelSection, 0, 0, knob(hc::kPan), knob(hc::kVolume));
	case kDelaySection:
		return page(kDelaySection, 0, 0,
		            step(hc::kDelayFeedback, in.alt_delay ? setting::kDelaySyncLevel : setting::kDelayType),
		            step(hc::kDelayRate, in.alt_delay ? setting::kDelaySyncType : setting::kDelayPingPong));
	case kCompressorSection: {
		// A clip chain's stutter outlives a press of its reverb button (main's
		// `GlobalEffectableForClip::modButtonAction`); the song's reverb button has its own pages (song_reverb_page),
		// which do not keep it.
		RemotePage out = page(kCompressorSection, 0, 0, knob(hc::kReverbAmount, PressAction::REVERB_PRESET),
		                      step(hc::kSidechainCompressorVolume, setting::kSidechainSyncLevel));
		out.keeps_stutter = true;
		return out;
	}
	case kStutterSection:
		return page(kStutterSection, 0, 0, in.song ? RemoteSlot{} : knob(hc::kPitchAdjust),
		            knob(hc::kStutterRate, PressAction::HOLD_STUTTER));
	case kLofiSection:
		return page(kLofiSection, 0, 0, knob(hc::kBitcrushAmount), knob(hc::kSampleRateReduction));
	default:
		return std::nullopt;
	}
}

/// The filter button's page @p page_id: low-pass, high-pass, EQ.
std::optional<RemotePage> filter_page(uint32_t page_id) {
	switch (page_id) {
	case 0:
		return page(kFilterSection, 0, 0, step(hc::kLpfResonance, setting::kLpfMode),
		            knob(hc::kLpfFrequency, PressAction::NEXT_PAGE));
	case 1:
		return page(kFilterSection, 1, 0, step(hc::kHpfResonance, setting::kHpfMode),
		            knob(hc::kHpfFrequency, PressAction::NEXT_PAGE));
	case 2:
		return page(kFilterSection, 2, 0, knob(hc::kBass), knob(hc::kTreble, PressAction::NEXT_PAGE));
	default:
		return std::nullopt;
	}
}

/// The song's reverb button's page @p page_id: the reverb page (group 0), then one page per compressor setting
/// (group 1), the threshold knob toggling between the groups.
std::optional<RemotePage> song_reverb_page(uint32_t page_id) {
	const RemoteSlot threshold = comp_knob(setting::kCompThreshold, PressAction::TOGGLE_GROUP);
	if (page_id == 0) {
		return page(kCompressorSection, 0, 0, knob(hc::kReverbAmount, PressAction::REVERB_PRESET), threshold);
	}
	constexpr std::array<ParamId, kCompPages> kComp{setting::kCompRatio, setting::kCompAttack, setting::kCompRelease,
	                                                setting::kCompHpf, setting::kCompBlend};
	if (page_id > kComp.size()) {
		return std::nullopt;
	}
	return page(kCompressorSection, page_id, 1, comp_knob(kComp[page_id - 1], PressAction::NEXT_PAGE), threshold);
}

} // namespace

std::optional<RemotePage> section_page(uint32_t section, uint32_t page_id, const PageInputs& in) {
	if (section == kFilterSection) {
		return filter_page(page_id);
	}
	if (section == kCompressorSection && in.song) {
		return song_reverb_page(page_id);
	}
	if (section == kModFxSection) {
		return page_id < kModFxViews && mod_fx_page_valid(in.mod_fx_type, page_id) ? std::optional{mod_fx_page(page_id)}
		                                                                           : std::nullopt;
	}
	return page_id == 0 ? single_page(section, in) : std::nullopt;
}

std::vector<RemotePage> section_pages(uint32_t section, const PageInputs& in) {
	std::vector<RemotePage> pages;
	for (uint32_t page_id = 0; page_id <= kLastPageId; page_id++) {
		if (const std::optional<RemotePage> p = section_page(section, page_id, in)) {
			pages.push_back(*p);
		}
	}
	return pages;
}

namespace {

/// The inputs a chain's pages follow, read from the context.
PageInputs inputs_of(const plugin::PageContext& ctx) {
	PageInputs in{.song = ctx.song, .alt_delay = ctx.alt_delay};
	if (ctx.state != nullptr) {
		if (const effects::EffectStates* states = ctx.state->as<effects::DelugeEffects>()) {
			in.mod_fx_type = states->mod_fx.type;
		}
	}
	return in;
}

/// The device pages in section order, then page id: the one at @p index, or none past the end.
std::optional<RemotePage> nth_page(const PageInputs& in, uint32_t index) {
	for (uint32_t section = 0; section < kSections; section++) {
		for (uint32_t page_id = 0; page_id <= kLastPageId; page_id++) {
			if (const std::optional<RemotePage> p = section_page(section, page_id, in)) {
				if (index == 0) {
					return p;
				}
				index--;
			}
		}
	}
	return std::nullopt;
}

uint32_t count_pages(const plugin::PageContext& ctx) {
	const PageInputs in = inputs_of(ctx);
	uint32_t n = 0;
	for (uint32_t section = 0; section < kSections; section++) {
		for (uint32_t page_id = 0; page_id <= kLastPageId; page_id++) {
			n += section_page(section, page_id, in).has_value() ? 1u : 0u;
		}
	}
	return n;
}

bool get_page(const plugin::PageContext& ctx, uint32_t index, RemotePage& out) {
	const std::optional<RemotePage> p = nth_page(inputs_of(ctx), index);
	if (p) {
		out = *p;
	}
	return p.has_value();
}

bool find_page(const plugin::PageContext& ctx, uint32_t section, uint32_t page_id, RemotePage& out) {
	if (section == kModFxSection) {
		out = mod_fx_page(page_id);
		return true;
	}
	const std::optional<RemotePage> p = section_page(section, page_id, inputs_of(ctx));
	if (p) {
		out = *p;
	}
	return p.has_value();
}

RemoteSlot decorate_slot(const plugin::PageContext& ctx, ParamId param) {
	const PageInputs in = inputs_of(ctx);
	for (uint32_t section = 0; section < kSections; section++) {
		for (uint32_t page_id = 0; page_id <= kLastPageId; page_id++) {
			if (const std::optional<RemotePage> p = section_page(section, page_id, in)) {
				for (const RemoteSlot& slot : p->slots) {
					if (slot.param == param) {
						return slot;
					}
				}
			}
		}
	}
	return knob(param);
}

} // namespace

const plugin::RemoteControls kRemoteControls{.plugin = plugin::kGlobalEffectable,
                                             .count = &count_pages,
                                             .get = &get_page,
                                             .find = &find_page,
                                             .decorate = &decorate_slot};

} // namespace deluge::host::chains
