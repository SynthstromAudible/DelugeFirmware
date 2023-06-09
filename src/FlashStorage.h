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

#ifndef FLASHSTORAGE_H_
#define FLASHSTORAGE_H_

#include "r_typedefs.h"

#define PREVIEW_OFF 0
#define PREVIEW_ONLY_WHILE_NOT_PLAYING 1
#define PREVIEW_ON 2

namespace FlashStorage {

extern uint8_t defaultScale;
extern bool audioClipRecordMargins;
extern uint8_t keyboardLayout;
extern uint8_t
    recordQuantizeLevel; // Assumes insideWorldTickMagnitude==1, which is not default anymore, so adjust accordingly
extern uint8_t sampleBrowserPreviewMode;
extern uint8_t defaultVelocity;
extern int8_t defaultMagnitude;
extern bool settingsBeenRead;
extern uint8_t defaultBendRange[2];

void readSettings();
void writeSettings();
void resetSettings();

} // namespace FlashStorage

#endif /* FLASHSTORAGE_H_ */
