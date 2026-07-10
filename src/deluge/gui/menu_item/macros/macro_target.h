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
#include "model/instrument/midi_instrument.h"
#include "model/output.h"
#include "model/song/song.h"
#include "modulation/macros/macros.h"
#include "util/d_stringbuf.h"
#include "util/misc.h"
#include <string>

namespace deluge::gui::menu_item::macros {

// macros are stored per-track on the current clip's instrument. These helpers resolve it; they
// return null when the active output isn't a MIDI or synth track (the menu is hidden then, but
// read/write can still be reached defensively).
inline Output* currentMacroInstrument() {
	return Macros::macroHost(getCurrentClip());
}
inline Macros::Macro* currentMacros() {
	Output* instrument = currentMacroInstrument();
	return instrument ? instrument->macros : nullptr;
}
inline Macros::Domain currentMacroDomain() {
	Output* instrument = currentMacroInstrument();
	return instrument ? Macros::domainForOutput(instrument) : Macros::Domain::MIDI;
}
inline void markMacroInstrumentEdited() {
	if (Output* instrument = currentMacroInstrument()) {
		Macros::markHostEdited(instrument);
	}
}

// The source of one macro: either a learned external CC (Command/LearnedMIDI) or one of the two
// physical gold knobs. LEARN + turn a gold knob learns it; SHIFT+LEARN + turn unlearns; learning an
// external CC unmaps the knob and vice-versa - the source is exactly one thing at a time.
class MacroSource final : public midi::Command {
public:
	MacroSource(l10n::String newName, int32_t newMacro) : midi::Command(newName), macro(newMacro) {}
	[[nodiscard]] LearnedMIDI& learned() const override {
		static LearnedMIDI dummy;
		Output* instrument = currentMacroInstrument();
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
		Macros::Macro* m = currentMacros();
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
		Macros::Macro* m = currentMacros();
		return m ? m[macro].sourceKnob : -1;
	}
	void clearSourceKnob() {
		if (Macros::Macro* m = currentMacros()) {
			m[macro].sourceKnob = -1;
		}
	}
	int32_t macro;
};

// The destination of one macro target slot. Menu value 0 = OFF, 1..kMaxMidiDestination+1 = CC
// 0..kMaxMidiDestination, then the cascade destinations (the higher-indexed macros this macro may target,
// shown as plain "Macro N" regardless of any user-given macro name).
class MacroTarget final : public IntegerWithOff {
public:
	MacroTarget(l10n::String newName, int32_t newMacro, int32_t newSlot)
	    : IntegerWithOff(newName), macro(newMacro), slot(newSlot) {}
	[[nodiscard]] int32_t getMaxValue() const override { return Macros::numDestinations(currentMacroDomain(), macro); }
	// Show the destination (OFF / CC number / param or macro name) as text instead of a dial,
	// unlike Base/Depth.
	void renderInHorizontalMenu(const SlotPosition& pos) override {
		deluge::hid::display::oled_canvas::Canvas& image = deluge::hid::display::OLED::main;
		int32_t v = this->getValue();
		DEF_STACK_STRING_BUF(str, 32);
		if (v == 0) {
			str.append("OFF");
		}
		else {
			int32_t destination = valueToDestination(v);
			if (Macros::isMacroParamID(destination)) {
				str.append('M');
				str.appendInt(Macros::macroIndexFromParamID(destination) + 1);
			}
			else if (Macros::isDomainInternal(currentMacroDomain())) {
				// the param's display name - the canvas clips what doesn't fit the slot
				Macros::appendDestinationName(str, currentMacroInstrument(), (uint8_t)destination);
			}
			else {
				str.appendInt(destination); // the CC number
			}
		}
		image.drawStringCentered(str.c_str(), pos.start_x, pos.start_y + kHorizontalMenuSlotYOffset, kTextTitleSpacingX,
		                         kTextTitleSizeY, pos.width);
	}
	// The turn popup: show "OFF", the destination's name/number, not the raw menu value.
	void getNotificationValue(StringBuf& value) override {
		int32_t v = this->getValue();
		if (v == 0) {
			value.append("OFF");
		}
		else if (isCascadeValue(v)) {
			appendCascadeName(value, v);
		}
		else if (Macros::isDomainInternal(currentMacroDomain())) {
			Macros::appendDestinationName(value, currentMacroInstrument(), (uint8_t)valueToDestination(v));
		}
		else {
			value.appendInt(valueToDestination(v));
		}
	}
	void drawValue() override {
		int32_t v = this->getValue();
		if (v != 0 && (isCascadeValue(v) || Macros::isDomainInternal(currentMacroDomain()))) {
			DEF_STACK_STRING_BUF(text, 32);
			getNotificationValue(text);
			display->setScrollingText(text.c_str());
			return;
		}
		IntegerWithOff::drawValue();
	}
	void readCurrentValue() override {
		Macros::Macro* m = currentMacros();
		uint8_t destination = m ? m[macro].targets[slot].destination : Macros::kNoDestination;
		// a destination that isn't in this domain's space (position -1) reads as OFF
		this->setValue((destination == Macros::kNoDestination) ? 0
		                                                       : std::max<int32_t>(0, destinationToValue(destination)));
	}
	void writeCurrentValue() override {
		Macros::Macro* m = currentMacros();
		if (!m) {
			return;
		}
		int32_t value = this->getValue();
		int32_t resolved = (value == 0) ? -1 : valueToDestination(value);
		uint8_t destination = (resolved < 0) ? Macros::kNoDestination : (uint8_t)resolved;
		if (destination != m[macro].targets[slot].destination) {
			// clears the old destination's baked lane (no ghost) and bakes the new one, undoably
			Macros::changeTargetDestination(getCurrentClip(), macro, slot, destination);
			markMacroInstrumentEdited();
		}
	}
	// A destination may be duplicated across macros (at most one active macro may own it), so allow
	// selecting any but warn "<destination> used by Macro M" when another target already targets it.
	void selectEncoderAction(int32_t offset) override {
		IntegerWithOff::selectEncoderAction(offset); // move + clamp + write + value popup
		Macros::Macro* m = currentMacros();
		int32_t v = this->getValue();
		if (m && v != 0) {
			uint8_t destination = (uint8_t)valueToDestination(v);
			int32_t owner = Macros::findTargetDestinationOwner(m, destination, macro, slot);
			if (owner >= 0) {
				Macros::showDestinationConflictPopup(destination, owner);
			}
		}
	}

protected:
	int32_t getDisplayValue() override { return this->getValue() - 1; }

private:
	// Menu value = dial position + 1 (0 is OFF); the seam's per-domain position space covers CC
	// numbers / the targetable param list, then this macro's allowed cascade destinations.
	bool isCascadeValue(int32_t value) const { return value != 0 && Macros::isMacroParamID(valueToDestination(value)); }
	int32_t valueToDestination(int32_t value) const {
		return Macros::destinationForPosition(currentMacroDomain(), macro, value - 1);
	}
	int32_t destinationToValue(int32_t destination) const {
		return Macros::positionForDestination(currentMacroDomain(), macro, destination) + 1;
	}
	void appendCascadeName(StringBuf& buf, int32_t value) const {
		buf.append(deluge::l10n::get(
		    static_cast<deluge::l10n::String>(util::to_underlying(deluge::l10n::String::STRING_FOR_MACRO_1)
		                                      + Macros::macroIndexFromParamID(valueToDestination(value)))));
	}
	int32_t macro;
	int32_t slot;
};

// The From or To output of one macro target slot, each 0..kMaxValue. The source 0..127 is
// scaled linearly onto [from, to] (from > to inverts).
class MacroTargetRange final : public Integer {
public:
	enum Field { FROM, TO };
	MacroTargetRange(l10n::String newName, int32_t newMacro, int32_t newSlot, Field newField)
	    : Integer(newName), macro(newMacro), slot(newSlot), field(newField) {}
	[[nodiscard]] int32_t getMaxValue() const override { return Macros::maxTargetValue(currentMacroDomain()); }
	void readCurrentValue() override { this->setValue(get()); }
	void writeCurrentValue() override {
		if (this->getValue() != get()) {
			get() = this->getValue();
			markMacroInstrumentEdited();
			Macros::reFanTarget(getCurrentClip(), macro, slot, nullptr); // from/to changed -> re-scale this lane
		}
	}
	// While this From/To dial is focused, twisting this target's own CC knob sets the endpoint live so
	// the dial tracks the knob. We update the endpoint but return false (don't consume), so the CC still
	// flows on to drive/record the param and reach the output. Only the focused From/To item responds,
	// and only for its own CC - so MIDI clips only (a synth destination byte is not a CC number).
	bool liveEditFromMidiCC(int32_t ccNumber, int32_t value) override {
		Output* instrument = currentMacroInstrument();
		if (!instrument || instrument->type != OutputType::MIDI_OUT
		    || ccNumber != instrument->macros[macro].targets[slot].destination) {
			return false;
		}
		uint8_t& endpoint = get();
		if (endpoint != value) {
			endpoint = value;
			Macros::markHostEdited(instrument);
			Macros::reFanTarget(getCurrentClip(), macro, slot, nullptr); // live from/to -> re-scale this lane
			readValueAgain();                                            // re-read from the model and redraw the dial
		}
		return false; // don't consume: let the CC continue out to the param/output
	}

private:
	uint8_t& get() {
		static uint8_t dummy = 0;
		Output* instrument = currentMacroInstrument();
		if (!instrument) {
			return dummy;
		}
		Macros::MacroTargetSlot& target = instrument->macros[macro].targets[slot];
		return (field == FROM) ? target.from : target.to;
	}
	int32_t macro;
	int32_t slot;
	Field field;
};

// Per-macro Active toggle - whether this macro participates while the instrument's macros are
// enabled. Duplicate target CCs never block the toggle: ownership (active macros first, then scan
// order) decides who drives a shared CC, and setMacroActive() re-bakes contested lanes on each flip.
class MacroActive final : public Toggle {
public:
	MacroActive(l10n::String newName, int32_t newMacro) : Toggle(newName), macro(newMacro) {}
	void readCurrentValue() override {
		Macros::Macro* m = currentMacros();
		this->setValue(m ? m[macro].active : false);
	}
	void writeCurrentValue() override {
		Macros::Macro* m = currentMacros();
		if (m && this->getValue() != m[macro].active) {
			Macros::setMacroActive(getCurrentClip(), macro, this->getValue());
		}
	}

private:
	int32_t macro;
};

// The macros submenu, shown while the feature is enabled in Community Features and the current
// output can host macros (MIDI, synth, or audio - anything Macros::macroHost() accepts).
class MacroMenu final : public Submenu {
public:
	using Submenu::Submenu;
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return Macros::isEnabled() && currentMacroInstrument() != nullptr;
	}
};
} // namespace deluge::gui::menu_item::macros
