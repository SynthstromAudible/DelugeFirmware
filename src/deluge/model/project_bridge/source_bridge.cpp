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

#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "storage/multi_range/multisample_range.h"

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
		if (ranges[i].file_path != now[i].file_path || (i > 0 && ranges[i].top_note <= ranges[i - 1].top_note)) {
			return Error::UNSPECIFIED;
		}
	}
	{
		AudioExcludedSection section; // the render reads these ranges' zones and tuning
		for (size_t i = 0; i < ranges.size(); i++) {
			MultiRange* range = source.ranges.getElement(static_cast<int32_t>(i));
			range->topNote = ranges[i].top_note;
			if (source.oscType != OscType::SAMPLE) {
				continue;
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
			if (zoneMoved && holder.audioFile != nullptr) {
				holder.claimClusterReasons(source.sampleControls.isCurrentlyReversed(), CLUSTER_ENQUEUE);
			}
		}
	}
	source.seedDocRanges();
	return Error::NONE;
}

} // namespace deluge::bridge
