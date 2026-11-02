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

#include "gui/ui/shell_slice_ops.h"

#include "gui/ui/shell_choice_dispatch.h"
#include "gui/ui/slice_core.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/display/display.h"
#include "model/sample/sample.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "scenes/browser/browser_scene.h"
#include "scenes/slicing/slicer_scene.h"
#include "storage/audio/latest_sample_load.h"
#include "storage/multi_range/multisample_range.h"

#include <string>

namespace deluge::gui::ui {

namespace {

using deluge::scenes::slicing::PreviewRequest;
using deluge::scenes::slicing::SliceCommit;
using deluge::scenes::slicing::slicer_scene;

/// @brief `SlicerScene`'s host: every call resolves `soundEditor` to what the ported menu edits, then does what the
///        legacy `Slicer` does through the same `SliceCore`.
class HostSliceOps final : public deluge::scenes::slicing::SliceOps {
public:
	/// The preview's sound stops, as `AudioEngine::stopAnyPreviewing`'s does; its holder stays bound, because it is
	/// what keeps the sample the slicer shows loaded.
	void stop_previewing() override { AudioEngine::sampleForPreview->killAllVoices(); }
	void kill_voices() override { core_.kill_voices(); }
	void audition(const PreviewRequest& req) override {
		if (sample_ == nullptr) {
			return;
		}
		adoptShellMenuContext();
		core_.audition(*sample_, SliceCore::AuditionTarget{.startPoint = req.start_point,
		                                                   .endPoint = req.end_point,
		                                                   .transpose = req.transpose,
		                                                   .on = req.on});
	}
	void cancel_audition() override { core_.cancel_audition(); }
	std::optional<int32_t> play_position() override {
		adoptShellMenuContext();
		return core_.play_position();
	}
	void restore_full_range() override { (void)core_.restore_full_range(); }
	bool begin_commit(const SliceCommit& commit) override {
		if (core_.slicing() || path_.empty()) {
			return false;
		}
		adoptShellMenuContext();
		SliceCore::Target target{};
		target.isManual = commit.is_manual;
		target.sliceCount = commit.slice_count;
		for (int32_t i = 0; i < MAX_MANUAL_SLICES; i++) {
			target.manualPoints[i] =
			    SliceItem{.startPos = commit.points[i].start_pos, .transpose = commit.points[i].transpose};
		}
		core_.begin_slice(target, path_);
		return true;
	}

	/// @brief Start loading @p path into the preview's drum, for the slicer to open on.
	void open(std::string_view path) {
		path_ = std::string{path};
		sample_ = nullptr;
		AudioEngine::stopAnyPreviewing(); // also unbinds the preview's holder, so the load looks the path up
		MultisampleRange* range =
		    static_cast<MultisampleRange*>(AudioEngine::sampleForPreview->sources[0].getOrCreateFirstRange());
		if (range == nullptr) {
			display->displayError(Error::INSUFFICIENT_RAM);
			deluge::scenes::browser::browser_scene().slice_ready(false);
			return;
		}
		range->sampleHolder.filePath.assign(path_);
		open_load_.request(
		    deluge::storage::SampleDemand{
		        .holder = &range->sampleHolder,
		        .manuallySelected = true,
		        .clusterLoadInstruction = CLUSTER_ENQUEUE,
		    },
		    /*context=*/nullptr, 0);
	}

	void poll() {
		poll_open();
		core_.tick_audition();
		if (const std::optional<SliceCore::SliceEnd> end = core_.tick_slice()) {
			if (end->landed) {
				// `doSlice`'s `exitAndNeverDeleteDrum` closes the menu and the browser with the slicer.
				deluge::scenes::browser::browser_scene().long_press_done(true);
				uiNeedsRendering(&instrumentClipView);
			}
			else if (end->error != Error::NONE) {
				display->displayError(end->error);
			}
			slicer_scene().commit_done(end->landed);
		}
	}

	[[nodiscard]] bool busy() const { return open_load_.pending() || core_.slicing() || core_.audition_pending(); }

private:
	void poll_open() {
		using Result = deluge::storage::LatestSampleLoad<uint32_t>::Result;
		using Reason = deluge::storage::AsyncSampleLoad::Reason;
		const auto outcome = open_load_.tick();
		if (!outcome.has_value()) {
			return;
		}
		const auto* range =
		    static_cast<const MultisampleRange*>(AudioEngine::sampleForPreview->sources[0].getOrCreateFirstRange());
		Sample* sample = range != nullptr ? static_cast<Sample*>(range->sampleHolder.audioFile) : nullptr;
		if (outcome->result == Result::Bound && sample != nullptr) {
			sample_ = sample;
			slicer_scene().open(sample);
			deluge::scenes::browser::browser_scene().slice_ready(true);
			return;
		}
		if (outcome->result == Result::Failed) {
			if (outcome->reason == Reason::Stalled || outcome->reason == Reason::CardUnusable) {
				display->displayError(Error::SD_CARD);
			}
		}
		else if (outcome->error != Error::NONE) {
			display->displayError(outcome->error);
		}
		deluge::scenes::browser::browser_scene().slice_ready(false);
	}

	SliceCore core_{};
	deluge::storage::LatestSampleLoad<uint32_t> open_load_{};
	std::string path_{};
	Sample* sample_ = nullptr;
};

HostSliceOps& host() {
	static HostSliceOps ops;
	return ops;
}

} // namespace

void wireShellSliceOps() {
	slicer_scene().set_ops(&host());
}

void openShellSlice(std::string_view path) {
	host().open(path);
}

void pollShellSlicer() {
	host().poll();
}

bool shellSlicerBusy() {
	return host().busy();
}

} // namespace deluge::gui::ui
