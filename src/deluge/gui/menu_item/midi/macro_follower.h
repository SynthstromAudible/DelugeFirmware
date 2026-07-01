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
#include "gui/menu_item/submenu.h"
#include "gui/menu_item/toggle.h"
#include "hid/display/oled.h"
#include "io/midi/midi_macro.h"
#include <string>

namespace deluge::gui::menu_item::midi {

// The learnable leader CC of one MIDI macro.
class MacroLeader final : public Command {
public:
	MacroLeader(l10n::String newName, int32_t newMacro) : Command(newName), macro(newMacro) {}
	[[nodiscard]] LearnedMIDI& learned() const override { return MIDIMacro::macros[macro].leader; }
	bool learnNoteOn(MIDICable& cable, int32_t channel, int32_t noteCode) override {
		bool learnt = Command::learnNoteOn(cable, channel, noteCode);
		MIDIMacro::markDirty();
		return learnt;
	}
	void unlearnAction() override {
		Command::unlearnAction();
		MIDIMacro::markDirty();
	}
	void selectEncoderAction(int32_t offset) override {
		Command::selectEncoderAction(offset);
		MIDIMacro::markDirty();
	}

private:
	int32_t macro;
};

// The CC number of one macro follower slot. Menu value 0 = OFF, 1..kMaxFollowerCC+1 = CC 0..kMaxFollowerCC.
class MacroFollower final : public IntegerWithOff {
public:
	MacroFollower(l10n::String newName, int32_t newMacro, int32_t newSlot)
	    : IntegerWithOff(newName), macro(newMacro), slot(newSlot) {}
	[[nodiscard]] int32_t getMaxValue() const override { return MIDIMacro::kMaxFollowerCC + 1; }
	// Show the CC number (or OFF) as text instead of a dial, unlike Base/Depth.
	void renderInHorizontalMenu(const SlotPosition& pos) override {
		deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;
		const std::string str = (this->getValue() == 0) ? "OFF" : std::to_string(getDisplayValue());
		image.drawStringCentered(str.data(), pos.start_x, pos.start_y + kHorizontalMenuSlotYOffset, kTextTitleSpacingX,
		                         kTextTitleSizeY, pos.width);
	}
	void readCurrentValue() override {
		uint8_t cc = MIDIMacro::macros[macro].followers[slot].cc;
		this->setValue((cc == MIDIMacro::kFollowerCCNone) ? 0 : cc + 1);
	}
	void writeCurrentValue() override {
		int32_t value = this->getValue();
		uint8_t cc = (value == 0) ? MIDIMacro::kFollowerCCNone : value - 1;
		if (cc != MIDIMacro::macros[macro].followers[slot].cc) {
			MIDIMacro::macros[macro].followers[slot].cc = cc;
			MIDIMacro::markDirty();
		}
	}

protected:
	int32_t getDisplayValue() override { return this->getValue() - 1; }

private:
	int32_t macro;
	int32_t slot;
};

// The From or To output of one macro follower slot, each 0..kMaxValue. The leader 0..127 is
// scaled linearly onto [from, to] (from > to inverts). from/to are the A/B captures.
class MacroFollowerRange final : public Integer {
public:
	enum Field { FROM, TO };
	MacroFollowerRange(l10n::String newName, int32_t newMacro, int32_t newSlot, Field newField)
	    : Integer(newName), macro(newMacro), slot(newSlot), field(newField) {}
	[[nodiscard]] int32_t getMaxValue() const override { return MIDIMacro::kMaxValue; }
	void readCurrentValue() override { this->setValue(get()); }
	void writeCurrentValue() override {
		if (this->getValue() != get()) {
			get() = this->getValue();
			MIDIMacro::markDirty();
		}
	}
	// While this From/To dial is focused, twisting this follower's own CC knob sets the endpoint live so
	// the dial tracks the knob. We update the endpoint but return false (don't consume), so the CC still
	// flows on to drive/record the param and reach the output. Only the focused From/To item responds,
	// and only for its own CC.
	bool liveEditFromMidiCC(int32_t ccNumber, int32_t value) override {
		if (ccNumber != MIDIMacro::macros[macro].followers[slot].cc) {
			return false;
		}
		uint8_t& endpoint = get();
		if (endpoint != value) {
			endpoint = value;
			MIDIMacro::markDirty();
			readValueAgain(); // re-read from the model and redraw the dial
		}
		return false; // don't consume: let the CC continue out to the param/output
	}

private:
	uint8_t& get() {
		MIDIMacro::MacroFollowerSlot& follower = MIDIMacro::macros[macro].followers[slot];
		return (field == FROM) ? follower.from : follower.to;
	}
	int32_t macro;
	int32_t slot;
	Field field;
};

// Send toggle for one macro follower slot; keeps the slot's cc/from/to while off.
class MacroFollowerSend final : public Toggle {
public:
	MacroFollowerSend(l10n::String newName, int32_t newMacro, int32_t newSlot)
	    : Toggle(newName), macro(newMacro), slot(newSlot) {}
	// The horizontal-menu slot keeps the on/off switcher icon; the toggle popup shows the state as
	// text ("Send: On"/"Send: Off") so it's clear what changed (the base Toggle popup has no value).
	void getNotificationValue(StringBuf& value) override { value.append(this->getValue() ? "On" : "Off"); }
	void readCurrentValue() override { this->setValue(MIDIMacro::macros[macro].followers[slot].enabled); }
	void writeCurrentValue() override {
		if (this->getValue() != MIDIMacro::macros[macro].followers[slot].enabled) {
			MIDIMacro::macros[macro].followers[slot].enabled = this->getValue();
			MIDIMacro::markDirty();
		}
	}

private:
	int32_t macro;
	int32_t slot;
};

// Master enable toggle for a whole macro.
class MacroEnable final : public Toggle {
public:
	MacroEnable(l10n::String newName, int32_t newMacro) : Toggle(newName), macro(newMacro) {}
	void readCurrentValue() override { this->setValue(MIDIMacro::macros[macro].enabled); }
	void writeCurrentValue() override {
		if (this->getValue() != MIDIMacro::macros[macro].enabled) {
			MIDIMacro::macros[macro].enabled = this->getValue();
			MIDIMacro::markDirty();
		}
	}

private:
	int32_t macro;
};

// Enables or disables every macro at once. Reads On only when all macros are enabled.
class MacroEnableAll final : public Toggle {
public:
	using Toggle::Toggle;
	void readCurrentValue() override {
		bool allOn = true;
		for (MIDIMacro::Macro& m : MIDIMacro::macros) {
			allOn = allOn && m.enabled;
		}
		this->setValue(allOn);
	}
	void writeCurrentValue() override {
		bool on = this->getValue();
		for (MIDIMacro::Macro& m : MIDIMacro::macros) {
			m.enabled = on;
		}
		MIDIMacro::markDirty();
	}
};

// The MIDI Macro submenu, shown in the MIDI settings menu only while the feature is enabled in
// Community Features.
class MacroMenu final : public Submenu {
public:
	using Submenu::Submenu;
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return MIDIMacro::isEnabled();
	}
};
} // namespace deluge::gui::menu_item::midi
