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

#ifndef DELUGE_H_
#define DELUGE_H_

#ifdef __cplusplus
extern "C" {
#endif

extern int main(void);
extern int deluge_main(void);

extern void timerGoneOff(void);

extern void routineWithClusterLoading(void);
extern void loadAnyEnqueuedClustersRoutine(void);

extern void logAudioAction(char const* string);

// This is defined in numericdriver.cpp
extern void freezeWithError(char const* errmsg);

extern void routineForSD(void);
extern void sdCardInserted(void);
extern void sdCardEjected(void);

extern void setTimeUSBInitializationEnds(int timeFromNow);

#ifdef __cplusplus
}
#endif

#endif /* DELUGE_H_ */
