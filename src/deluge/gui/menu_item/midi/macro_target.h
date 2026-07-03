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
#include "gui/ui/ui.h"
#include "hid/buttons.h"
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

// The source of one MIDI macro: either a learned external CC (Command/LearnedMIDI) or one of the two
// physical gold knobs. LEARN + turn a gold knob learns it; SHIFT+LEARN + turn unlearns; learning an
// external CC unmaps the knob and vice-versa - the source is exactly one thing at a time.
class MacroSource final : public Command {
public:
	MacroSource(l10n::String newName, int32_t newMacro) : Command(newName), macro(newMacro) {}
	[[nodiscard]] LearnedMIDI& learned() const override {
		static LearnedMIDI dummy;
		MIDIInstrument* instrument = currentMacroInstrument();
		return instrument ? instrument->macros[macro].source : dummy;
	}
	bool learnNoteOn(MIDICable& cable, int32_t channel, int32_t noteCode) override {
		bool learnt = Command::learnNoteOn(cable, channel, noteCode);
		clearSourceKnob(); // an external-CC source replaces any gold-knob source
		markMacroInstrumentEdited();
		return learnt;
	}
	void learnKnob(MIDICable* cable, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) override {
		if (cable != nullptr) {
			return; // only gold knobs (cable == nullptr) can be a macro source
		}
		MIDIMacro::Macro* m = currentMacros();
		if (!m) {
			return;
		}
		if (Buttons::isShiftButtonPressed()) {
			// SHIFT+LEARN + turn: unlearn to a fully uninitialized source.
			m[macro].sourceKnob = -1;
			learned().clear();
			markMacroInstrumentEdited();
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_UNLEARNED));
			return;
		}
		if (whichKnob >= 0 && whichKnob < kNumPhysicalModKnobs) {
			m[macro].sourceKnob = (int8_t)whichKnob;
			learned().clear(); // a gold-knob source replaces any external-CC source
			markMacroInstrumentEdited();
			if (soundEditor.getCurrentMenuItem() == this && display->haveOLED()) {
				renderUIsForOled();
			}
			else if (soundEditor.getCurrentMenuItem() == this) {
				drawValue();
			}
			else {
				display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_LEARNED));
			}
		}
	}
	void unlearnAction() override {
		Command::unlearnAction();
		clearSourceKnob();
		markMacroInstrumentEdited();
	}
	void selectEncoderAction(int32_t offset) override {
		Command::selectEncoderAction(offset); // clears the learned CC (sets source to none)
		clearSourceKnob();
		markMacroInstrumentEdited();
	}
	void drawPixelsForOled() override {
		int8_t knob = sourceKnob();
		if (knob >= 0) {
			DEF_STACK_STRING_BUF(text, 20);
			text.append("Gold Knob ");
			text.appendInt(knob + 1);
			deluge::hid::display::OLED::main.drawString(text.c_str(), 0, 20, kTextSpacingX, kTextSizeYUpdated);
			return;
		}
		Command::drawPixelsForOled();
	}
	void drawValue() const override {
		int8_t knob = sourceKnob();
		if (knob >= 0) {
			char buffer[5] = {'G', 'K', (char)('1' + knob), 0, 0};
			display->setText(buffer);
			return;
		}
		Command::drawValue();
	}

private:
	int8_t sourceKnob() const {
		MIDIMacro::Macro* m = currentMacros();
		return m ? m[macro].sourceKnob : -1;
	}
	void clearSourceKnob() {
		if (MIDIMacro::Macro* m = currentMacros()) {
			m[macro].sourceKnob = -1;
		}
	}
	int32_t macro;
};

// The CC number of one macro target slot. Menu value 0 = OFF, 1..kMaxTargetCC+1 = CC 0..kMaxTargetCC.
class MacroTarget final : public IntegerWithOff {
public:
	MacroTarget(l10n::String newName, int32_t newMacro, int32_t newSlot)
	    : IntegerWithOff(newName), macro(newMacro), slot(newSlot) {}
	[[nodiscard]] int32_t getMaxValue() const override { return MIDIMacro::kMaxTargetCC + 1; }
	// Show the CC number (or OFF) as text instead of a dial, unlike Base/Depth.
	void renderInHorizontalMenu(const SlotPosition& pos) override {
		deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;
		const std::string str = (this->getValue() == 0) ? "OFF" : std::to_string(getDisplayValue());
		image.drawStringCentered(str.data(), pos.start_x, pos.start_y + kHorizontalMenuSlotYOffset, kTextTitleSpacingX,
		                         kTextTitleSizeY, pos.width);
	}
	// The turn popup: show "OFF" or the CC number, not the raw menu value (0 = OFF, 1 = CC 0, ...).
	void getNotificationValue(StringBuf& value) override {
		if (this->getValue() == 0) {
			value.append("OFF");
		}
		else {
			value.appendInt(getDisplayValue());
		}
	}
	void readCurrentValue() override {
		MIDIMacro::Macro* m = currentMacros();
		uint8_t cc = m ? m[macro].targets[slot].cc : MIDIMacro::kTargetCCNone;
		this->setValue((cc == MIDIMacro::kTargetCCNone) ? 0 : cc + 1);
	}
	void writeCurrentValue() override {
		MIDIMacro::Macro* m = currentMacros();
		if (!m) {
			return;
		}
		int32_t value = this->getValue();
		uint8_t cc = (value == 0) ? MIDIMacro::kTargetCCNone : value - 1;
		if (cc != m[macro].targets[slot].cc) {
			// clears the old CC's baked lane (no ghost) and bakes the new one, undoably
			MIDIMacro::changeTargetCC(getCurrentClip(), macro, slot, cc);
			markMacroInstrumentEdited();
		}
	}
	// A destination CC may be duplicated across macros (at most one active macro may own it), so allow
	// selecting any CC but warn "CC N used by Macro M" when another target already targets it.
	void selectEncoderAction(int32_t offset) override {
		IntegerWithOff::selectEncoderAction(offset); // move + clamp + write + value popup
		MIDIMacro::Macro* m = currentMacros();
		int32_t v = this->getValue();
		if (m && v != 0) {
			int32_t owner = MIDIMacro::findTargetCCOwner(m, (uint8_t)(v - 1), macro, slot);
			if (owner >= 0) {
				MIDIMacro::showCCConflictPopup((uint8_t)(v - 1), owner);
			}
		}
	}

protected:
	int32_t getDisplayValue() override { return this->getValue() - 1; }

private:
	int32_t macro;
	int32_t slot;
};

