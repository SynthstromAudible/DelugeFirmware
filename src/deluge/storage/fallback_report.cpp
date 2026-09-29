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

#include "storage/fallback_report.h"

#ifdef DELUGE_HOST

#include "libdeluge/boot.h"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <set>
#include <string>

namespace {
std::atomic<uint32_t> g_calls{0};
std::mutex g_reportedLock;
std::set<std::string> g_reported;
} // namespace

namespace deluge::storage {

void noteFallbackCalled(const char* name) noexcept {
	if (!deluge_boot_finished()) {
		return;
	}
	g_calls.fetch_add(1, std::memory_order_relaxed);
	// The fallbacks this is called from are noexcept. The count above is the evidence; the line is a courtesy, so an
	// allocation failure only costs the line.
	try {
		const std::lock_guard lock{g_reportedLock};
		if (g_reported.insert(name).second) {
			std::fprintf(stderr, "FALLBACK %s called after boot: this build links no driver for its channel\n", name);
		}
	} catch (...) {}
}

} // namespace deluge::storage

extern "C" uint32_t deluge_storage_fallback_calls(void) {
	return g_calls.load(std::memory_order_relaxed);
}

#else

extern "C" uint32_t deluge_storage_fallback_calls(void) {
	return 0;
}

#endif
