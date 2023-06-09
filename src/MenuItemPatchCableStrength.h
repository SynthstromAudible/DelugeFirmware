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

#ifndef MENUITEMPATCHCABLESTRENGTH_H_
#define MENUITEMPATCHCABLESTRENGTH_H_

#include "MenuItemInteger.h"
#include "MenuItemWithCCLearning.h"

class MenuItemPatchCableStrength : public MenuItemIntegerContinuous, public MenuItemWithCCLearning {
public:
	MenuItemPatchCableStrength(char const* newName = NULL) : MenuItemIntegerContinuous(newName) {}
	void readCurrentValue() final;
	void writeCurrentValue();
	int getMinValue() final { return -50; }
	int getMaxValue() final { return 50; }
	virtual int checkPermissionToBeginSession(Sound* sound, int whichThing, MultiRange** currentRange);
	virtual ParamDescriptor getDestinationDescriptor() = 0;
	virtual uint8_t getS() = 0;
	uint8_t getIndexOfPatchedParamToBlink() final;
	MenuItem* selectButtonPress();
#if HAVE_OLED
	void renderOLED();
#endif

	void unlearnAction() final {
		MenuItemWithCCLearning::unlearnAction();
	}
	bool allowsLearnMode() final {
		return MenuItemWithCCLearning::allowsLearnMode();
	}
	void learnKnob(MIDIDevice* fromDevice, int whichKnob, int modKnobMode, int midiChannel) final {
		MenuItemWithCCLearning::learnKnob(fromDevice, whichKnob, modKnobMode, midiChannel);
	};

protected:
	ModelStackWithAutoParam* getModelStack(void* memory, bool allowCreation = false);
};

class MenuItemPatchCableStrengthRegular : public MenuItemPatchCableStrength {
public:
	MenuItemPatchCableStrengthRegular(char const* newName = NULL) : MenuItemPatchCableStrength(newName) {}
	ParamDescriptor getDestinationDescriptor() final;
	uint8_t getS() final;
	ParamDescriptor getLearningThing() final;
	int checkPermissionToBeginSession(Sound* sound, int whichThing, MultiRange** currentRange);
	uint8_t shouldBlinkPatchingSourceShortcut(int s, uint8_t* colour);
	MenuItem* patchingSourceShortcutPress(int s, bool previousPressStillActive);
	MenuItem* selectButtonPress() final;
#if !HAVE_OLED
	void drawValue() final;
#endif
};

class MenuItemPatchCableStrengthRange final : public MenuItemPatchCableStrength {
public:
	MenuItemPatchCableStrengthRange(char const* newName = NULL) : MenuItemPatchCableStrength(newName) {}
	void drawValue();
	ParamDescriptor getDestinationDescriptor();
	uint8_t getS();
	ParamDescriptor getLearningThing();
	uint8_t shouldBlinkPatchingSourceShortcut(int s, uint8_t* colour);
	MenuItem* patchingSourceShortcutPress(int s, bool previousPressStillActive);
};

extern MenuItemPatchCableStrengthRegular patchCableStrengthMenuRegular;
extern MenuItemPatchCableStrengthRange patchCableStrengthMenuRange;

class MenuItemFixedPatchCableStrength : public MenuItemPatchCableStrengthRegular {
public:
	MenuItemFixedPatchCableStrength(char const* newName = NULL, int newP = 0, int newS = 0)
	    : MenuItemPatchCableStrengthRegular(newName) {
		p = newP;
		s = newS;
	}

	int checkPermissionToBeginSession(Sound* sound, int whichThing, MultiRange** currentRange) final;
	uint8_t shouldBlinkPatchingSourceShortcut(int s, uint8_t* colour) final;
	MenuItem* patchingSourceShortcutPress(int s, bool previousPressStillActive) final;

protected:
	uint8_t p;
	uint8_t s;
};

#endif /* MENUITEMPATCHCABLESTRENGTH_H_ */
