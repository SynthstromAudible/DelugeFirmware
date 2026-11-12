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
#include "deluge/modulation/params/param.h"
#include "gui/menu_item/param.h"
#include "gui/menu_item/source/patched_param.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"
#include "util/functions.h"
#include <string>

namespace deluge::gui::menu_item::envelope {

/// The envelope mode of the sound being edited
inline modulation::EnvelopeMode currentEnvelopeMode(uint8_t envelopeIndex) {
	if (soundEditor.currentSound == nullptr) {
		return modulation::EnvelopeMode::ADSR;
	}
	return soundEditor.currentSound->envelopeConfig[envelopeIndex].mode;
}

/// One of an envelope's four params. AHD and AR modes reuse them for other jobs, so the name follows the mode.
class Segment : public source::PatchedParam {
public:
	Segment(l10n::String newName, l10n::String title, int32_t newP, uint8_t source_id)
	    : PatchedParam(newName, title, newP, source_id) {}

	[[nodiscard]] std::string_view getName() const override { return l10n::getView(strings().name); }

	[[nodiscard]] std::string_view getTitle() const override {
		title_ = l10n::get(strings().title);
		asterixToInt(title_.data(), source_id_ + 1);
		return title_;
	}

	void getColumnLabel(StringBuf& label) override { label.append(deluge::l10n::get(strings().shortName)); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) const override {
		using enum modulation::EnvelopeMode;
		auto base = menu_item::PatchedParam::getP();
		switch (currentEnvelopeMode(source_id_)) {
		case AHD:
			return base != modulation::params::LOCAL_ENV_0_SUSTAIN;
		case AR:
			return base != modulation::params::LOCAL_ENV_0_DECAY;
		default:
			return true;
		}
	}

private:
	struct Strings {
		l10n::String name;
		l10n::String shortName;
		l10n::String title;
	};

	[[nodiscard]] Strings strings() const {
		using namespace deluge::modulation;
		using enum l10n::String;
		EnvelopeMode mode = currentEnvelopeMode(source_id_);
		switch (menu_item::PatchedParam::getP()) {
		case params::LOCAL_ENV_0_ATTACK:
			return {STRING_FOR_ATTACK, STRING_FOR_ATTACK_SHORT, STRING_FOR_ENV_ATTACK_MENU_TITLE};
		case params::LOCAL_ENV_0_DECAY:
			if (mode == EnvelopeMode::AHD) {
				return {STRING_FOR_HOLD, STRING_FOR_HOLD_SHORT, STRING_FOR_ENV_HOLD_MENU_TITLE};
			}
			return {STRING_FOR_DECAY, STRING_FOR_DECAY_SHORT, STRING_FOR_ENV_DECAY_MENU_TITLE};
		case params::LOCAL_ENV_0_SUSTAIN:
			if (mode == EnvelopeMode::AR) {
				return {STRING_FOR_ENV_LEVEL, STRING_FOR_ENV_LEVEL_SHORT, STRING_FOR_ENV_LEVEL_MENU_TITLE};
			}
			return {STRING_FOR_SUSTAIN, STRING_FOR_SUSTAIN_SHORT, STRING_FOR_ENV_SUSTAIN_MENU_TITLE};
		case params::LOCAL_ENV_0_RELEASE:
			if (mode == EnvelopeMode::AHD) {
				// AHD decays at the release rate, so the release gold knob sets a drum's length
				return {STRING_FOR_DECAY, STRING_FOR_DECAY_SHORT, STRING_FOR_ENV_DECAY_MENU_TITLE};
			}
			return {STRING_FOR_RELEASE, STRING_FOR_RELEASE_SHORT, STRING_FOR_ENV_RELEASE_MENU_TITLE};
		}
		return {STRING_FOR_NONE, STRING_FOR_NONE, STRING_FOR_NONE};
	}

	mutable std::string title_;
};
} // namespace deluge::gui::menu_item::envelope
