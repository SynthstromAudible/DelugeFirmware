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
#include "gui/l10n/l10n.h"
#include "gui/menu_item/integer.h"
#include "gui/menu_item/midi/command.h"
#include "gui/menu_item/submenu.h"
#include "gui/menu_item/toggle.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "io/midi/midi_macro.h"
#include "model/instrument/midi_instrument.h"
#include "model/output.h"
#include "model/song/song.h"
#include "util/d_stringbuf.h"
#include <string>

namespace deluge::gui::menu_item::midi {

// MIDI macros are stored per-track on the current MIDI clip's instrument. These helpers resolve it;
// they return null when the active output isn't a MIDI track (the menu is hidden then, but read/write
// can still be reached defensively).
inline MIDIInstrument* currentMacroInstrument() {
	Output* output = getCurrentOutput();
	if (!output || output->type != OutputType::MIDI_OUT) {
		return nullptr;
	}
	return static_cast<MIDIInstrument*>(output);
}
inline MIDIMacro::Macro* currentMacros() {
	MIDIInstrument* instrument = currentMacroInstrument();
	return instrument ? instrument->macros : nullptr;
}
inline void markMacroInstrumentEdited() {
	if (MIDIInstrument* instrument = currentMacroInstrument()) {
		instrument->editedByUser = true;
	}
}

// The learnable leader CC of one MIDI macro.
class MacroLeader final : public Command {
public:
	MacroLeader(l10n::String newName, int32_t newMacro) : Command(newName), macro(newMacro) {}
	[[nodiscard]] LearnedMIDI& learned() const override {
		static LearnedMIDI dummy;
		MIDIInstrument* instrument = currentMacroInstrument();
		return instrument ? instrument->macros[macro].leader : dummy;
	}
	bool learnNoteOn(MIDICable& cable, int32_t channel, int32_t noteCode) override {
		bool learnt = Command::learnNoteOn(cable, channel, noteCode);
		markMacroInstrumentEdited();
		return learnt;
	}
	void unlearnAction() override {
		Command::unlearnAction();
		markMacroInstrumentEdited();
	}
	void selectEncoderAction(int32_t offset) override {
		Command::selectEncoderAction(offset);
		markMacroInstrumentEdited();
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
		MIDIMacro::Macro* m = currentMacros();
		uint8_t cc = m ? m[macro].followers[slot].cc : MIDIMacro::kFollowerCCNone;
		this->setValue((cc == MIDIMacro::kFollowerCCNone) ? 0 : cc + 1);
	}
	void writeCurrentValue() override {
		MIDIMacro::Macro* m = currentMacros();
		if (!m) {
			return;
		}
		int32_t value = this->getValue();
		uint8_t cc = (value == 0) ? MIDIMacro::kFollowerCCNone : value - 1;
		if (cc != m[macro].followers[slot].cc) {
			m[macro].followers[slot].cc = cc;
			markMacroInstrumentEdited();
		}
	}
	// A destination CC must be unique across this instrument's macros, so skip over CCs already used by
	// another follower while scrolling. If there's no free CC in that direction, restore the previous CC
	// and warn - two followers on one CC would just fight over it.
	void selectEncoderAction(int32_t offset) override {
		MIDIMacro::Macro* m = currentMacros();
		uint8_t previousCC = m ? m[macro].followers[slot].cc : MIDIMacro::kFollowerCCNone;
		IntegerWithOff::selectEncoderAction(offset); // move + clamp + write + redraw
		if (offset == 0) {
			return;
		}
		int32_t dir = (offset > 0) ? 1 : -1;
		while (currentValueIsUsedCC()) {
			int32_t before = this->getValue();
			IntegerWithOff::selectEncoderAction(dir);
			if (this->getValue() == before) { // hit a bound with no free CC - restore + warn
				this->setValue((previousCC == MIDIMacro::kFollowerCCNone) ? 0 : previousCC + 1);
				writeCurrentValue();
				readValueAgain();
				display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_MACRO_CC_IN_USE));
				return;
			}
		}
	}

