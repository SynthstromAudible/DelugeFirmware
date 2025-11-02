/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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

#include "gui/menu_item/number.h"
#include "gui/ui/sound_editor.h"
#include "model/mod_controllable/filters/filter_config.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "modulation/patch/patch_cable_set.h"

namespace deluge::gui::menu_item::filter {

enum class FilterSlot : uint8_t {
	LPF,
	HPF,
};

enum class FilterParamType : uint8_t {
	FREQUENCY,
	RESONANCE,
	MORPH,
	MODE,
};

class FilterInfo {
public:
	FilterInfo(FilterSlot slot_, FilterParamType type_) : slot{slot_}, type{type_} {}
	::FilterMode getMode() const {
		if (slot == FilterSlot::LPF) {
			return soundEditor.currentModControllable->lpfMode;
		}
		else {
			return soundEditor.currentModControllable->hpfMode;
		}
	}
	int32_t getModeValue() const {
		if (slot == FilterSlot::HPF) {
			return util::to_underlying(soundEditor.currentModControllable->hpfMode) - kFirstHPFMode;
		}
		else {
			// Off is located past the HPFLadder, which isn't an option for the low pass filter (should it be?)
			int32_t selection = util::to_underlying(soundEditor.currentModControllable->lpfMode);
			return std::min(selection, kNumLPFModes);
		}
	}
	void setMode(int32_t value) const { setModeForModControllable(value, soundEditor.currentModControllable); }
	void setModeForModControllable(int32_t value, ModControllableAudio* modControllable) const {
		if (slot == FilterSlot::HPF) {
			modControllable->hpfMode = static_cast<FilterMode>(value + kFirstHPFMode);
		}
		else {
			// num lpf modes counts off but there's HPF modes in the middle
			if (value >= kNumLPFModes) {
				modControllable->lpfMode = FilterMode::OFF;
			}
			else {
				modControllable->lpfMode = static_cast<FilterMode>(value);
			}
		}
	}
	FilterParamType getFilterParamType() const { return type; }
	FilterSlot getSlot() const { return slot; }
	/// Returns morphname for morph parameters, and the alt argument for others.
	[[nodiscard]] std::string_view getMorphNameOr(std::string_view alt, bool shortName = false) const {
		if (type == FilterParamType::MORPH) {
			using enum l10n::String;
			auto filt = dsp::filter::SpecificFilter(getMode());
			return l10n::getView(filt.getMorphName(shortName));
		}
		return alt;
	}
	[[nodiscard]] bool isMorphable() const {
		auto filter = dsp::filter::SpecificFilter(getMode());
		return filter.getFamily() == dsp::filter::FilterFamily::SVF;
	}

	bool isOn() const { return getMode() != ::FilterMode::OFF; }

private:
	FilterSlot slot;
	FilterParamType type;
};

static_assert(sizeof(FilterInfo) <= 4);

} // namespace deluge::gui::menu_item::filter
