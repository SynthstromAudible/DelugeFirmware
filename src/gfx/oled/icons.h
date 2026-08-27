#pragma once
#include "gfx/oled/icon.h"

namespace deluge::gfx::oled::icons {
extern const Icon syncTypeTriplets;
extern const Icon syncTypeDotted;
extern const Icon syncTypeEven;
extern const Icon switcherOff;
extern const Icon switcherOn;
extern const Icon arpModeUp;
extern const Icon arpModeUpDown;
extern const Icon arpModeWalk;
extern const Icon arpModeCustom;
extern const Icon lockedBig;
extern const Icon unlockedBig;
extern const Icon dice;
extern const Icon sine;
extern const Icon triangle;
extern const Icon square;
extern const Icon saw;
extern const Icon sampleHold;
extern const Icon randomWalk;
extern const Icon warbler;
extern const Icon direction;
extern const Icon knobArc;
extern const Icon infinity;
extern const Icon sample;
extern const Icon wavetable;
extern const Icon input;
extern const Icon mic;
extern const Icon folderBig;
extern const Icon loopPoint;
extern const Icon sampleModeLoop;
extern const Icon sampleModeOnce;
extern const Icon sampleModeCut;
extern const Icon sampleModeStretch;
extern const Icon keyboard;
extern const Icon crossedOutKeyboard;

// Menu-row icons, ported from the legacy OLED::checkedBoxIcon / uncheckedBoxIcon /
// submenuArrowIcon (src/deluge/hid/display/oled_icons.cpp:130-157). 7 columns wide, which is
// exactly the kSubmenuIconSpacingX the legacy submenu renderer blits them at.
extern const Icon checkedBox;
extern const Icon uncheckedBox;
extern const Icon submenuArrow;
} // namespace deluge::gfx::oled::icons
