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

// View::activeModControllableModelStack's single owner: only the interaction tier writes the stack, and the render asks
// it for changes through deluge::playback's request queue (playback/render_requests.h).

#include "gui/views/view.h"
#include "model/clip/clip.h"
#include "model/clip/clip_array.h"
#include "model/model_stack.h"
#include "model/output.h"
#include "model/song/song.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iterator>

bool View::activeModControllableNamesSong(Song* song) const {
	ModelStackWithThreeMainThings const& stack = activeModControllableModelStack;
	TimelineCounter const* counter = stack.getTimelineCounterAllowNull();
	if (counter == song || stack.modControllable == &song->project.global_fx.global_effectable) {
		return true;
	}
	for (ClipArray const* array : {&song->live.clips.session_clips, &song->live.clips.arrangement_only_clips}) {
		for (Clip const* clip : *array) {
			if (counter == clip) {
				return true;
			}
		}
	}
	if (stack.modControllable != nullptr) {
		for (Output* first :
		     {song->live.media.first_output, static_cast<Output*>(song->live.media.first_hibernating_instrument)}) {
			for (Output* output = first; output != nullptr; output = output->next) {
				if (stack.modControllable == output->toModControllable()) {
					return true;
				}
			}
		}
	}
	return false;
}

void View::followRecordingToClip(Clip* from, Clip* to) {
	ModelStackWithThreeMainThings& stack = activeModControllableModelStack;
	if (stack.getTimelineCounterAllowNull() == from) {
		stack.setTimelineCounter(to);
		stack.paramManager = &to->paramManager;
	}
}

#if defined(DELUGE_HOST)
namespace {
std::atomic<bool> g_watchRenderStackWrites{false};
std::atomic<int32_t> g_renderStackWrites{0};

void snapshotStack(void const* (&out)[4]) {
	ModelStackWithThreeMainThings const& stack = view.activeModControllableModelStack;
	out[0] = stack.song;
	out[1] = stack.getTimelineCounterAllowNull();
	out[2] = stack.modControllable;
	out[3] = stack.paramManager;
}
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
		snapshotStack(before_);
	}
}

View::RenderBlockStackWatch::~RenderBlockStackWatch() {
	if (!watching_) {
		return;
	}
	void const* after[4];
	snapshotStack(after);
	if (!std::equal(std::begin(before_), std::end(before_), std::begin(after))) {
		g_renderStackWrites.fetch_add(1, std::memory_order_relaxed);
	}
}
#endif
