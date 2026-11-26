/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "model/sample/waveform_scan.h"
#include "board_config.h" // PLACE_SDRAM_TEXT: thread mode, once a render or an idle tick
#include "definitions_cxx.hpp"
#include "deluge_storage_sample_peaks.h"
#include "model/sample/sample.h"
#include "model/sample/sample_reader_bridge.h"
#include "model/sample/sample_recorder.h"
#include "processing/engines/audio_engine.h"
#include "sync/sd_access.h"
#include <algorithm>
#include <optional>

namespace deluge::sample {

namespace {

/// Read the 256-frame block at frame @p from and fold it into @p peaks. False if a cluster under it is not resident:
/// the read has requested its fill, and the tap folds the whole cluster when it lands.
PLACE_SDRAM_TEXT bool fold_block_from_frames(Sample& sample, DelugePeakIndex* peaks, uint64_t from) {
	constexpr uint64_t kBlockFrames = uint64_t{1} << 8;
	const uint64_t end = std::min(from + kBlockFrames, sample.lengthInSamples);
	const uint32_t frameBytes = sample.numChannels * sample.byteDepth;
	SampleFrameReader reader{source_id_for(sample), from, 1, DELUGE_READ_SCAN};
	DelugePeak block{.min = INT16_MAX, .max = INT16_MIN};
	for (uint64_t at = from; at < end;) {
		const DelugeFrameWindow window = reader.window();
		if (!reader.ok() || window.frames == nullptr || window.frame_count == 0) {
			return false;
		}
		const auto frames = static_cast<uint32_t>(std::min<uint64_t>(window.frame_count, end - at));
		const DelugeMaybePeak p = deluge_peaks_of_frames(static_cast<const uint8_t*>(window.frames),
		                                                 frames * frameBytes, static_cast<uint8_t>(sample.byteDepth));
		if (p.has) {
			block = {.min = std::min(block.min, p.peak.min), .max = std::max(block.max, p.peak.max)};
		}
		at += frames;
		reader.advance(frames);
	}
	deluge_peaks_fold_block(peaks, from >> 8, block);
	return true;
}

/// The peak of frames [lo, hi) read from the sample, and each whole block among them folded back into its index; or
/// nothing if a cluster under them is not resident (its fill is requested).
PLACE_SDRAM_TEXT std::optional<DelugePeak> read_peak(Sample& sample, DelugePeakIndex* peaks, uint64_t lo, uint64_t hi) {
	constexpr uint64_t kBlockFrames = uint64_t{1} << 8;
	const uint32_t frameBytes = sample.numChannels * sample.byteDepth;
	const auto depth = static_cast<uint8_t>(sample.byteDepth);
	SampleFrameReader reader{source_id_for(sample), lo, 1, DELUGE_READ_CACHED};
	DelugePeak all{.min = INT16_MAX, .max = INT16_MIN};
	DelugePeak block{.min = INT16_MAX, .max = INT16_MIN};
	for (uint64_t at = lo; at < hi;) {
		const DelugeFrameWindow window = reader.window();
		if (!reader.ok() || window.frames == nullptr || window.frame_count == 0) {
			return std::nullopt;
		}
		// Up to the window's end, the range's end or the next block boundary, whichever comes first.
		const uint64_t blockEnd = (at | (kBlockFrames - 1)) + 1;
		const auto frames = static_cast<uint32_t>(std::min({uint64_t{window.frame_count}, hi - at, blockEnd - at}));
		const DelugeMaybePeak p =
		    deluge_peaks_of_frames(static_cast<const uint8_t*>(window.frames), frames * frameBytes, depth);
		if (p.has) {
			all = {.min = std::min(all.min, p.peak.min), .max = std::max(all.max, p.peak.max)};
			block = {.min = std::min(block.min, p.peak.min), .max = std::max(block.max, p.peak.max)};
		}
		const uint64_t next = at + frames;
		// A block read whole, from its first frame to its last (or the file's end): keep it.
		if ((next == blockEnd || next == sample.lengthInSamples) && (blockEnd - kBlockFrames >= lo)) {
			deluge_peaks_fold_block(peaks, at >> 8, block);
		}
		if (next == blockEnd) {
			block = {.min = INT16_MAX, .max = INT16_MIN};
		}
		at = next;
		reader.advance(frames);
	}
	return all;
}

/// The column frames [lo, hi): its plan's stored peaks and the frames at its ends.
PLACE_SDRAM_TEXT deluge::ui::views::waveform::ColumnRead column_of(Sample& sample, DelugePeakIndex* peaks, uint64_t lo,
                                                                   uint64_t hi) {
	using deluge::ui::views::waveform::ColumnRead;
	DelugePeaksColumn plan = deluge_peaks_column(peaks, lo, hi);
	if (plan.pending) {
		// Ask for what it lacks, as the background scan would: the first unscanned block under it, read (which requests
		// its cluster's fill, whose tap folds the whole cluster) or folded at once if its frames are resident.
		if (const uint64_t first = deluge_peaks_next_unscanned(peaks, lo);
		    first < hi && fold_block_from_frames(sample, peaks, first)) {
			plan = deluge_peaks_column(peaks, lo, hi);
		}
		if (plan.pending) {
			return ColumnRead::pending();
		}
	}
	DelugePeak acc = plan.has_interior ? plan.interior : DelugePeak{.min = INT16_MAX, .max = INT16_MIN};
	for (uint8_t r = 0; r < plan.read_count && r < 2; r++) {
		const std::optional<DelugePeak> p = read_peak(sample, peaks, plan.read_lo[r], plan.read_hi[r]);
		if (!p) {
			return ColumnRead::pending();
		}
		acc = {.min = std::min(acc.min, p->min), .max = std::max(acc.max, p->max)};
	}
	if (acc.min > acc.max) {
		return ColumnRead::past_end(); // no whole frame under it
	}
	// 16-bit peaks at the 32-bit scale the drawing works in.
	return ColumnRead::of({.min = static_cast<int32_t>(acc.min) * 65536, .max = static_cast<int32_t>(acc.max) * 65536});
}

} // namespace

PLACE_SDRAM_TEXT ui::views::waveform::PeakRead waveform_columns(Sample& sample, std::optional<uint64_t> captured,
                                                                ui::views::waveform::Window at) {
	using ui::views::waveform::ColumnRead;
	using ui::views::waveform::kColumns;
	if (at.zoom < 1) {
		return ui::views::waveform::PeakRead::all_past_end();
	}
	DelugePeakIndex* peaks = sample.peaks();
	const auto valid = static_cast<int64_t>(captured.value_or(sample.lengthInSamples));
	// What the index holds: a take's may lag its recorder by a tick.
	const auto held = static_cast<int64_t>(deluge_peaks_frames(peaks));
	ui::views::waveform::PeakRead read; // every column pending until it is answered
	for (int32_t col = 0; col < kColumns; col++) {
		int64_t lo = at.scroll + int64_t{col} * at.zoom;
		int64_t hi = lo + at.zoom;
		if (lo >= valid || hi <= 0) {
			read.set(col, ColumnRead::past_end()); // outside the waveform: past its end, or wholly before
			continue;
		}
		lo = std::max<int64_t>(lo, 0);
		if (hi > valid) {
			if (captured) {
				read.set(col, ColumnRead::past_end()); // a take's column not wholly captured yet
				continue;
			}
			hi = valid;
		}
		if (peaks == nullptr || hi > held) {
			continue; // pending
		}
		// Keep level-0 values around the column's ends, so a later frame reads them instead of the samples.
		deluge_peaks_ensure_level0(peaks, static_cast<uint64_t>(std::max<int64_t>(lo - 4096, 0)),
		                           static_cast<uint64_t>(lo + 4096));
		deluge_peaks_ensure_level0(peaks, static_cast<uint64_t>(std::max<int64_t>(hi - 4096, 0)),
		                           static_cast<uint64_t>(hi + 4096));
		read.set(col, column_of(sample, peaks, static_cast<uint64_t>(lo), static_cast<uint64_t>(hi)));
	}
	return read;
}

PLACE_SDRAM_TEXT bool advance_peak_scan(Sample& sample) {
	DelugePeakIndex* peaks = sample.peaks();
	if (peaks == nullptr || !sample.isLengthKnown()) {
		return false;
	}
	const uint64_t from = deluge_peaks_next_unscanned(peaks, sample.peaksScanFrom);
	if (from == UINT64_MAX) {
		return false;
	}
	sample.peaksScanFrom = from;
	for (SampleRecorder* recorder = AudioEngine::firstRecorder; recorder != nullptr; recorder = recorder->next) {
		if (recorder->sample == &sample) {
			return false; // its recorder fills its index; a take's clusters are not readable yet
		}
	}
	// As the overview scan: never compete with the card or the audio routine for a read.
	if (deluge::sync::sd_busy() || AudioEngine::audioTicksHeld()) {
		return true;
	}
	if (!fold_block_from_frames(sample, peaks, from)) {
		return true; // the fill is requested; the tap folds it, or a later call reads it
	}
	sample.peaksScanFrom = from + 256;
	return true;
}

} // namespace deluge::sample
