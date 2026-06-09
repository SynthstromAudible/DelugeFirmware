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
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
extern int main(void);
extern int32_t deluge_main(void);

extern void midiAndGateTimerGoneOff(void);

extern void routineWithClusterLoading(void);

extern void logAudioAction(char const* string);

extern void consoleTextIfAllBootedUp(char const* text);
// The SD-wait yield hooks below are the runtime side of a libdeluge boundary
// service — their canonical contract is <libdeluge/storage_wait.h>, which the BSP
// consumes. These mirror declarations stay here because deluge.h is included very
// widely (incl. vendored libs) and must not depend on the boundary headers; they
// are kept signature-identical and are removed once deluge.h is slimmed down.
typedef bool (*RunCondition)();
bool yieldingRoutineWithTimeoutForSD(RunCondition until, double timeoutSeconds);
void yieldingRoutineForSD(RunCondition until);

extern void setTimeUSBInitializationEnds(int32_t timeFromNow);

#ifdef __cplusplus
}
#endif
