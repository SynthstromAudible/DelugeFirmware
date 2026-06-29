/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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
#include "gui/menu_item/integer.h"
#include "gui/menu_item/midi/command.h"
#include "gui/menu_item/toggle.h"
#include "hid/display/oled.h"
#include "io/midi/midi_fanout.h"
#include <string>

namespace deluge::gui::menu_item::midi {

// The learnable source CC of one MIDI fan-out group
class FanOutSource final : public Command {
public:
	FanOutSource(l10n::String newName, int32_t newGroup) : Command(newName), group(newGroup) {}
	[[nodiscard]] LearnedMIDI& learned() const override { return MIDIFanOut::groups[group].source; }
	bool learnNoteOn(MIDICable& cable, int32_t channel, int32_t noteCode) override {
		bool learnt = Command::learnNoteOn(cable, channel, noteCode);
		MIDIFanOut::markDirty();
		return learnt;
	}
	void unlearnAction() override {
		Command::unlearnAction();
		MIDIFanOut::markDirty();
	}
	void selectEncoderAction(int32_t offset) override {
		Command::selectEncoderAction(offset);
		MIDIFanOut::markDirty();
	}

private:
	int32_t group;
};

// The destination CC number of one fan-out dest slot. Menu value 0 = OFF, 1..kMaxDestCC+1 = CC 0..kMaxDestCC.
class FanOutDest final : public IntegerWithOff {
public:
	FanOutDest(l10n::String newName, int32_t newGroup, int32_t newSlot)
	    : IntegerWithOff(newName), group(newGroup), slot(newSlot) {}
	[[nodiscard]] int32_t getMaxValue() const override { return MIDIFanOut::kMaxDestCC + 1; }
	// Show the CC number (or OFF) as text instead of a dial, unlike Base/Depth.
	void renderInHorizontalMenu(const SlotPosition& pos) override {
		deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;
		const std::string str = (this->getValue() == 0) ? "OFF" : std::to_string(getDisplayValue());
		image.drawStringCentered(str.data(), pos.start_x, pos.start_y + kHorizontalMenuSlotYOffset, kTextTitleSpacingX,
		                         kTextTitleSizeY, pos.width);
	}
	void readCurrentValue() override {
		uint8_t cc = MIDIFanOut::groups[group].dests[slot].cc;
		this->setValue((cc == MIDIFanOut::kDestCCNone) ? 0 : cc + 1);
	}
	void writeCurrentValue() override {
		int32_t value = this->getValue();
		uint8_t cc = (value == 0) ? MIDIFanOut::kDestCCNone : value - 1;
		if (cc != MIDIFanOut::groups[group].dests[slot].cc) {
			MIDIFanOut::groups[group].dests[slot].cc = cc;
			MIDIFanOut::markDirty();
		}
	}

protected:
	int32_t getDisplayValue() override { return this->getValue() - 1; }

private:
	int32_t group;
	int32_t slot;
};

// The From or To output of one fan-out dest slot, each 0..kMaxValue. The source 0..127 is
// scaled linearly onto [from, to] (from > to inverts).
class FanOutDestRange final : public Integer {
public:
	enum Field { FROM, TO };
	FanOutDestRange(l10n::String newName, int32_t newGroup, int32_t newSlot, Field newField)
	    : Integer(newName), group(newGroup), slot(newSlot), field(newField) {}
	[[nodiscard]] int32_t getMaxValue() const override { return MIDIFanOut::kMaxValue; }
	void readCurrentValue() override { this->setValue(get()); }
	void writeCurrentValue() override {
		if (this->getValue() != get()) {
			get() = this->getValue();
			MIDIFanOut::markDirty();
		}
	}

private:
	uint8_t& get() {
		MIDIFanOut::FanOutDestSlot& dest = MIDIFanOut::groups[group].dests[slot];
		return (field == FROM) ? dest.from : dest.to;
	}
	int32_t group;
	int32_t slot;
	Field field;
};

// Send toggle for one fan-out dest slot; keeps the slot's cc/from/to while off.
class FanOutDestSend final : public Toggle {
public:
	FanOutDestSend(l10n::String newName, int32_t newGroup, int32_t newSlot)
	    : Toggle(newName), group(newGroup), slot(newSlot) {}
	// The horizontal-menu slot keeps the on/off switcher icon; the toggle popup shows the state as
	// text ("Send: On"/"Send: Off") so it's clear what changed (the base Toggle popup has no value).
	void getNotificationValue(StringBuf& value) override { value.append(this->getValue() ? "On" : "Off"); }
	void readCurrentValue() override { this->setValue(MIDIFanOut::groups[group].dests[slot].enabled); }
	void writeCurrentValue() override {
		if (this->getValue() != MIDIFanOut::groups[group].dests[slot].enabled) {
			MIDIFanOut::groups[group].dests[slot].enabled = this->getValue();
			MIDIFanOut::markDirty();
		}
	}

private:
	int32_t group;
	int32_t slot;
};

// Master enable toggle for a whole fan-out group.
class FanOutGroupOn final : public Toggle {
public:
	FanOutGroupOn(l10n::String newName, int32_t newGroup) : Toggle(newName), group(newGroup) {}
	void readCurrentValue() override { this->setValue(MIDIFanOut::groups[group].enabled); }
	void writeCurrentValue() override {
		if (this->getValue() != MIDIFanOut::groups[group].enabled) {
			MIDIFanOut::groups[group].enabled = this->getValue();
			MIDIFanOut::markDirty();
		}
	}

private:
	int32_t group;
};
} // namespace deluge::gui::menu_item::midi
