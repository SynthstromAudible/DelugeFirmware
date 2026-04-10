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

#include "log.h"

#include "io/debug/print.h"
#include "scheduler_api.h"
#include "util/functions.h"
#include <cstdarg>

#ifdef __cplusplus
extern "C" {
#endif

#if ENABLE_TEXT_OUTPUT
// only called from the D_PRINT macros
void logDebug(enum DebugPrintMode mode, const char* file, int line, size_t bufsize, const char* format, ...) {
	static char buffer[512];
	va_list args;

	// Start variadic argument processing
	va_start(args, format);
	// Compose the log message into the buffer
	const char* baseFile = getFileNameFromEndOfPath(file);
	if (mode == kDebugPrintModeRaw) {
		vsnprintf(buffer, bufsize, format, args);
	}
	else {
		snprintf(buffer, sizeof(buffer), "%.4f: ", getSystemTime());
		snprintf(buffer + strlen(buffer), sizeof(buffer), "%s:%d: ", baseFile, line);
		vsnprintf(buffer + strlen(buffer), bufsize - strlen(buffer), format, args);
	}
	// End variadic argument processing
	va_end(args);

	// Pass the buffer to another logging library
	if (mode == kDebugPrintModeNewlined) {
		Debug::println(buffer);
	}
	else {
		Debug::print(buffer);
	}
}
#endif

#ifdef __cplusplus
}
#endif
