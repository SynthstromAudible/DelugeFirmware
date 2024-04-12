/*
 * Copyright © 2014-2023 Mark Adams
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
#ifndef DELUGE_TASK_SCHEDULER_H
#define DELUGE_TASK_SCHEDULER_H
#ifdef __cplusplus
extern "C" {
#endif

#include "RZA1/ostm/ostm.h"

/// void function with no arguments
typedef void (*Task_Handle)();
typedef int8_t taskID;
/// highest priority is 0. Attempt to schedule the task every maxTime after minTime has passed, in priority order
/// if runToCompletion is false the task will be interrupted by higher priority requirements, otherwise it will run
/// until it returns. returns the index of the task in the global table
uint8_t registerTask(Task_Handle handle, uint8_t priority, double minTimeBetweenCalls, double targetTimeBetweenCalls,
                     double maxTimeBetweenCalls, bool runToCompletion);
void removeTask(taskID id);
/// start the task scheduler
void startTaskManager();
#ifdef __cplusplus
}
#endif
#endif // DELUGE_TASK_SCHEDULER_H
