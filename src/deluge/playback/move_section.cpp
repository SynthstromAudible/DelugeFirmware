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
#include "io/debug/log.h"
#include "model/mod_controllable/delay_type.h"
#include "modulation/params/lane_snapshot.h"
#include "runtime/app_loop.h"

#include <algorithm>
#include <array>
#include <chrono>
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
	if (collecting_ && g_collected.depth++ == 0) {
		begin_ = g_collected.count;
	}
}

MoveEffects::Scope::~Scope() {
	if (!collecting_ || --g_collected.depth > 0) {
		return;
	}
	// No scope collects while these run, so a notify acts at once. One may make a move, which is then the outermost
	// scope: it collects after these and runs its own before this loop goes on.
	for (size_t i = begin_; i < g_collected.count; i++) {
		g_runner(g_collected.notifies[i]);
	}
	g_collected.count = begin_;
}

#if ALPHA_OR_BETA_VERSION || defined(DELUGE_HOST)
namespace {
/// Sites measured, in the order first seen; a site past the room is not measured. All-zero at start: .sdram_bss.
constexpr size_t kSpanSites = 48;
struct Spans {
	std::array<MaskedSpan::Site, kSpanSites> sites;
	size_t count;
};
PLACE_SDRAM_BSS Spans g_spans;

/// µs: the app loop's clock on the device; wall time on the host, whose harness may run the loop on virtual time.
uint64_t spanClockUs() {
#if defined(DELUGE_HOST)
	return static_cast<uint64_t>(
	    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
	        .count());
#else
	return deluge::runtime::nowUs();
#endif
}
} // namespace

MaskedSpan::MaskedSpan(const char* site)
    : site_(deluge::memory::onRenderTier() ? nullptr : site), startUs_(site_ != nullptr ? spanClockUs() : 0) {
}

MaskedSpan::~MaskedSpan() {
	if (site_ != nullptr) {
		record(site_, static_cast<uint32_t>(spanClockUs() - startUs_));
	}
}

void MaskedSpan::record(const char* site, uint32_t us) {
	for (size_t i = 0; i < g_spans.count; i++) {
		MaskedSpan::Site& known = g_spans.sites[i];
		if (known.name == site) {
			known.longestUs = std::max(known.longestUs, us);
			known.count++;
			return;
		}
	}
	if (g_spans.count < kSpanSites) {
		g_spans.sites[g_spans.count++] = {.name = site, .longestUs = us, .count = 1};
	}
}

uint64_t MaskedSpan::nowUs() {
	return spanClockUs();
}

void MaskedSpan::forEachSite(void (*visit)(const Site&)) {
	for (size_t i = 0; i < g_spans.count; i++) {
		visit(g_spans.sites[i]);
	}
}

void MaskedSpan::report() {
	forEachSite([](const Site& site) {
		D_PRINTLN("PM-SPAN site=%s longest_us=%u count=%u", site.name, static_cast<unsigned>(site.longestUs),
		          static_cast<unsigned>(site.count));
	});
#if ALPHA_OR_BETA_VERSION
	D_PRINTLN("LANES-MEM bytes=%u",
	          static_cast<unsigned>(deluge::modulation::publishedBytes.load(std::memory_order_relaxed)));
#endif
}
#else
void MaskedSpan::forEachSite(void (* /*visit*/)(const Site&)) {
}
void MaskedSpan::report() {
}
#endif

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
