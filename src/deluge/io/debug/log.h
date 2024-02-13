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

#include <cstddef>
#include <cstring>
#include <string> // IWYU pragma: export (these need to be included before printf.h redefines the strformat functions)

#include "lib/printf.h" // IWYU pragma: export (non-allocating string formatting)

#ifdef __cplusplus

extern "C" {
#endif
// This is defined in display.cpp
extern void freezeWithError(char const* errmsg);
// this is defined in deluge.cpp
enum DebugPrintMode { kDebugPrintModeDefault, kDebugPrintModeRaw, kDebugPrintModeNewlined };
void logDebug(enum DebugPrintMode mode, const char* file, int line, size_t bufsize, const char* format, ...);
#ifdef __cplusplus
}
#endif

#if ENABLE_TEXT_OUTPUT
#define D_PRINTLN(...) logDebug(kDebugPrintModeNewlined, __FILE__, __LINE__, 256, __VA_ARGS__)
#define D_PRINT(...) logDebug(kDebugPrintModeDefault, __FILE__, __LINE__, 256, __VA_ARGS__)
#define D_PRINT_RAW(...) logDebug(kDebugPrintModeRaw, __FILE__, __LINE__, 256, __VA_ARGS__)

#else

#define D_PRINTLN(...)
#define D_PRINT(...)
#define D_PRINT_RAW(...)
#endif