// The From or To output of one macro target slot, each 0..kMaxValue. The source 0..127 is
// scaled linearly onto [from, to] (from > to inverts). from/to are the A/B captures.
class MacroTargetRange final : public Integer {
public:
	enum Field { FROM, TO };
	MacroTargetRange(l10n::String newName, int32_t newMacro, int32_t newSlot, Field newField)
	    : Integer(newName), macro(newMacro), slot(newSlot), field(newField) {}
	[[nodiscard]] int32_t getMaxValue() const override { return MIDIMacro::kMaxValue; }
	void readCurrentValue() override { this->setValue(get()); }
	void writeCurrentValue() override {
		if (this->getValue() != get()) {
			get() = this->getValue();
			markMacroInstrumentEdited();
			MIDIMacro::reFanTarget(getCurrentClip(), macro, slot, nullptr); // from/to changed -> re-scale this lane
		}
	}
	// While this From/To dial is focused, twisting this target's own CC knob sets the endpoint live so
	// the dial tracks the knob. We update the endpoint but return false (don't consume), so the CC still
	// flows on to drive/record the param and reach the output. Only the focused From/To item responds,
	// and only for its own CC.
	bool liveEditFromMidiCC(int32_t ccNumber, int32_t value) override {
		MIDIInstrument* instrument = currentMacroInstrument();
		if (!instrument || ccNumber != instrument->macros[macro].targets[slot].cc) {
			return false;
		}
		uint8_t& endpoint = get();
		if (endpoint != value) {
			endpoint = value;
			instrument->editedByUser = true;
			MIDIMacro::reFanTarget(getCurrentClip(), macro, slot, nullptr); // live from/to -> re-scale this lane
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
		MIDIMacro::MacroTargetSlot& target = instrument->macros[macro].targets[slot];
		return (field == FROM) ? target.from : target.to;
	}
	int32_t macro;
	int32_t slot;
	Field field;
};

// Send toggle for one macro target slot; keeps the slot's cc/from/to while off.
class MacroTargetSend final : public Toggle {
public:
	MacroTargetSend(l10n::String newName, int32_t newMacro, int32_t newSlot)
	    : Toggle(newName), macro(newMacro), slot(newSlot) {}
	// The horizontal-menu slot keeps the on/off switcher icon; the toggle popup shows the state as
	// text ("Send: On"/"Send: Off") so it's clear what changed (the base Toggle popup has no value).
	void getNotificationValue(StringBuf& value) override { value.append(this->getValue() ? "On" : "Off"); }
	void readCurrentValue() override {
		MIDIMacro::Macro* m = currentMacros();
		this->setValue(m ? m[macro].targets[slot].send : true);
	}
	void writeCurrentValue() override {
		MIDIMacro::Macro* m = currentMacros();
		if (!m) {
			return;
		}
		if (this->getValue() != m[macro].targets[slot].send) {
			m[macro].targets[slot].send = this->getValue();
			markMacroInstrumentEdited();
			MIDIMacro::reFanTarget(getCurrentClip(), macro, slot, nullptr); // re-bake (muted target gets cleared)
		}
	}

private:
	int32_t macro;
	int32_t slot;
};

// Per-macro Active toggle - whether this macro participates while the instrument's macros are
// enabled. Duplicate target CCs never block the toggle: ownership (active macros first, then scan
// order) decides who drives a shared CC, and setMacroActive() re-bakes contested lanes on each flip.
class MacroActive final : public Toggle {
public:
	MacroActive(l10n::String newName, int32_t newMacro) : Toggle(newName), macro(newMacro) {}
	void readCurrentValue() override {
		MIDIMacro::Macro* m = currentMacros();
		this->setValue(m ? m[macro].active : false);
	}
	void writeCurrentValue() override {
		MIDIMacro::Macro* m = currentMacros();
		if (m && this->getValue() != m[macro].active) {
			MIDIMacro::setMacroActive(getCurrentClip(), macro, this->getValue());
		}
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
