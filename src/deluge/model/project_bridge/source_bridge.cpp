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

std::vector<synth::SourceRange> rangesOf(const ::SourceResources& source) {
	return source.heldRanges();
}

namespace {

/// Where @p longer holds one range @p shorter does not, the others in order with their files and zones; nullopt when
/// there is no such range. With @p fileless, only a range holding no file counts (a new range has none). Ranges alike
/// in file and zone (empty new ones) are told apart by their top notes, of which only the extra one's lower neighbour
/// may have moved; failing that, by file and zone alone.
std::optional<size_t> extraAt(const std::vector<synth::SourceRange>& shorter,
                              const std::vector<synth::SourceRange>& longer, bool fileless) {
	const auto same = [](const synth::SourceRange& a, const synth::SourceRange& b) {
		return a.file_path == b.file_path && a.zone == b.zone;
	};
	const auto find = [&](bool by_top) -> std::optional<size_t> {
		for (size_t at = 0; at < longer.size(); at++) {
			bool rest = !fileless || longer[at].file_path.empty();
			for (size_t i = 0; rest && i < shorter.size(); i++) {
				const synth::SourceRange& other = longer[i < at ? i : i + 1];
				rest = same(shorter[i], other) && (!by_top || i + 1 == at || shorter[i].top_note == other.top_note);
			}
			if (rest) {
				return at;
			}
		}
		return std::nullopt;
	};
	if (const std::optional<size_t> at = find(true)) {
		return at;
	}
	return find(false);
}

/// Whether @p ranges can replace @p now in place: the same length, each range keeping its file, and the top notes in
/// order wherever one moves or is @p added (two ranges a malformed file left on one top note stand).
bool takes(const std::vector<synth::SourceRange>& now, const std::vector<synth::SourceRange>& ranges,
           std::optional<size_t> added = std::nullopt) {
	if (now.size() != ranges.size()) {
		return false;
	}
	for (size_t i = 0; i < ranges.size(); i++) {
		if (ranges[i].file_path != now[i].file_path) {
			return false;
		}
		const bool moved = ranges[i].top_note != now[i].top_note || added == i;
		if (moved
		    && ((i > 0 && ranges[i].top_note <= ranges[i - 1].top_note)
		        || (i + 1 < ranges.size() && ranges[i + 1].top_note <= ranges[i].top_note))) {
			return false;
		}
	}
	return true;
}

} // namespace

Error applyRanges(::Sound& sound, int32_t s, const std::vector<synth::SourceRange>& ranges) {
	::SourceResources& source = sound.sourceResources[s];
	const std::vector<synth::SourceRange> now = rangesOf(source);
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
			// Judged before anything changes: the list as it will be once the range is in
			std::vector<synth::SourceRange> next = now;
			next.insert(next.begin() + static_cast<std::ptrdiff_t>(*at), ranges[*at]);
			if (!takes(next, ranges, *at)) {
				return Error::UNSPECIFIED;
			}
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
			std::vector<synth::SourceRange> next = now;
			next.erase(next.begin() + static_cast<std::ptrdiff_t>(*gone));
			if (!takes(next, ranges)) {
				return Error::UNSPECIFIED;
			}
			sound.deleteMultiRange(s, static_cast<int32_t>(*gone));
			return applyRanges(sound, s, ranges);
		}
	}
	if (!takes(now, ranges)) {
		return Error::UNSPECIFIED;
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
			const bool zoneMoved = holder.startPos != ranges[i].zone.start || holder.endPos != ranges[i].zone.end
			                       || holder.loopStartPos != ranges[i].zone.loop_start
			                       || holder.loopEndPos != ranges[i].zone.loop_end;
			holder.startPos = ranges[i].zone.start;
			holder.endPos = ranges[i].zone.end;
			holder.loopStartPos = ranges[i].zone.loop_start;
			holder.loopEndPos = ranges[i].zone.loop_end;
			holder.transpose = ranges[i].tuning.transpose;
			holder.setCents(ranges[i].tuning.cents);
			if (zoneMoved && holder.audioFile != nullptr && sound.oscType(s) == OscType::SAMPLE) {
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

Error applyDx7Patch(::Sound& sound, const deluge::instruments::classic::DxPatchState& patch) {
	if (sound.getSynthMode() != SynthMode::DX7) {
		return Error::UNSPECIFIED;
	}
	if (sound.dx7Patch.state() != patch && !sound.dx7Patch.assign(patch)) {
		return Error::INSUFFICIENT_RAM;
	}
	sound.recordHeldDx7Patch();
	return Error::NONE;
}

} // namespace deluge::bridge
