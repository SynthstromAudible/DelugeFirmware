/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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
#include "model/scale/preset_scales.h"
#include <bitset>
#include <cstdint>
#include <span>

#define PREVIEW_OFF 0
#define PREVIEW_ONLY_WHILE_NOT_PLAYING 1
#define PREVIEW_ON 2

enum class Polarity : uint8_t;
namespace FlashStorage {

extern uint8_t defaultScale;
extern bool audioClipRecordMargins;
extern KeyboardLayout keyboardLayout;
extern uint8_t
    recordQuantizeLevel; // Assumes insideWorldTickMagnitude==1, which is not default anymore, so adjust accordingly
extern uint8_t sampleBrowserPreviewMode;
extern uint8_t defaultVelocity;
extern int8_t defaultMagnitude;
extern bool settingsBeenRead;
extern uint8_t defaultBendRange[2];

extern SessionLayoutType defaultSessionLayout;
extern KeyboardLayoutType defaultKeyboardLayout;

extern bool keyboardFunctionsVelocityGlide;
extern bool keyboardFunctionsModwheelGlide;

extern FavouritesDefaultLayout defaultFavouritesLayout;

extern bool gridEmptyPadsUnarm;
extern bool gridEmptyPadsCreateRec;
extern bool gridAllowGreenSelection;
extern GridDefaultActiveMode defaultGridActiveMode;

extern uint8_t defaultMetronomeVolume;

extern bool automationInterpolate;
extern bool automationClear;
extern bool automationShift;
extern bool automationNudgeNote;
extern bool automationDisableAuditionPadShortcuts;

extern StartupSongMode defaultStartupSongMode;
extern uint8_t defaultPadBrightness;
extern SampleRepeatMode defaultSliceMode;
extern bool highCPUUsageIndicator;
extern uint8_t defaultHoldTime;
extern int32_t holdTime;

extern uint8_t defaultSwingInterval;

extern std::bitset<NUM_PRESET_SCALES> defaultDisabledPresetScales;

extern bool accessibilityShortcuts;
extern MenuHighlighting accessibilityMenuHighlighting;

extern OutputType defaultNewClipType;
extern bool defaultUseLastClipType;
/// What the screensaver shows, or OFF to disable it entirely.
extern ScreensaverMode screensaverMode;
/// Minutes without physical input before the screensaver appears, kMinScreensaverTimeoutMinutes to
/// kMaxScreensaverTimeoutMinutes.
extern uint8_t screensaverTimeoutMinutes;

extern ThresholdRecordingMode defaultThresholdRecordingMode;

extern Polarity defaultPatchCablePolarity;

extern GlobalMIDICommand defaultLoopRecordingCommand;

extern bool defaultUseSharps;

/// @brief Which of the six pad-colour settings a `padColours` entry holds.
///
/// The order is the COLOURS page's own (`gui/ui/menus.cpp:1248-1253`, `colourMenu`), which is also
/// the order `readSettings`/`writeSettings` lay the first four out in flash (bytes 74..77, plus 165
/// and 166 for FILL and ONCE -- see `flash_storage.cpp`).
enum PadColourSlot : uint8_t {
	PAD_COLOUR_ACTIVE,
	PAD_COLOUR_STOPPED,
	PAD_COLOUR_MUTED,
	PAD_COLOUR_SOLOED,
	PAD_COLOUR_FILL,
	PAD_COLOUR_ONCE,
	NUM_PAD_COLOUR_SLOTS,
};

/// @brief The six pad colours, each one a `deluge::gui::menu_item::Colour::Option`.
///
/// These used to live one-per-object as `menu_item::Colour::value`, a public member of the legacy
/// menu item itself (`gui/menu_item/colour.h`). They are settings, not menu state -- `readSettings`
/// and `writeSettings` have always persisted them and `Song::setupDefault`-adjacent code has always
/// read them -- and a member of a `gui/` class is unreachable from the layers that are not allowed
/// to name `gui/` at all (`src/scenes`, whose COLOURS page edits exactly these six bytes). So the
/// bytes live here and `menu_item::Colour::value` is now a reference onto one of them; every legacy
/// spelling (`activeColourMenu.value`, `Colour::getRGB()`) reads and writes the same storage it did
/// before.
extern uint8_t padColours[NUM_PAD_COLOUR_SLOTS];

/// @brief Which of the three default `{lower, upper}` range settings a `defaultRanges` entry holds.
///
/// The order is `soundEditorRootMenuSettings`' own DEFAULTS page (`gui/ui/menus.cpp:1693-1696`),
/// which is also the order `readSettings`/`writeSettings` lay them out in flash (bytes 53..58).
enum DefaultRangeSlot : uint8_t {
	DEFAULT_RANGE_TEMPO,
	DEFAULT_RANGE_SWING_AMOUNT,
	DEFAULT_RANGE_KEY,
	NUM_DEFAULT_RANGE_SLOTS,
};

/// @brief One default setting that is a *range* rather than a value: a low and a high bound, from
///        which `Song::setupDefault` draws a random starting point (`model/song/song.cpp:263-265`).
struct DefaultRange {
	int32_t lower;
	int32_t upper;
};

/// @brief The three default ranges: tempo, swing amount and key.
///
/// These used to live one pair per object as `menu_item::IntegerRange::lower`/`upper`
/// (`gui/menu_item/integer_range.h:33`) and `menu_item::KeyRange::lower`/`upper`
/// (`gui/menu_item/key_range.h:173`) -- public members of the legacy menu items themselves. They
/// are settings, not menu state: `readSettings`/`writeSettings` have always persisted them
/// (bytes 53..58) and `Song::setupDefault` has always read them, both by bare-`extern`ing the
/// *menu item* (`song.cpp:249-251`, `flash_storage.cpp:45-47`) -- which is exactly the smell that
/// says the storage is in the wrong place.
///
/// A member of a `gui/` class is unreachable from a layer that may not name `gui/` at all
/// (`src/scenes`, whose DEFAULTS page edits exactly these six values), and there is no
/// declaration-only escape from a class member the way there is from a namespace-scope global. So
/// the values live here and `IntegerRange`/`KeyRange`'s `lower`/`upper` are now references onto
/// one of these slots; every legacy spelling (`defaultTempoMenu.lower`,
/// `defaultKeyMenu.getRandomValueInRange()`) reads and writes the same storage it did before. The
/// same move, for the same reason, that `padColours` above is.
extern DefaultRange defaultRanges[NUM_DEFAULT_RANGE_SLOTS];

void readSettings();
void writeSettings();
void resetSettings();
void factoryReset(bool showPopup = true);
void resetMidiFollowSettings();
void resetAutomationSettings();

} // namespace FlashStorage
