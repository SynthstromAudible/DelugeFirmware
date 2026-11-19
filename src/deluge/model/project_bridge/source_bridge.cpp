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
#include <optional>

#include "processing/dx7/dx_patch.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/source_resources.h"
#include "storage/multi_range/multisample_range.h"

#include <vector>

namespace deluge::bridge {

std::vector<deluge::instrument::RangeState> rangesOf(const ::SourceResources& source) {
	deluge::instrument::SourceState into;
	source.seedRangesInto(into);
	return std::move(into.ranges);
}

namespace {

/// Where @p longer holds one range @p shorter does not, the others in order with their files and zones; nullopt when
/// there is no such range. With @p fileless, only a range holding no file counts (a new range has none).
std::optional<size_t> extraAt(const std::vector<deluge::instrument::RangeState>& shorter,
                              const std::vector<deluge::instrument::RangeState>& longer, bool fileless) {
	const auto same = [](const deluge::instrument::RangeState& a, const deluge::instrument::RangeState& b) {
		return a.file_path == b.file_path && a.start_pos == b.start_pos && a.end_pos == b.end_pos
		       && a.loop_start_pos == b.loop_start_pos && a.loop_end_pos == b.loop_end_pos;
	};
	for (size_t at = 0; at < longer.size(); at++) {
		bool rest = !fileless || longer[at].file_path.empty();
		for (size_t i = 0; rest && i < shorter.size(); i++) {
			rest = same(shorter[i], longer[i < at ? i : i + 1]);
		}
		if (rest) {
			return at;
		}
	}
	return std::nullopt;
}

} // namespace

Error applyRanges(::Sound& sound, int32_t s, const std::vector<deluge::instrument::RangeState>& ranges) {
	::SourceResources& source = sound.sources[s];
	const std::vector<deluge::instrument::RangeState> now = rangesOf(source);
	if (now == ranges) {
		sound.recordHeldRanges(s); // a gesture changed the runtime itself and records the result
		return Error::NONE;
	}
	if (now.empty() && ranges.size() == 1 && ranges[0].file_path.empty()) {
		// A source's first range, which an editor of its zone makes before any file is loaded into it.
		if (sound.getOrCreateFirstRange(s) == nullptr) {
			return Error::INSUFFICIENT_RAM;
		}
		return applyRanges(sound, s, ranges);
	}
	// One range more, holding no file: the range editor's insert. One fewer: its delete. The others keep their files
	// and zones, which is how the changed one is found; any other change of length is refused.
	if (ranges.size() == now.size() + 1) {
		if (const std::optional<size_t> at = extraAt(now, ranges, true)) {
			sound.killAllVoices(); // range storage is about to change
			::MultiRange* inserted = nullptr;
			{
				AudioEngine::AudioTickHold hold;
				inserted = source.ranges.insertMultiRange(static_cast<int32_t>(*at));
			}
			if (inserted == nullptr) {
				return Error::INSUFFICIENT_RAM;
			}
			inserted->topNote = ranges[*at].top_note;
			return applyRanges(sound, s, ranges);
		}
	}
	if (ranges.size() + 1 == now.size()) {
		if (const std::optional<size_t> gone = extraAt(ranges, now, false)) {
			sound.deleteMultiRange(s, static_cast<int32_t>(*gone));
			return applyRanges(sound, s, ranges);
		}
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
			if (zoneMoved && holder.audioFile != nullptr && sound.config().sources[s].osc_type == OscType::SAMPLE) {
				moved.push_back(&holder);
			}
		}
	}
	// A claim reaches the sample reserve, which must not run while the render is excluded. Only a playing sample
	// source's zones are claimed.
	for (SampleHolderForVoice* holder : moved) {
		holder->claimClusterReasons(sound.playsReversed(s), CLUSTER_ENQUEUE);
	}
	sound.recordHeldRanges(s);
	return Error::NONE;
}

std::optional<deluge::instrument::DxPatchState> dxPatchOf(const ::SourceResources& source) {
	deluge::instrument::SourceState into;
	source.seedDxPatchInto(into);
	return into.dx_patch;
}

Error applyDxPatch(::Sound& sound, int32_t s, const deluge::instrument::DxPatchState& patch) {
	::SourceResources& source = sound.sources[s];
	if (sound.config().sources[s].osc_type != OscType::DX7) {
		return Error::UNSPECIFIED;
	}
	if (dxPatchOf(source) != patch) {
		DxPatch* runtime = source.ensureDxPatch();
		if (runtime == nullptr) {
			return Error::INSUFFICIENT_RAM;
		}
		std::ranges::copy(patch.params, runtime->params.begin());
		runtime->setEngineMode(patch.engine_mode); // the engine follows the bytes as well as the mode
		runtime->random_detune = patch.random_detune;
		source.dxPatchChanged = true;
	}
	sound.recordHeldDxPatch(s);
	return Error::NONE;
}

} // namespace deluge::bridge
