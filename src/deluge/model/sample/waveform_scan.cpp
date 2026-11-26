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
#include "definitions_cxx.hpp"
#include "deluge_storage_sample_peaks.h"
#include "io/debug/log.h"
#include "model/sample/overview_cache_entry.h"
#include "model/sample/sample.h"
#include "model/sample/sample_reader_bridge.h"
#include "model/sample/sample_recorder.h"
#include "model/sample/waveform_peak_math.h"
#include "processing/engines/audio_engine.h"
#include "storage/cluster/cluster.h"
#include "sync/sd_access.h"
#include <algorithm>
#include <optional>

namespace deluge::sample {

ui::views::waveform::PeakRead read_columns(Sample& sample, SampleRecorder* recorder, ui::views::waveform::Window at,
                                           app::ports::waveforms::ColumnMask wanted) {
	if (at.zoom < 1) {
		return ui::views::waveform::PeakRead::all_past_end();
	}

	uint64_t num_valid_samples = 0;
	int32_t endClusters = 0;
	if (recorder) {
		num_valid_samples = recorder->numSamplesCaptured;
		// The overview cache's own physical size, NOT stream().num_clusters(): that count is now
		// derived from the live recorder's captured-cluster count, which can exceed the (un-grown
		// during recording) overview cache -- see OverviewCacheEntry's docs. Bounding on the
		// cache's own size keeps every entry(clusterIndexToDo) index below in-bounds by construction.
		endClusters = static_cast<int32_t>(sample.overviewCacheSize());
	}
	else {
		num_valid_samples = sample.lengthInSamples;
		endClusters = sample.getFirstClusterIndexWithNoAudioData();
	}

	const uint64_t numValidBytes = num_valid_samples * sample.byteDepth * sample.numChannels;

	ui::views::waveform::PeakRead read; // every column pending until it is answered

	// One reader for the whole scan (SCAN hint: this is a one-shot whole-sample pass, so it must not evict
	// the voice's own warm cluster set). Re-seeked per column below; cheap to open even if no column is
	// wanted (open takes no lease and does no I/O).
	deluge::sample::SampleFrameReader reader{deluge::sample::source_id_for(sample), 0, 1, DELUGE_READ_SCAN};

	const auto valid = static_cast<int64_t>(num_valid_samples);
	const int64_t frame_bytes = int64_t{sample.numChannels} * sample.byteDepth;
	for (int32_t col = 0; col < ui::views::waveform::kColumns; col++) {
		if (!wanted.test(static_cast<size_t>(col))) {
			continue;
		}
		int64_t col_start = at.scroll + int64_t{col} * at.zoom;
		int64_t col_end = col_start + at.zoom;
		if (col_start >= valid || col_end <= 0) {
			read.set(
			    col,
			    ui::views::waveform::ColumnRead::past_end()); // outside the waveform: past its end, or wholly before
			continue;
		}
		col_start = std::max<int64_t>(col_start, 0); // a column straddling the start reads from frame 0
		if (col_end >= valid) {
			if (recorder != nullptr) {
				// A take still being written: the column is not wholly captured yet.
				read.set(col, ui::views::waveform::ColumnRead::past_end());
				continue;
			}
			col_end = valid;
		}
		const int64_t col_start_byte = col_start * frame_bytes + sample.audioDataStartPosBytes;
		const int64_t col_end_byte = col_end * frame_bytes + sample.audioDataStartPosBytes;

		int32_t colStartCluster = static_cast<int32_t>(col_start_byte >> Cluster::size_magnitude);
		int32_t colEndCluster = static_cast<int32_t>(col_end_byte >> Cluster::size_magnitude);

		int32_t clusterIndexToDo = 0;
		int32_t startByteWithinCluster = 0;
		int32_t endByteWithinCluster = 0;

		int32_t numClustersSpan = colEndCluster - colStartCluster;

		bool investigatingAWholeCluster = false;

		// If both same cluster...
		if (numClustersSpan == 0) {
			clusterIndexToDo = colStartCluster;
			startByteWithinCluster = static_cast<int32_t>(col_start_byte & (Cluster::size - 1));
			endByteWithinCluster = static_cast<int32_t>(col_end_byte & (Cluster::size - 1));
		}

		// Special case to make sure we get initial transient (we know there's more than 1 cluster)
		else if (col_start == 0 && col_start_byte < (Cluster::size >> 1)) {
			clusterIndexToDo = colStartCluster;
			startByteWithinCluster = static_cast<int32_t>(col_start_byte & (Cluster::size - 1));
			endByteWithinCluster = Cluster::size;
			investigatingAWholeCluster = true;
		}

		// If 3 or more clusters, take 2nd one. TODO: have it take any one which has previously been fully investigated?
		else if (numClustersSpan >= 2) {
			clusterIndexToDo = colStartCluster + 1;

			startByteWithinCluster =
			    firstFrameStartWithinCluster(clusterStartByte(clusterIndexToDo, Cluster::size_magnitude),
			                                 sample.audioDataStartPosBytes, sample.numChannels * sample.byteDepth);

			endByteWithinCluster = Cluster::size;
			investigatingAWholeCluster = true;
		}

		// If 2 cluster..
		else if (numClustersSpan == 1) {

			int32_t startByteWithinFirstCluster = static_cast<int32_t>(col_start_byte & (Cluster::size - 1));
			int32_t bytesInFirstCluster = Cluster::size - startByteWithinFirstCluster;

			int32_t bytesInSecondCluster = static_cast<int32_t>(col_end_byte & (Cluster::size - 1));

			// If more in first cluster...
			if (bytesInFirstCluster >= bytesInSecondCluster) {
				clusterIndexToDo = colStartCluster;
				startByteWithinCluster = startByteWithinFirstCluster;
				endByteWithinCluster = Cluster::size;
			}

			// Or if more in second cluster...
			else {
				clusterIndexToDo = colEndCluster;

				startByteWithinCluster =
				    firstFrameStartWithinCluster(clusterStartByte(clusterIndexToDo, Cluster::size_magnitude),
				                                 sample.audioDataStartPosBytes, sample.numChannels * sample.byteDepth);

				endByteWithinCluster = bytesInSecondCluster;
			}
		}

		if (clusterIndexToDo >= endClusters) { // Could this actually happen?
			read.set(col, ui::views::waveform::ColumnRead::past_end());
			continue;
		}

		if (clusterIndexToDo == endClusters - 1) {

			int32_t limit = lastAudioClusterEndByte(numValidBytes, sample.audioDataStartPosBytes, Cluster::size);

			if (endByteWithinCluster > limit) {
				endByteWithinCluster = limit;
			}
		}

		OverviewCacheEntry* sampleCluster = &sample.overviewCacheEntry(clusterIndexToDo);

		// If we're wanting to investigate the whole length of one Cluster, and that's already actually been done
		// previously, we can just reuse those findings!
		if (investigatingAWholeCluster && sampleCluster->investigated) {
			read.set(col, ui::views::waveform::ColumnRead::of(
			                  {.min = (int32_t)sampleCluster->min << 24, .max = (int32_t)sampleCluster->max << 24}));
			continue;
		}

		// Otherwise, do our normal investigation.
		// startByteWithinCluster is always frame-grid-aligned (either the column's own masked byte offset, or
		// firstFrameStartWithinCluster's result), so it maps onto exactly one frame index -- the frame this
		// column's (or whole-cluster investigation's) read begins at.
		const int64_t startByteAbs =
		    clusterStartByte(clusterIndexToDo, Cluster::size_magnitude) + startByteWithinCluster;
		const auto startFrame = static_cast<uint64_t>(
		    base::trunc_div(startByteAbs - static_cast<int64_t>(sample.audioDataStartPosBytes), frame_bytes));

		reader.seek(startFrame);
		DelugeFrameWindow window = reader.window();
		if (!reader.ok()) {
			D_PRINTLN("cant read");
			continue; // pending: a later read asks again
		}

		// How many whole frames of the intended [startByteWithinCluster, endByteWithinCluster) span the
		// reader actually made resident. Capped to window.frame_count rather than trusting it outright:
		// unlike a straight streaming read, this scan deliberately never widens a column's or a whole
		// cluster's span to include a frame straddling into the next cluster (it always rounds such a span
		// down rather than reading across the boundary).
		const int64_t wantedBytes =
		    endByteWithinCluster > startByteWithinCluster ? int64_t{endByteWithinCluster} - startByteWithinCluster : 0;
		const auto numFramesToRead =
		    static_cast<uint32_t>(std::min<int64_t>(base::trunc_div(wantedBytes, frame_bytes), window.frame_count));
		const auto numBytesToRead = static_cast<uint32_t>(numFramesToRead * frame_bytes);

		// If there are no whole frames to read here (e.g. audio ends very early in the final cluster, or
		// this column's span collapsed to less than one frame near a cluster boundary), this column has no
		// valid data: it has nothing to draw (#4460).
		if (numBytesToRead == 0) {
			read.set(col, ui::views::waveform::ColumnRead::past_end());
			continue;
		}

		// NOTE: from here on, we read *both* channels (if there are two), counting each one as a "sample"
		WaveformPeak peak = scanClusterPeak(reinterpret_cast<const char*>(window.frames), 0,
		                                    static_cast<int32_t>(numBytesToRead), sample.byteDepth, sample.numChannels);
		int32_t minThisCol = peak.min;
		int32_t maxThisCol = peak.max;

		// If we just looked at the length of one entire cluster...
		if (investigatingAWholeCluster) {

			// See if we want to include any previously captured maximums and minimums, which might have looked at
			// slightly different values
			int32_t prevMin = (int32_t)sampleCluster->min << 24;
			int32_t prevMax = (int32_t)sampleCluster->max << 24;

			if (prevMin < minThisCol) {
				minThisCol = prevMin;
			}
			if (prevMax > maxThisCol) {
				maxThisCol = prevMax;
			}

			// And mark the overview cache entry as fully investigated (rounding toward 0)
			sampleCluster->min = toCoarsePeak(minThisCol);
			sampleCluster->max = toCoarsePeak(maxThisCol);
			sampleCluster->investigated = true;
		}

		// Or, if we only looked at a smaller part of a cluster...
		else {

			// Then just contribute to the running record of max and min found
			int8_t smallMin = toCoarsePeak(minThisCol);
			int8_t smallMax = toCoarsePeak(maxThisCol);

			if (smallMin < sampleCluster->min) {
				sampleCluster->min = smallMin;
			}
			if (smallMax > sampleCluster->max) {
				sampleCluster->max = smallMax;
			}
		}

		read.set(col, ui::views::waveform::ColumnRead::of({.min = minThisCol, .max = maxThisCol}));
	}

	if (recorder != nullptr) {
		sample.maxValueFound = recorder->recordMax;
		sample.minValueFound = recorder->recordMin;
	}
	else {
		// The columns just read. A column a cache keeps was folded when it was read, and the range only widens.
		for (int32_t col = 0; col < ui::views::waveform::kColumns; col++) {
			if (!wanted.test(static_cast<size_t>(col))) {
				continue;
			}
			if (const std::optional<ui::views::waveform::Peak> p = read.column(col).peak()) {
				sample.maxValueFound = std::max(sample.maxValueFound, p->max);
				sample.minValueFound = std::min(sample.minValueFound, p->min);
			}
		}
	}
	return read;
}

ui::views::waveform::PeakRead find_peaks_per_col(Sample& sample, ui::views::waveform::Window at,
                                                 app::ports::waveforms::PeakCache& cache, SampleRecorder* recorder,
                                                 int32_t first, int32_t last) {
	static_assert(ui::views::waveform::kColumns == kDisplayWidth);
	const auto valid =
	    static_cast<int64_t>(recorder != nullptr ? recorder->numSamplesCaptured : sample.lengthInSamples);
	return cache.update(
	    at, valid, recorder != nullptr,
	    [&](app::ports::waveforms::ColumnMask wanted) { return read_columns(sample, recorder, at, wanted); }, first,
	    last);
}

// Background "waveform overview" pre-scan (issue #4460). Investigates one whole cluster the same way
// findPeaksPerCol's whole-cluster branch does, but self-contained so it can run off the render path.
// Caches the result in the Sample's overview cache entry (int8 min/max + investigated) so later zoomed-out
// renders get a cache hit and never load this cluster synchronously while the user is scrolling.
bool investigate_whole_cluster(Sample* sample, int32_t clusterIndex) {

	const int32_t endClusters = sample->getFirstClusterIndexWithNoAudioData();
	if (clusterIndex < 0 || clusterIndex >= endClusters) {
		return true; // Nothing to do
	}

	OverviewCacheEntry* sampleCluster = &sample->overviewCacheEntry(clusterIndex);
	if (sampleCluster->investigated) {
		return true; // Already cached (perhaps by a previous render)
	}

	// Don't investigate a cluster of a Sample a SampleRecorder still has open. A recording target's
	// SampleRecorder writes into private capture buffers and never opens a real efatfs read handle on
	// its Sample (SampleStream::efatfs_handle() stays 0 for its whole life as a recording target -- see
	// SampleStream::open_read_stream()/make_read_source()), so ANY get_cluster() load attempt here --
	// not just the actively-being-written cluster -- would simply fail. (The background scan does
	// reach a recording-in-progress Sample: SampleRecorder::setup() registers it in
	// audioFileManager.audioFiles as soon as the file's created, well before capture finishes.) We
	// leave the scan cursor parked here; once the recorder detaches (recording finishes and the
	// SampleRecorder is destructed), the Sample becomes independently loadable and the scan resumes.
	for (SampleRecorder* recorder = AudioEngine::firstRecorder; recorder != nullptr; recorder = recorder->next) {
		if (recorder->sample == sample) {
			return false;
		}
	}

	const int32_t frameSize = sample->numChannels * sample->byteDepth;
	const uint64_t numValidBytes = sample->lengthInSamples * sample->byteDepth * sample->numChannels;

	// Work out the first frame-aligned byte of audio data within this cluster. 64-bit so multi-GB samples
	// (cluster index << size_magnitude) don't overflow (#4460).
	const int64_t clusterStartByteAbs = clusterStartByte(clusterIndex, Cluster::size_magnitude);
	const int32_t startByteWithinCluster =
	    firstFrameStartWithinCluster(clusterStartByteAbs, sample->audioDataStartPosBytes, frameSize);

	int32_t endByteWithinCluster = Cluster::size;
	if (clusterIndex == endClusters - 1) {
		endByteWithinCluster =
		    std::min(endByteWithinCluster,
		             lastAudioClusterEndByte(numValidBytes, sample->audioDataStartPosBytes, Cluster::size));
	}

	// Trim the end back to a whole frame so we never read past the cluster boundary.
	endByteWithinCluster -= (endByteWithinCluster - startByteWithinCluster) % frameSize;

	if (endByteWithinCluster <= startByteWithinCluster) {
		// No whole frames to scan here (e.g. a sub-frame tail). Leave it un-investigated rather than
		// stamping an inverted sentinel; advanceOverviewScan advances the cursor past it regardless.
		return true;
	}

	uint64_t startFrame = static_cast<uint64_t>(
	    (clusterStartByteAbs + startByteWithinCluster - static_cast<int64_t>(sample->audioDataStartPosBytes))
	    / frameSize);

	// SCAN hint: this background pre-scan must not evict the voice's own warm cluster set.
	deluge::sample::SampleFrameReader reader{deluge::sample::source_id_for(*sample), startFrame, 1, DELUGE_READ_SCAN};
	DelugeFrameWindow window = reader.window();
	if (!reader.ok()) {
		return false; // Card busy / couldn't read - caller will retry later
	}

	// endByteWithinCluster/startByteWithinCluster are already frame-trimmed above, so this is exactly the
	// span we want -- capped to what the reader actually made resident.
	uint32_t wantedFrames = static_cast<uint32_t>(endByteWithinCluster - startByteWithinCluster) / frameSize;
	uint32_t numFramesToRead = std::min(wantedFrames, window.frame_count);
	uint32_t numBytesToRead = numFramesToRead * frameSize;
	if (numBytesToRead == 0) {
		return true; // Nothing came back resident this call; leave un-investigated, same as above.
	}

	WaveformPeak peak = scanClusterPeak(reinterpret_cast<const char*>(window.frames), 0,
	                                    static_cast<int32_t>(numBytesToRead), sample->byteDepth, sample->numChannels);

	// Fold in anything previously captured for this cluster (same as findPeaksPerCol's whole-cluster path).
	peak.min = std::min(peak.min, static_cast<int32_t>(sampleCluster->min) << 24);
	peak.max = std::max(peak.max, static_cast<int32_t>(sampleCluster->max) << 24);

	// Store the coarse (int8) overview, rounding towards 0.
	sampleCluster->min = toCoarsePeak(peak.min);
	sampleCluster->max = toCoarsePeak(peak.max);
	sampleCluster->investigated = true;

	// The sample's found range is left alone: the full-screen waveform scales its bars to it, and main's holds only
	// what a render has scanned. Whole-cluster peaks folded in here would draw every later render's bars smaller.

	return true;
}

// Advances the sample's background overview pre-scan by up to maxClusters previously-uninvestigated
// clusters. Cheaply skips clusters already investigated (e.g. by a render). Returns true while work
// remains, so the idle driver knows to come back.
bool advance_overview_scan(Sample* sample, int32_t maxClusters) {

	const int32_t endClusters = sample->getFirstClusterIndexWithNoAudioData();

	// Never scan header-only clusters: start at the first cluster that actually holds audio.
	sample->overviewScanNextCluster =
	    std::max(sample->overviewScanNextCluster, sample->getFirstClusterIndexWithAudioData());

	// If the valid range shrank (e.g. after invalidation), clamp the cursor back into range.
	sample->overviewScanNextCluster = std::min(sample->overviewScanNextCluster, endClusters);

	for (int32_t budget = maxClusters; budget > 0 && sample->overviewScanNextCluster < endClusters;) {
		const int32_t clusterIndex = sample->overviewScanNextCluster;

		if (sample->overviewCacheEntry(clusterIndex).investigated) {
			sample->overviewScanNextCluster++; // Free; don't spend budget on cache hits
			continue;
		}

		// Authoritative "is card I/O safe right now?" guard: each investigateWholeCluster can issue a
		// synchronous load, so bail before every one rather than compete with the card or audio routine
		// (#4460), and it scales safely if maxClusters ever grows past 1. This guard is load-bearing, not
		// defensive: without it, this scan's synchronous read runs off the storage owner and can block on
		// the efatfs FS mutex while it's held by the worker fiber suspended mid-operation. That fiber can
		// only be resumed by a MAIN-executor task — but the spin itself occupies MAIN's executor.poll(),
		// so nothing can ever resume the fiber. That's an unbreakable deadlock, not a stall -- in the
		// Embassy HOST harness specifically: the executor-parking spin this paragraph describes is
		// `embassy_futures::block_on`, which lives only in efatfs_host_shim.rs (the host_app feature).
		// The device build has no such spin: every entry point in efatfs_fs.rs (e.g.
		// deluge_efatfs_read_at, the function this scan's fill path actually calls) gates on on_fiber()
		// and REJECTS an off-fiber caller immediately instead of blocking, so on real hardware this
		// guard's job is to stop the scan from churning always-failing off-fiber attempts, not to prevent
		// an executor deadlock that was never going to happen there. `sd_busy()` (deluge::sync,
		// sync/sd_access.h) reports whether the FS mutex is actually held right now, so a future reader
		// must not mistake this check for redundant and delete it. This is only meaningful on BSPs that
		// back the seam with a real mutex-held check (Embassy); cooperative/C-host builds answer false
		// unconditionally because storage there runs inline on the caller, so there's no FS mutex to
		// report on and no deadlock of this shape to guard against.
		if (deluge::sync::sd_busy() || AudioEngine::audioTicksHeld()) {
			return true;
		}

		if (!investigate_whole_cluster(sample, clusterIndex)) {
			return true; // Card busy or still recording - retry this cluster next time without advancing
		}

		sample->overviewScanNextCluster++;
		budget--;
	}

	return sample->overviewScanNextCluster < endClusters;
}

namespace {

/// Read the 256-frame block at frame @p from and fold it into @p peaks. False if a cluster under it is not resident:
/// the read has requested its fill, and the tap folds the whole cluster when it lands.
bool fold_block_from_frames(Sample& sample, DelugePeakIndex* peaks, uint64_t from) {
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
std::optional<DelugePeak> read_peak(Sample& sample, DelugePeakIndex* peaks, uint64_t lo, uint64_t hi) {
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
deluge::ui::views::waveform::ColumnRead column_of(Sample& sample, DelugePeakIndex* peaks, uint64_t lo, uint64_t hi) {
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

ui::views::waveform::PeakRead waveform_columns(Sample& sample, std::optional<uint64_t> captured,
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

bool advance_peak_scan(Sample& sample) {
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
