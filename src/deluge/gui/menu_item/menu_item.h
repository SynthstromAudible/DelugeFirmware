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
#include "gui/l10n/strings.h"
#include "hid/buttons.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include <cstdint>
#include <span>

enum class MenuPermission {
	NO,
	YES,
	MUST_SELECT_RANGE,
};

class Sound;
class MultiRange;
class MIDIDevice;

/// Base class for all menu items.
class MenuItem {
public:
	MenuItem() : name(deluge::l10n::String::EMPTY_STRING), title(deluge::l10n::String::EMPTY_STRING) {}
	MenuItem(deluge::l10n::String newName, deluge::l10n::String newTitle = deluge::l10n::String::EMPTY_STRING)
	    : name(newName), title(newTitle) {
		if (newTitle == deluge::l10n::String::EMPTY_STRING) {
			title = newName;
		}
	}

	MenuItem(const MenuItem& other) = delete;
	MenuItem(const MenuItem&& other) = delete;
	MenuItem& operator=(const MenuItem& other) = delete;
	MenuItem& operator=(const MenuItem&& other) = delete;

	virtual ~MenuItem() = default;

	/// @name Input Handling
	/// @{

	/// @brief Handle an arbitrary button.
	///
	/// The returned result is forwarded up the View stack, so returning `ActionResult::DEALT_WITH` will suppress
	/// buttons going to the current View.
	///
	/// This is called with `(b == SELECT_ENC && on)` immediately after the menu is entered, or after
	/// \ref selectButtonPress is called and returns `NO_NAVIGATION`.
	virtual ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
		return ActionResult::NOT_DEALT_WITH;
	}
	/// @brief Handle horizontal encoder movement.
	///
	/// @param offset must be either -1 or 1, jumping is not supported by many children.
	virtual void horizontalEncoderAction(int32_t offset) {}
	/// @brief Handle select encoder movement.
	///
	/// Child classes which override this should be careful to handle offsets larger than 1, as holding shift and
	/// scrolling will increase them.
	virtual void selectEncoderAction(int32_t offset) {}
	/// @brief Used by the sound editor to mark the current instrument as edited when the select encoder is scrolled.
	///
	/// @todo Menu items should probably do that work themselves, rather than bubbling it out to the SoundEditor.
	virtual bool selectEncoderActionEditsInstrument() { return false; }
	/// @brief Handle a select button press.
	///
	/// @returns
	///   - `NO_NAVIGATION` if the SoundEditor should stay on the current menu.
	///   - `nullptr` if the SoundEditor should go up one level in the menu stack.
	///   - otherwise, enter the returned menu.
	virtual MenuItem* selectButtonPress() { return nullptr; }

	/// @brief Handle a TimerName::UI_SPECIFIC event.
	virtual ActionResult timerCallback() { return ActionResult::DEALT_WITH; }

	/// @brief Claim support for Kit AFFECT_ENTIRE editing.
	///
	/// @return true if this Menu can edit parameters across an entire kit if changed with AFFECT_ENTIRE held down.
	virtual bool usesAffectEntire() { return false; }

	/// @}
	/// @name Session Management
	/// @{

	/// Double-check that this MenuItem will work with the currently selected sound range.
	virtual MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                                     MultiRange** currentRange);
	/// @brief Begin an editing session with this menu item.
	///
	/// Should make sure the menu's internal state matches the system and redraw the display.
	virtual void beginSession(MenuItem* navigatedBackwardFrom = nullptr){};

	/// Re-read the value from the system and redraw the display to match.
	virtual void readValueAgain() {}

	/// @}
	/// @name Patching support
	/// @{

	/// @return the parameter index (\ref deluge::modulation::params) that the SoundEditor should look for in
	/// paramShortcutsForSounds.
	virtual uint8_t getIndexOfPatchedParamToBlink() { return 255; }
	/// Declares which parameter we intend to edit. SoundEditor uses this to find which shortcut pad to blink based on
	/// paramShortcutsForSounds.
	///
	/// @return the parameter kind (\ref deluge::modulation::params::Kind) we edit if we're a patched param, otherwise
	/// Kind::NONE
	virtual deluge::modulation::params::Kind getParamKind() { return deluge::modulation::params::Kind::NONE; }
	///
	/// @return the parameter index (\ref deluge::modulation::params) we edit if we're a patched param, otherwise 255.
	virtual uint32_t getParamIndex() { return 255; }

	/// Get the frequency at which this pad should blink the given source.
	///
	/// @param colour Output parameter for the colour to use when blinking.
	///
	/// @return Blink time period (higher means more delay between flashes), or 255 if we shouldn't blink.
	virtual uint8_t shouldBlinkPatchingSourceShortcut(PatchSource s, uint8_t* colour) { return 255; }

	/// @brief Action to take when a source shortcut is pressed.
	///
	/// Potentially reconfigures some SoundEditor state so patching will work.
	///
	/// @param s The source being pressed.
	/// @param previousPressStillActive True if there is another patch source press still active. Useful to set up cable
	/// strength modulation.
	///
	/// @returns
	///   - `NO_NAVIGATION` if the SoundEditor should ask the menu 1 layer up in the stack what to do
	///   - `nullptr` if nothing should happen
	///   - A valid MenuItem if we should switch to that menu item.
	virtual MenuItem* patchingSourceShortcutPress(PatchSource s, bool previousPressStillActive = false) {
		return nullptr;
	}

	/// @}
	/// @name Parameter learning
	/// @{

	/// Learn a mod knob to the parameter edited by this menu.
	virtual void learnKnob(MIDIDevice* fromDevice, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) {}

	/// Used by SoundEditor to determine if the current menu item can accept MIDI learning.
	virtual bool allowsLearnMode() { return false; }
	/// @brief Attempt to bind this menu item to a note code.
	///
	/// @return True if the learn succeeded and the feature controlled by this menu item will now be bound to a MIDI
	/// note, false otherwise.
	virtual bool learnNoteOn(MIDIDevice* fromDevice, int32_t channel, int32_t noteCode) { return false; }

	virtual void learnProgramChange(MIDIDevice* fromDevice, int32_t channel, int32_t programNumber) {}
	virtual void learnCC(MIDIDevice* fromDevice, int32_t channel, int32_t ccNumber, int32_t value);

	/// Return true if the Learn LED should blink while this menu is active (otherwise, it blinks only while LEARN is
	/// held).
	virtual bool shouldBlinkLearnLed() { return false; }

	/// Unlearn the parameter controlled by this menu.
	virtual void unlearnAction() {}

	/// Returns true if this parameter is only relevant to some note ranges.
	virtual bool isRangeDependent() { return false; }

	/// @}
	/// @name Rendering
	/// @{

	/// @brief Root rendering routine for OLED.
	///
	/// If you want a title, you probably want to override \ref drawPixelsForOled instead or you will need to paint the
	/// title yourself. Uses the title from \ref getTitle() when rendering.
	virtual void renderOLED();
	/// @brief Paints the pixels below the standard title block.
	virtual void drawPixelsForOled() {}

	/// @}
	///
	/// @name Submenu entry support
	///
	/// Things that \ref deluge::gui::menu_item::Submenu -like MenuItem s will query.

	/// Can get overridden by getTitle(). Actual max num chars for OLED display is 14.
	deluge::l10n::String title;

	/// @brief Get the title to be used when rendering on OLED, both as a deluge::gui::menu_item::Submenu and when
	/// displaying ourselves (using the default \ref renderOLED implementation).
	///
	/// If not overridden, defaults to returning `title`.
	///
	/// The returned pointer must live long enough for us to draw the title, which for practical purposes means "the
	/// lifetime of this menu item"
	[[nodiscard]] virtual std::string_view getTitle() const { return deluge::l10n::getView(title); }

	/// @brief Get the "draw dot state".
	///
	/// This is a bitfield representation of which segments on the 7seg should have their '.' lit up.
	///
	/// The upper 4 bits determine how the lower 4 are interpreted. If the upper 4 bits are...
	///   - `0b0000`, the lower 4 bits are treated as an index describing which dot to light (where 0 is the leftmost
	///   7seg).
	///   - `0b1000`, the lower 4 bits are a bitmask where the LSB is the leftmost 7seg and the MSB is the rightmost.
	///
	/// @warning (sapphire): I didn't actually check LTR vs RTL, someone who actually has a 7seg should do that.
	virtual uint8_t shouldDrawDotOnName() { return 255; }

	/// @brief Draw the name we want to use when selecting this in a deluge::gui::menu_item::Submenu to the 7SEG.
	///
	/// This is based on \ref getName and \ref shouldDrawDotOnName.
	virtual void drawName();

	/// @brief Default name for use on OLED for deluge::gui::menu_item::Submenu s.
	///
	/// May be overridden by `getName`
	const deluge::l10n::String name;
	/// @brief Get the actual name for use on OLED for deluge::gui::menu_item::Submenu s.
	///
	/// By default this is just the l10n string for \ref name, but can be overriden.
	[[nodiscard]] virtual std::string_view getName() const { return deluge::l10n::getView(name); }

	/// @brief Check if this MenuItem should show up in a containing deluge::gui::menu_item::Submenu.
	///
	/// @param sound Sound we would edit if we were to be entered.
	/// @param whichThing Source index within the sound. Usually ignored, but matters for e.g. oscillator and FM
	/// modulator parameters.
	virtual bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) { return true; }

	/// Internal helper which can draw the standard deluge::gui::menu_item::Submenu layout.
	static void drawItemsForOled(std::span<std::string_view> options, int32_t selectedOption, int32_t offset = 0);

	/// @brief Check if selecting this menu item (with select encoder) should enter a submenu
	virtual bool shouldEnterSubmenu() { return true; }

	/// @brief Handle rendering of submenu item types
	// length = spacing before (3 pixels)
	//			+ width of 3 characters (3 * 6 pixels - to accomodate rendering of 3 digit param values)
	//          + spacing after (3 pixels)
	// spacing before is to ensure that menu item text doesn't overlap and can scroll
	// length = 6 pixels before + (3 characters * 6 pixels per character) + 3 pixels after
	virtual int32_t getSubmenuItemTypeRenderLength() { return (3 + (3 * kTextSpacingX) + 3); }
	// push the start x over so it aligns with the right most character drawn for param value menus
	// startX = end pixel of menu item string + 3px spacing after it + spacing for 2 characters +
	// 			shift icon left 1 pixel (it's 1px wider than a regular character)
	virtual int32_t getSubmenuItemTypeRenderIconStart() { return (OLED_MAIN_WIDTH_PIXELS - 3 - kTextSpacingX - 1); }
	// render the submenu item type (icon or value)
	virtual void renderSubmenuItemTypeForOled(int32_t yPixel);

	/// @}
};

#define NO_NAVIGATION ((MenuItem*)0xFFFFFFFF)
