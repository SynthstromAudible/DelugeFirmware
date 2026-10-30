/*
 * Copyright © 2023 Synthstrom Audible Limited
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
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
extern int main(void);

extern void logAudioAction(char const* string);

extern void consoleTextIfAllBootedUp(char const* text);

/// Starts a console-text quiet window that ends @p timeFromNow samples from now.
extern void setTimeUSBInitializationEnds(int32_t timeFromNow);

/// Whether the console-text quiet window (one second after boot, or one setTimeUSBInitializationEnds
/// started) has ended.
extern bool usbInitializationPeriodComplete(void);

#ifdef __cplusplus
}

namespace Deluge {
void factoryReset();
}
#endif
