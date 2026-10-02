/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "storage/audio/load_trace.h"

#ifdef DELUGE_HOST

#include <cstdarg>
#include <cstdio>

namespace deluge::storage::load_trace {

namespace {
Sink g_sink = nullptr;
} // namespace

Sink setSink(Sink newSink) {
	const Sink previous = g_sink;
	g_sink = newSink;
	return previous;
}

void emit(const char* format, ...) {
	if (g_sink == nullptr) {
		return;
	}
	char line[512];
	va_list args;
	va_start(args, format);
	(void)vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	g_sink(line);
}

} // namespace deluge::storage::load_trace

#endif // DELUGE_HOST
