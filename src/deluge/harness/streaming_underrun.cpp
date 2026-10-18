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

#include "harness/streaming_underrun.h"

#if DELUGE_UNDERRUN_COUNTERS

#include "libdeluge/tier.h"

#include <array>
#include <atomic>

namespace deluge::harness {

namespace {
// Relaxed atomics: these are counters, not synchronization. A reader may load them from a
// different thread than the audio-render thread that increments them; relaxed is sufficient
// because the harness only cares about the eventual totals, not ordering against other memory
// operations.
std::atomic<uint64_t> underrunWaitCount{0};
std::atomic<uint64_t> underrunUnassignCount{0};
std::array<std::atomic<uint64_t>, 3> voiceRenderCount{};
} // namespace

void noteVoiceRender() {
	const uint8_t tier = deluge_tier_current();
	if (tier < voiceRenderCount.size()) {
		voiceRenderCount[tier].fetch_add(1, std::memory_order_relaxed);
	}
}

uint64_t voiceRenderCountLoad(uint8_t tier) {
	return tier < voiceRenderCount.size() ? voiceRenderCount[tier].load(std::memory_order_relaxed) : 0;
}

void noteUnderrunWait() {
	underrunWaitCount.fetch_add(1, std::memory_order_relaxed);
}

void noteUnderrunUnassign() {
	underrunUnassignCount.fetch_add(1, std::memory_order_relaxed);
}

uint64_t underrunWaitCountLoad() {
	return underrunWaitCount.load(std::memory_order_relaxed);
}

uint64_t underrunUnassignCountLoad() {
	return underrunUnassignCount.load(std::memory_order_relaxed);
}

} // namespace deluge::harness

extern "C" {

uint64_t deluge_sim_underrun_wait_count() {
	return deluge::harness::underrunWaitCountLoad();
}

uint64_t deluge_sim_underrun_unassign_count() {
	return deluge::harness::underrunUnassignCountLoad();
}

uint64_t deluge_sim_voice_render_count(uint8_t tier) {
	return deluge::harness::voiceRenderCountLoad(tier);
}

} // extern "C"

#endif // DELUGE_UNDERRUN_COUNTERS
