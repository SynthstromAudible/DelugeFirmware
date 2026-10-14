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

/// @{
/// @name Menu-row icons
///
/// 7 columns wide -- exactly the kSubmenuIconSpacingX the submenu renderer blits them at.
/// @note Legacy icon `checkedBoxIcon`.
extern const Icon checkedBox;
/// @note Legacy icon `uncheckedBoxIcon`.
extern const Icon uncheckedBox;
/// @note Legacy icon `submenuArrowIcon`.
extern const Icon submenuArrow;
/// @brief The bolder chevron the *horizontal* menu draws for a submenu slot.
/// @note Legacy icon `submenuArrowIconBold`.
extern const Icon submenuArrowBold;
/// @}

/// @{
/// @name Routing-diagram arrowheads (`ui/oled/routing.h`)
/// @note Legacy icons `OLED::downArrowIcon` (5 wide) and `OLED::rightArrowIcon` (3 wide).
extern const Icon arrowDown;
extern const Icon arrowRight;
/// @}

/// @{
/// @name File-browser row icons (`scenes/browser/browser_scene.h`)
///
/// 8 columns wide -- exactly the `iconWidth` `Browser::renderOLED` blits them at
/// (`gui/ui/browser/browser.cpp:973`) -- except `browserMidiEdge`, which is the MIDI plug's ninth
/// column drawn on its own.
/// @note Legacy icon `OLED::folderIcon`.
extern const Icon browserFolder;
/// @note Legacy icon `OLED::waveIcon`, `SampleBrowser`'s `fileIcon`.
extern const Icon browserWave;
/// @note Legacy icon `OLED::midiIcon`, `LoadMidiDeviceDefinitionUI`'s `fileIcon`.
extern const Icon browserMidi;
/// @note Legacy icon `OLED::midiIconPt2`, its `fileIconPt2` -- one column, see icons.cpp.
extern const Icon browserMidiEdge;
/// @}

/// @{
/// @name Sample-marker editor (`scenes/marker/marker_scene.h`)
/// @note Legacy icon `OLED::lockIcon` (`hid/display/oled_icons.cpp:120`), 7 columns wide. **It is
///       nine pixels tall and a byte is eight**, so legacy's own comment says "remember to draw a
///       rectangle under it so it looks correct" -- `SampleMarkerEditor::renderOLED` does that with
///       an `invertArea` one row below (`gui/ui/sample_marker_editor.cpp:1131-1132`), and so does
///       `ui::oled::title_badge`, which is the only intended caller.
extern const Icon markerLock;
/// @}
} // namespace deluge::gfx::oled::icons