protected:
	int32_t getDisplayValue() override { return this->getValue() - 1; }

	bool currentValueIsUsedCC() {
		MIDIMacro::Macro* m = currentMacros();
		int32_t v = this->getValue();
		return m && v != 0 && MIDIMacro::isFollowerCCUsed(m, (uint8_t)(v - 1), macro, slot);
	}

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
			markMacroInstrumentEdited();
		}
	}
	// While this From/To dial is focused, twisting this follower's own CC knob sets the endpoint live so
	// the dial tracks the knob. We update the endpoint but return false (don't consume), so the CC still
	// flows on to drive/record the param and reach the output. Only the focused From/To item responds,
	// and only for its own CC.
	bool liveEditFromMidiCC(int32_t ccNumber, int32_t value) override {
		MIDIInstrument* instrument = currentMacroInstrument();
		if (!instrument || ccNumber != instrument->macros[macro].followers[slot].cc) {
			return false;
		}
		uint8_t& endpoint = get();
		if (endpoint != value) {
			endpoint = value;
			instrument->editedByUser = true;
			readValueAgain(); // re-read from the model and redraw the dial
		}
		return false; // don't consume: let the CC continue out to the param/output
	}

private:
	uint8_t& get() {
		static uint8_t dummy = 0;
		MIDIInstrument* instrument = currentMacroInstrument();
		if (!instrument) {
			return dummy;
		}
		MIDIMacro::MacroFollowerSlot& follower = instrument->macros[macro].followers[slot];
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
	void readCurrentValue() override {
		MIDIMacro::Macro* m = currentMacros();
		this->setValue(m ? m[macro].followers[slot].send : true);
	}
	void writeCurrentValue() override {
		MIDIMacro::Macro* m = currentMacros();
		if (!m) {
			return;
		}
		if (this->getValue() != m[macro].followers[slot].send) {
			m[macro].followers[slot].send = this->getValue();
			markMacroInstrumentEdited();
		}
	}

private:
	int32_t macro;
	int32_t slot;
};

// Per-macro Active toggle - whether this macro participates while the instrument's macros are enabled.
// A macro can't be activated while one of its follower CCs is owned by another active macro.
class MacroActive final : public Toggle {
public:
	MacroActive(l10n::String newName, int32_t newMacro) : Toggle(newName), macro(newMacro) {}
	void readCurrentValue() override {
		MIDIMacro::Macro* m = currentMacros();
		this->setValue(m ? m[macro].active : false);
	}
	void writeCurrentValue() override {
		MIDIMacro::Macro* m = currentMacros();
		if (!m) {
			return;
		}
		bool want = this->getValue();
		if (want && MIDIMacro::macroHasActiveConflict(m, macro)) {
			want = false; // refuse activation - a follower CC is owned by another active macro
			this->setValue(false);
		}
		if (want != m[macro].active) {
			m[macro].active = want;
			markMacroInstrumentEdited();
		}
	}
	MenuItem* selectButtonPress() override {
		if (soundEditor.getCurrentMenuItem() == this) {
			return Toggle::selectButtonPress();
		}
		// Which active macro (and CC) would block turning this one on - captured before the toggle.
		MIDIMacro::Macro* m = currentMacros();
		uint8_t conflictCC = 0;
		int32_t conflict = (m && !m[macro].active) ? MIDIMacro::findActiveConflict(m, macro, &conflictCC) : -1;
		MenuItem* result = Toggle::selectButtonPress(); // toggles (writeCurrentValue may block)
		if (conflict >= 0 && m && !m[macro].active) {
			// tried to turn on but a CC conflict blocked it, e.g. "CC 16 used by Macro 1"
			DEF_STACK_STRING_BUF(popup, 48);
			popup.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_MACRO_FOLLOWER_CC)); // "CC"
			popup.append(' ');
			popup.appendInt(conflictCC);
			popup.append(' ');
			popup.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_MACRO_USED_BY_MACRO)); // "used by Macro"
			popup.append(' ');
			popup.appendInt(conflict + 1);
			display->displayPopup(popup.c_str());
		}
		return result;
	}

private:
	int32_t macro;
};

// Per-instrument enable gate ("Enable Macros"): when off, none of this track's macros fire even if
// individually Active. Does not change any macro's Active state - just pauses/resumes them at once.
class MacroEnable final : public Toggle {
public:
	using Toggle::Toggle;
	void readCurrentValue() override {
		MIDIInstrument* instrument = currentMacroInstrument();
		this->setValue(instrument ? instrument->macrosEnabled : false);
	}
	void writeCurrentValue() override {
		MIDIInstrument* instrument = currentMacroInstrument();
		if (instrument && this->getValue() != instrument->macrosEnabled) {
			instrument->macrosEnabled = this->getValue();
			instrument->editedByUser = true;
		}
	}
};

// The MIDI Macros submenu, shown in the MIDI clip's menu only while the feature is enabled in
// Community Features and the current output is a MIDI track.
class MacroMenu final : public Submenu {
public:
	using Submenu::Submenu;
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return MIDIMacro::isEnabled() && currentMacroInstrument() != nullptr;
	}
};
} // namespace deluge::gui::menu_item::midi
