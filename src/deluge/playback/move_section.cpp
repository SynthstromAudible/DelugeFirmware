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

#include "definitions_cxx.hpp"
#include "model/mod_controllable/delay_type.h"

#include <array>
#include <cstddef>

namespace deluge::playback {

namespace {

/// Delay notifies one move may defer. A move notifies each lane it moves, but only a delay's rate or feedback lanes are
/// deferred: a few per owner whose delay they reach. A notify past the room acts at once, inside the section, where the
/// host's masked-allocation and masked-action counts show it.
constexpr size_t kRoom = 32;

/// [interaction] Recorded notifies, in order. All-zero at start: it lives in .sdram_bss.
struct Collected {
	std::array<MoveEffects::DelayNotify, kRoom> notifies;
	size_t count;
	/// Scopes the interaction tier is inside; the outermost one's end runs what they collected.
	int32_t depth;
};
PLACE_SDRAM_BSS Collected g_collected;

void runDelayNotify(const MoveEffects::DelayNotify& notify) {
	deluge::model::onDelayParamModified(notify.stack, notify.oldValue, notify.automatedBefore);
}

void (*g_runner)(const MoveEffects::DelayNotify&) = &runDelayNotify;

} // namespace

bool MoveEffects::collecting() {
	return g_collected.depth > 0 && !deluge::memory::onRenderTier();
}

bool MoveEffects::defer(const DelayNotify& notify) {
	if (g_collected.count == kRoom) {
		return false;
	}
	g_collected.notifies[g_collected.count++] = notify;
	return true;
}

#if defined(DELUGE_HOST)
void MoveEffects::setRunnerForTest(void (*runner)(const DelayNotify&)) {
	g_runner = runner != nullptr ? runner : &runDelayNotify;
}
#endif

MoveEffects::Scope::Scope() : collecting_(!deluge::memory::onRenderTier()) {
	if (collecting_) {
		g_collected.depth++;
	}
}

MoveEffects::Scope::~Scope() {
	if (!collecting_ || --g_collected.depth > 0) {
		return;
	}
	// Taken before running: a notify run here acts at once (no scope is collecting), and may run another move.
	const size_t count = g_collected.count;
	g_collected.count = 0;
	for (size_t i = 0; i < count; i++) {
		g_runner(g_collected.notifies[i]);
	}
}

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
