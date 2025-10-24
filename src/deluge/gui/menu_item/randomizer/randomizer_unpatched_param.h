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
#include "gui/menu_item/unpatched_param.h"
#include "gui/ui/sound_editor.h"

#include <util/comparison.h>

namespace deluge::gui::menu_item::randomizer {
class RandomizerUnpatchedParam : public UnpatchedParam {
public:
	using UnpatchedParam::UnpatchedParam;

	RandomizerUnpatchedParam(l10n::String newName, l10n::String title, int32_t newP, RenderingStyle style)
	    : UnpatchedParam(newName, title, newP), style_(style) {}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		if (soundEditor.editingCVOrMIDIClip() || soundEditor.editingNonAudioDrumRow()) {
			return false;
		}

		using namespace modulation::params;
		const auto p = static_cast<UnpatchedShared>(getP());
		const bool isGlobal =
		    util::one_of(p, {UNPATCHED_SPREAD_VELOCITY, UNPATCHED_NOTE_PROBABILITY, UNPATCHED_REVERSE_PROBABILITY});

		return isGlobal || soundEditor.currentArpSettings->mode != ArpMode::OFF;
	}

	void getColumnLabel(StringBuf& label) override {
		label.append(deluge::l10n::get(deluge::l10n::built_in::seven_segment, this->name));
	}

	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return style_; }

private:
	RenderingStyle style_ = KNOB;
};

class RandomizerSoundOnlyUnpatchedParam final : public UnpatchedParam {
public:
	using UnpatchedParam::UnpatchedParam;
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return !soundEditor.editingCVOrMIDIClip() && !soundEditor.editingKitAffectEntire()
		       && !soundEditor.editingNonAudioDrumRow() && soundEditor.currentArpSettings->mode != ArpMode::OFF;
	}
	void getColumnLabel(StringBuf& label) override {
		label.append(deluge::l10n::getView(deluge::l10n::built_in::seven_segment, this->name).data());
	}
};

class RandomizerNonKitSoundUnpatchedParam final : public UnpatchedParam {
public:
	using UnpatchedParam::UnpatchedParam;

	RandomizerNonKitSoundUnpatchedParam(l10n::String newName, l10n::String title, int32_t newP, RenderingStyle style)
	    : UnpatchedParam(newName, title, newP), style_(style) {}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return !soundEditor.editingCVOrMIDIClip() && !soundEditor.editingKit()
		       && soundEditor.currentArpSettings->mode != ArpMode::OFF;
	}
	void getColumnLabel(StringBuf& label) override {
		label.append(deluge::l10n::get(deluge::l10n::built_in::seven_segment, this->name));
	}
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return style_; }

private:
	RenderingStyle style_ = KNOB;
};

} // namespace deluge::gui::menu_item::randomizer
