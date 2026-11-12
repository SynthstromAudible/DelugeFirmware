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
#include "definitions_cxx.hpp"
#include "gui/menu_item/formatted_title.h"
#include "gui/menu_item/integer.h"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item::envelope {

/// Applies `change` to the envelope config of the sound being edited, or of every sound in the kit when the
/// affect-entire button is held
template <typename Change>
void changeEnvelopeConfig(uint8_t envelopeIndex, Change change) {
	if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {
		Kit* kit = getCurrentKit();
		for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
			if (thisDrum->type == DrumType::SOUND) {
				change(static_cast<SoundDrum*>(thisDrum)->envelopeConfig[envelopeIndex]);
			}
		}
	}
	else {
		change(soundEditor.currentSound->envelopeConfig[envelopeIndex]);
	}
}

class Mode final : public Selection, FormattedTitle {
public:
	Mode(l10n::String name, l10n::String title, uint8_t envelopeIndex)
	    : Selection(name, title), FormattedTitle(title, envelopeIndex + 1), envelopeIndex_(envelopeIndex) {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		using enum l10n::String;
		return {l10n::getView(STRING_FOR_ADSR), l10n::getView(STRING_FOR_AHD), l10n::getView(STRING_FOR_AR),
		        l10n::getView(STRING_FOR_MPE)};
	}

	void readCurrentValue() override { this->setValue(soundEditor.currentSound->envelopeConfig[envelopeIndex_].mode); }
	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		auto mode = this->getValue<modulation::EnvelopeMode>();
		changeEnvelopeConfig(envelopeIndex_, [mode](modulation::EnvelopeConfig& config) { config.mode = mode; });
	}

private:
	uint8_t envelopeIndex_;
};

/// What an MPE-mode envelope follows: pressure (Z) or mod wheel (Y)
class Follow final : public Selection, FormattedTitle {
public:
	Follow(l10n::String name, l10n::String title, uint8_t envelopeIndex)
	    : Selection(name, title), FormattedTitle(title, envelopeIndex + 1), envelopeIndex_(envelopeIndex) {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		using enum l10n::String;
		bool shortOpt = optType == OptType::SHORT;
		return {l10n::getView(shortOpt ? STRING_FOR_ENV_FOLLOW_Z_SHORT : STRING_FOR_ENV_FOLLOW_Z),
		        l10n::getView(shortOpt ? STRING_FOR_ENV_FOLLOW_Y_SHORT : STRING_FOR_ENV_FOLLOW_Y)};
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) const override {
		return soundEditor.currentSound != nullptr
		       && soundEditor.currentSound->envelopeConfig[envelopeIndex_].mode == modulation::EnvelopeMode::MPE;
	}

	void readCurrentValue() override {
		this->setValue(soundEditor.currentSound->envelopeConfig[envelopeIndex_].follow);
	}
	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		auto follow = this->getValue<modulation::EnvelopeFollow>();
		changeEnvelopeConfig(envelopeIndex_, [follow](modulation::EnvelopeConfig& config) { config.follow = follow; });
	}

private:
	uint8_t envelopeIndex_;
};

/// One curve for attack, decay and release: 0 is linear, positive exponential, negative the other way
class Curve final : public Integer, FormattedTitle {
public:
	Curve(l10n::String name, l10n::String title, uint8_t envelopeIndex)
	    : Integer(name, title), FormattedTitle(title, envelopeIndex + 1), envelopeIndex_(envelopeIndex) {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	[[nodiscard]] int32_t getMinValue() const override { return modulation::EnvelopeShape::kMinCurve; }
	[[nodiscard]] int32_t getMaxValue() const override { return modulation::EnvelopeShape::kMaxCurve; }
	// Bipolar with a meaningful zero (linear) and default (32, classic), so show the number rather than a knob
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return NUMBER; }

	void readCurrentValue() override { this->setValue(soundEditor.currentSound->envelopeConfig[envelopeIndex_].curve); }
	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		int32_t curve = this->getValue();
		changeEnvelopeConfig(envelopeIndex_, [curve](modulation::EnvelopeConfig& config) { config.setCurve(curve); });
	}

private:
	uint8_t envelopeIndex_;
};

} // namespace deluge::gui::menu_item::envelope
