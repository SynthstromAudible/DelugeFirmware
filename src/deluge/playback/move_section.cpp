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

#include "playback/move_section.h"

#include <array>
#include <cstddef>

namespace deluge::playback {

#if defined(DELUGE_HOST)
namespace {
std::array<void (*)(), static_cast<size_t>(InjectAt::kCount)> g_injections{};
} // namespace

void setRenderInjection(InjectAt at, void (*hook)()) {
	g_injections[static_cast<size_t>(at)] = hook;
}

void injectRenderAt(InjectAt at) {
	void (*hook)() = g_injections[static_cast<size_t>(at)];
	if (hook != nullptr) {
		g_injections[static_cast<size_t>(at)] = nullptr;
		hook();
	}
}
#endif

} // namespace deluge::playback
