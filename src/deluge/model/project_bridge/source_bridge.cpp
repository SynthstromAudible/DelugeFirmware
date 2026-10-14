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

#include "model/project_bridge/source_bridge.h"

#include "dsp/dx/dx7note.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "storage/multi_range/multisample_range.h"

#include <vector>

namespace deluge::bridge {

std::vector<deluge::instrument::RangeState> rangesOf(const ::Source& source) {
	deluge::instrument::SourceState into;
	source.seedRangesInto(into);
	return std::move(into.ranges);
}

Error applyRanges(::Sound& sound, int32_t s, const std::vector<deluge::instrument::RangeState>& ranges) {
	::Source& source = sound.sources[s];
	const std::vector<deluge::instrument::RangeState> now = rangesOf(source);
	if (now == ranges) {
		source.seedDocRanges(); // a gesture changed the runtime itself and records the result
		return Error::NONE;
	}
	if (now.size() != ranges.size()) {
		return Error::UNSPECIFIED;
	}
	for (size_t i = 0; i < ranges.size(); i++) {
		if (ranges[i].file_path != now[i].file_path) {
			return Error::UNSPECIFIED;
		}
		// Order is judged only where the list changes: two ranges a malformed file left on one top note stand.
		const bool moved = ranges[i].top_note != now[i].top_note;
		if (moved
		    && ((i > 0 && ranges[i].top_note <= ranges[i - 1].top_note)
		        || (i + 1 < ranges.size() && ranges[i + 1].top_note <= ranges[i].top_note))) {
			return Error::UNSPECIFIED;
		}
	}
	std::vector<SampleHolderForVoice*> moved; // claimed once the render is free again
	{
		AudioExcludedSection section; // the render reads these ranges' zones and tuning
		for (size_t i = 0; i < ranges.size(); i++) {
			MultiRange* range = source.ranges.getElement(static_cast<int32_t>(i));
			range->topNote = ranges[i].top_note;
			if (source.ranges.currentElementSize() != sizeof(MultisampleRange)) {
				continue; // a wavetable range has no zone; sample ranges keep theirs whatever the type
			}
			SampleHolderForVoice& holder = static_cast<MultisampleRange*>(range)->sampleHolder;
			const bool zoneMoved = holder.startPos != ranges[i].start_pos || holder.endPos != ranges[i].end_pos
			                       || holder.loopStartPos != ranges[i].loop_start_pos
			                       || holder.loopEndPos != ranges[i].loop_end_pos;
			holder.startPos = ranges[i].start_pos;
			holder.endPos = ranges[i].end_pos;
			holder.loopStartPos = ranges[i].loop_start_pos;
			holder.loopEndPos = ranges[i].loop_end_pos;
			holder.transpose = ranges[i].transpose;
			holder.setCents(ranges[i].cents);
			if (zoneMoved && holder.audioFile != nullptr && source.oscType == OscType::SAMPLE) {
				moved.push_back(&holder);
			}
		}
	}
	// A claim reaches the sample reserve, which must not run while the render is excluded. Only a playing sample
	// source's zones are claimed.
	for (SampleHolderForVoice* holder : moved) {
		holder->claimClusterReasons(source.sampleControls.isCurrentlyReversed(), CLUSTER_ENQUEUE);
	}
	source.seedDocRanges();
	return Error::NONE;
}

std::optional<deluge::instrument::DxPatchState> dxPatchOf(const ::Source& source) {
	deluge::instrument::SourceState into;
	source.seedDxPatchInto(into);
	return into.dx_patch;
}

Error applyDxPatch(::Sound& sound, int32_t s, const deluge::instrument::DxPatchState& patch) {
	::Source& source = sound.sources[s];
	if (source.oscType != OscType::DX7) {
		return Error::UNSPECIFIED;
	}
	if (dxPatchOf(source) != patch) {
		DxPatch* runtime = source.ensureDxPatch();
		std::ranges::copy(patch.params, runtime->params);
		if (runtime->engineMode != patch.engine_mode) {
			runtime->setEngineMode(patch.engine_mode);
		}
		runtime->random_detune = patch.random_detune;
		source.dxPatchChanged = true;
	}
	source.seedDocDxPatch();
	return Error::NONE;
}

} // namespace deluge::bridge
