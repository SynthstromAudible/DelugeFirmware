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
#include "gui/l10n/l10n.h"
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"
#include "model/clip/instrument_clip.h"
#include "model/sequencer/mute_sequencer.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::sequencer {

using namespace hid::display;

/// The mute sequencer belongs to the clip, so it applies to synth, MIDI and CV clips, and to whole kits (not rows)
inline bool isMuteSequencerEditable() {
	return getCurrentInstrumentClip() != nullptr && !soundEditor.editingKitRow();
}

/// Only valid while isMuteSequencerEditable(), which the menu items' relevance guarantees
inline MuteSequencer& currentMuteSequencer() {
	return getCurrentInstrumentClip()->muteSequencer;
}

class MuteSteps final : public IntegerWithOff {
public:
	using IntegerWithOff::IntegerWithOff;

	void readCurrentValue() override { this->setValue(currentMuteSequencer().getSteps()); }
	void writeCurrentValue() override { currentMuteSequencer().setSteps(this->getValue()); }

	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return MuteSequencer::kMaxSteps; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return NUMBER; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) const override {
		return isMuteSequencerEditable();
	}

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		if (this->getValue() != 0) {
			return IntegerWithOff::renderInHorizontalMenu(slot);
		}
		OLED::main.drawStringCentered("OFF", slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset,
		                              kTextTitleSpacingX, kTextTitleSizeY, slot.width);
	}

	void getNotificationValue(StringBuf& value) override {
		if (this->getValue() == 0) {
			value.append("OFF");
		}
		else {
			value.appendInt(this->getValue());
		}
	}
};

/// How many steps of the pattern are muted, shown as a fraction of the step count
class MuteCount final : public Integer {
public:
	using Integer::Integer;

	void readCurrentValue() override { this->setValue(currentMuteSequencer().getMutes()); }
	void writeCurrentValue() override { currentMuteSequencer().setMutes(this->getValue()); }

	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return currentMuteSequencer().getSteps(); }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return NUMBER; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) const override {
		return isMuteSequencerEditable() && currentMuteSequencer().isEnabled();
	}

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		DEF_STACK_STRING_BUF(fraction, 12);
		getNotificationValue(fraction);
		OLED::main.drawStringCentered(fraction, slot.start_x, slot.start_y + kHorizontalMenuSlotYOffset,
		                              kTextTitleSpacingX, kTextTitleSizeY, slot.width);
	}

	void getNotificationValue(StringBuf& value) override {
		value.appendInt(this->getValue());
		value.append('/');
		value.appendInt(getMaxValue());
	}

protected:
	void drawInteger(int32_t textWidth, int32_t textHeight, int32_t yPixel) override {
		DEF_STACK_STRING_BUF(fraction, 12);
		getNotificationValue(fraction);
		OLED::main.drawStringCentred(fraction.c_str(), yPixel + OLED_MAIN_TOPMOST_PIXEL, textWidth, textHeight);
	}

	[[nodiscard]] int32_t getOccupiedSlots() const override { return 2; }
};

/// Shifts the pattern to the left, so the clip starts that many steps into it
class MuteOffset final : public Integer {
public:
	using Integer::Integer;

	void readCurrentValue() override { this->setValue(currentMuteSequencer().getOffset()); }
	void writeCurrentValue() override { currentMuteSequencer().setOffset(this->getValue()); }

	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return currentMuteSequencer().getMaxOffset(); }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return NUMBER; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) const override {
		return isMuteSequencerEditable() && currentMuteSequencer().isEnabled();
	}
};

} // namespace deluge::gui::menu_item::sequencer
