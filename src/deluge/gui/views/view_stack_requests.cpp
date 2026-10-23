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

// The knobs' render-visible words: only the interaction tier writes them, and the render asks for a release through
// deluge::playback's request queue (playback/render_requests.h).

#include "gui/views/view.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iterator>

#if defined(DELUGE_HOST)
namespace {
std::atomic<bool> g_watchRenderStackWrites{false};
std::atomic<int32_t> g_renderStackWrites{0};
} // namespace

void View::watchRenderStackWrites(bool on) {
	g_renderStackWrites.store(0, std::memory_order_relaxed);
	g_watchRenderStackWrites.store(on, std::memory_order_release);
}

int32_t View::renderStackWrites() {
	return g_renderStackWrites.load(std::memory_order_acquire);
}

View::RenderBlockStackWatch::RenderBlockStackWatch()
    : watching_{g_watchRenderStackWrites.load(std::memory_order_acquire)} {
	if (watching_) {
		before_[0] = view.knobSong_;
		before_[1] = view.knobParamManager_.load(std::memory_order_relaxed);
	}
}

View::RenderBlockStackWatch::~RenderBlockStackWatch() {
	if (!watching_) {
		return;
	}
	void const* after[2]{view.knobSong_, view.knobParamManager_.load(std::memory_order_relaxed)};
	if (!std::equal(std::begin(before_), std::end(before_), std::begin(after))) {
		g_renderStackWrites.fetch_add(1, std::memory_order_relaxed);
	}
}
#endif
