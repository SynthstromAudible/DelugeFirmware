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

// Moved out of gui/waveform/waveform_renderer.cpp unchanged -- see waveform_scan.h for why.

#include "model/sample/waveform_scan.h"
#include "definitions_cxx.hpp"
#include "io/debug/log.h"
#include "model/sample/overview_cache_entry.h"
#include "model/sample/sample.h"
#include "model/sample/sample_reader_bridge.h"
#include "model/sample/sample_recorder.h"
#include "model/sample/waveform_peak_math.h"
#include "model/sample/waveform_render_data.h"
#include "processing/engines/audio_engine.h"
#include "storage/cluster/cluster.h"
#include "sync/sd_access.h"
#include <algorithm>
#include <iterator>

namespace deluge::sample {

namespace {

// Slides the cached per-column peak data when the waveform scrolls horizontally by a whole number of
// columns (fix A, issue #4460). After the shift, new column `i` holds what old column `i + shift` held,
// so only the columns scrolled freshly into view need re-investigating (marked un-investigated here).
void shiftRenderCache(WaveformRenderData& data, int32_t shift) {
	if (shift == 0) {
		return;
	}
	if (std::abs(shift) >= kDisplayWidth) {
		std::ranges::fill(data.colStatus, COL_STATUS_NOT_INVESTIGATED);
		return;
	}

	auto slide = [shift](auto& array) {
		if (shift > 0) {
			std::shift_left(std::begin(array), std::end(array), shift);
		}
		else {
			std::shift_right(std::begin(array), std::end(array), -shift);
		}
	};
	slide(data.colStatus);
	slide(data.minPerCol);
	slide(data.maxPerCol);

	// Mark the freshly-exposed columns (left behind by the shift) as needing investigation.
	if (shift > 0) {
		std::fill_n(std::end(data.colStatus) - shift, shift, uint8_t{COL_STATUS_NOT_INVESTIGATED});
	}
	else {
		std::fill_n(std::begin(data.colStatus), -shift, uint8_t{COL_STATUS_NOT_INVESTIGATED});
	}
}

} // namespace

// Value out of 255
int32_t col_brightness_for_single_row(int32_t xDisplay, int32_t maxPeakFromZero, const WaveformRenderData* data) {

	int32_t peak1 = std::abs(data->minPerCol[xDisplay]);
	int32_t peak2 = std::abs(data->maxPerCol[xDisplay]);

	int32_t peakHere = std::max(peak1, peak2);

	if (false && peakHere >= maxPeakFromZero) {
		D_PRINTLN("peak:  %d  but max:  %d", peakHere, maxPeakFromZero);
	}

	uint32_t peak16 = ((int64_t)peakHere << 16) / maxPeakFromZero;

	return std::min<int32_t>(peak16 >> 8, 256); // Max 256 - for now. Looks great and bright.
	                                            // Must manually limit this, cos if we've ended up with values higher
	                                            // than our maxPeakFromZero, there'd be trouble otherwise
}

// Returns false if had trouble loading some (will often not be all) Clusters, e.g. cos we're in the card routine
bool find_peaks_per_col(Sample* sample, int64_t xScrollSamples, uint64_t xZoomSamples, WaveformRenderData* data,
                        SampleRecorder* recorder, int32_t xStart, int32_t xEnd) {

	uint64_t numValidSamples;
	int32_t endClusters;
	if (recorder) {
		numValidSamples = recorder->numSamplesCaptured;
		// The overview cache's own physical size, NOT stream().num_clusters(): that count is now
		// derived from the live recorder's captured-cluster count, which can exceed the (un-grown
		// during recording) overview cache -- see OverviewCacheEntry's docs. Bounding on the
		// cache's own size keeps every entry(clusterIndexToDo) index below in-bounds by construction.
		endClusters = static_cast<int32_t>(sample->overviewCacheSize());
	}
	else {
		numValidSamples = sample->lengthInSamples;
		endClusters = sample->getFirstClusterIndexWithNoAudioData();
	}

	uint64_t numValidBytes = numValidSamples * sample->byteDepth * sample->numChannels;

	bool lengthChanged = static_cast<int64_t>(numValidSamples) != data->validLengthSamples;
	if (recorder != nullptr) {
		// During recording the waveform grows every frame. A monotonic append leaves already-investigated
		// columns valid (the growing edge is re-checked below because only COL_STATUS_INVESTIGATED columns are
		// skipped), so don't nuke the whole cache each frame just because the length ticked up (#4460).
		lengthChanged = false;
	}
	if (xZoomSamples != data->xZoom || lengthChanged) {
		// Zoom changed, or (off the record path) the waveform length changed (e.g. sample replaced/shrunk):
		// nothing is reusable.
		std::ranges::fill(data->colStatus, COL_STATUS_NOT_INVESTIGATED);
	}
	else if (xScrollSamples != data->xScroll) {
		// Scroll changed but zoom and length didn't. If we moved by a whole number of columns that fits the
		// display width, the overlapping columns' peaks are still valid - slide them across so only the
		// newly-exposed columns get re-investigated (fix A, issue #4460). Otherwise fall back to a full
		// invalidate (this also covers a scroll delta so large the column count wouldn't fit in int32).
		int64_t deltaSamples = xScrollSamples - data->xScroll;
		int64_t shiftCols = (deltaSamples % static_cast<int64_t>(xZoomSamples) == 0)
		                        ? deltaSamples / static_cast<int64_t>(xZoomSamples)
		                        : 0;
		if (shiftCols != 0 && shiftCols >= -kDisplayWidth && shiftCols <= kDisplayWidth) {
			shiftRenderCache(*data, static_cast<int32_t>(shiftCols));
		}
		else {
			std::ranges::fill(data->colStatus, COL_STATUS_NOT_INVESTIGATED);
		}
	}

	data->xScroll = xScrollSamples;
	data->xZoom = xZoomSamples;
	data->validLengthSamples = static_cast<int64_t>(numValidSamples);

	bool hadAnyTroubleLoading = false;

	// One reader for the whole scan (SCAN hint: this is a one-shot whole-sample pass, so it must not evict
	// the voice's own warm cluster set). Re-seeked per column below; cheap to open even if every column
	// this pass turns out to be a cache hit (open takes no lease and does no I/O).
	deluge::sample::SampleFrameReader reader{deluge::sample::source_id_for(*sample), 0, 1, DELUGE_READ_SCAN};

	for (int32_t col = xStart; col < xEnd; col++) {

		if (data->colStatus[col] == COL_STATUS_INVESTIGATED) {
			continue;
		}

		data->colStatus[col] = COL_STATUS_INVESTIGATED; // Default, which we may override below

		int32_t colStartSample = xScrollSamples + col * xZoomSamples;
		if (colStartSample >= numValidSamples) {
			data->colStatus[col] = COL_STATUS_INVESTIGATED_BUT_BEYOND_WAVEFORM;
			continue;
		}
		else if (colStartSample < 0) {
			colStartSample = 0;
		}

		int32_t colEndSample = xScrollSamples + (col + 1) * xZoomSamples;

		// If this column extends further right than the end of the waveform...
		if (colEndSample >= numValidSamples) {

			// If we're still recording, we'll just want to come back and render this one when the waveform has grown to
			// cover this whole column
			if (recorder) {
				data->colStatus[col] = 0;
				continue;
			}
			colEndSample = numValidSamples;
		}
		else if (colEndSample < 0) {
			data->colStatus[col] = COL_STATUS_INVESTIGATED_BUT_BEYOND_WAVEFORM;
			continue;
		}

		// 64-bit so samples past 2 GB (colStartSample * frameSize) don't overflow (#4460).
		int64_t colStartByte = static_cast<int64_t>(colStartSample) * sample->numChannels * sample->byteDepth
		                       + sample->audioDataStartPosBytes;
		int64_t colEndByte = static_cast<int64_t>(colEndSample) * sample->numChannels * sample->byteDepth
		                     + sample->audioDataStartPosBytes;

		int32_t colStartCluster = static_cast<int32_t>(colStartByte >> Cluster::size_magnitude);
		int32_t colEndCluster = static_cast<int32_t>(colEndByte >> Cluster::size_magnitude);

		int32_t clusterIndexToDo;
		int32_t startByteWithinCluster;
		int32_t endByteWithinCluster;

		int32_t numClustersSpan = colEndCluster - colStartCluster;

		bool investigatingAWholeCluster = false;

		// If both same cluster...
		if (numClustersSpan == 0) {
			clusterIndexToDo = colStartCluster;
			startByteWithinCluster = static_cast<int32_t>(colStartByte & (Cluster::size - 1));
			endByteWithinCluster = static_cast<int32_t>(colEndByte & (Cluster::size - 1));
		}

		// Special case to make sure we get initial transient (we know there's more than 1 cluster)
		else if (colStartSample == 0 && colStartByte < (Cluster::size >> 1)) {
			clusterIndexToDo = colStartCluster;
			startByteWithinCluster = static_cast<int32_t>(colStartByte & (Cluster::size - 1));
			endByteWithinCluster = Cluster::size;
			investigatingAWholeCluster = true;
		}

		// If 3 or more clusters, take 2nd one. TODO: have it take any one which has previously been fully investigated?
		else if (numClustersSpan >= 2) {
			clusterIndexToDo = colStartCluster + 1;

			startByteWithinCluster =
			    firstFrameStartWithinCluster(clusterStartByte(clusterIndexToDo, Cluster::size_magnitude),
			                                 sample->audioDataStartPosBytes, sample->numChannels * sample->byteDepth);

			endByteWithinCluster = Cluster::size;
			investigatingAWholeCluster = true;
		}

		// If 2 cluster..
		else if (numClustersSpan == 1) {

			int32_t startByteWithinFirstCluster = static_cast<int32_t>(colStartByte & (Cluster::size - 1));
			int32_t bytesInFirstCluster = Cluster::size - startByteWithinFirstCluster;

			int32_t bytesInSecondCluster = static_cast<int32_t>(colEndByte & (Cluster::size - 1));

			// If more in first cluster...
			if (bytesInFirstCluster >= bytesInSecondCluster) {
				clusterIndexToDo = colStartCluster;
				startByteWithinCluster = startByteWithinFirstCluster;
				endByteWithinCluster = Cluster::size;
			}

			// Or if more in second cluster...
			else {
				clusterIndexToDo = colEndCluster;

				startByteWithinCluster = firstFrameStartWithinCluster(
				    clusterStartByte(clusterIndexToDo, Cluster::size_magnitude), sample->audioDataStartPosBytes,
				    sample->numChannels * sample->byteDepth);

				endByteWithinCluster = bytesInSecondCluster;
			}
		}

		if (clusterIndexToDo >= endClusters) { // Could this actually happen?
			data->colStatus[col] = COL_STATUS_INVESTIGATED_BUT_BEYOND_WAVEFORM;
			continue;
		}

		else if (clusterIndexToDo == endClusters - 1) {

			int32_t limit = lastAudioClusterEndByte(numValidBytes, sample->audioDataStartPosBytes, Cluster::size);

			if (endByteWithinCluster > limit) {
				endByteWithinCluster = limit;
			}
		}

		OverviewCacheEntry* sampleCluster = &sample->overviewCacheEntry(clusterIndexToDo);

		// If we're wanting to investigate the whole length of one Cluster, and that's already actually been done
		// previously, we can just reuse those findings!
		if (investigatingAWholeCluster && sampleCluster->investigated) {
			data->minPerCol[col] = (int32_t)sampleCluster->min << 24;
			data->maxPerCol[col] = (int32_t)sampleCluster->max << 24;
		}

		// Otherwise, do our normal investigation
		else {
			int32_t frameSize = sample->numChannels * sample->byteDepth;

			// startByteWithinCluster is always frame-grid-aligned (either colStartByte/colEndByte's own
			// masked byte offset, or firstFrameStartWithinCluster's result), so it maps onto exactly one
			// frame index -- the frame this column's (or whole-cluster investigation's) read begins at.
			int64_t startByteAbs = clusterStartByte(clusterIndexToDo, Cluster::size_magnitude) + startByteWithinCluster;
			uint64_t startFrame =
			    static_cast<uint64_t>(startByteAbs - static_cast<int64_t>(sample->audioDataStartPosBytes)) / frameSize;

			reader.seek(startFrame);
			DelugeFrameWindow window = reader.window();
			if (!reader.ok()) {
				D_PRINTLN("cant read");
				data->colStatus[col] = 0;
				hadAnyTroubleLoading = true;
				continue;
			}

			// How many whole frames of the intended [startByteWithinCluster, endByteWithinCluster) span the
			// reader actually made resident. Capped to window.frame_count rather than trusting it outright:
			// unlike a straight streaming read, this scan deliberately never widens a column's or a whole
			// cluster's span to include a frame straddling into the next cluster (matching the historical
			// behaviour here, which always rounded such a span down rather than reading across the boundary).
			uint32_t wantedBytes = endByteWithinCluster > startByteWithinCluster
			                           ? static_cast<uint32_t>(endByteWithinCluster - startByteWithinCluster)
			                           : 0;
			uint32_t numFramesToRead = std::min(wantedBytes / static_cast<uint32_t>(frameSize), window.frame_count);
			uint32_t numBytesToRead = numFramesToRead * frameSize;

			// If there are no whole frames to read here (e.g. audio ends very early in the final cluster, or
			// this column's span collapsed to less than one frame near a cluster boundary), this column has no
			// valid data. Mark it as having nothing to draw rather than scanning an empty window and caching an
			// inverted sentinel (min > max), which would render as a spurious bright column (#4460).
			if (numBytesToRead == 0) {
				data->colStatus[col] = COL_STATUS_INVESTIGATED_BUT_BEYOND_WAVEFORM;
				continue;
			}

			// NOTE: from here on, we read *both* channels (if there are two), counting each one as a "sample"
			WaveformPeak peak =
			    scanClusterPeak(reinterpret_cast<const char*>(window.frames), 0, static_cast<int32_t>(numBytesToRead),
			                    sample->byteDepth, sample->numChannels);
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

			data->maxPerCol[col] = maxThisCol;
			data->minPerCol[col] = minThisCol;

			AudioEngine::routineWithClusterLoading();
		}
	}

	if (recorder != nullptr) {
		sample->maxValueFound = recorder->recordMax;
		sample->minValueFound = recorder->recordMin;
	}

	// Keep a running best for the max and min found for the whole Sample
	else {
		for (int32_t col = xStart; col < xEnd; col++) {
			if (data->colStatus[col] == COL_STATUS_INVESTIGATED) {
				if (data->maxPerCol[col] > sample->maxValueFound) {
					sample->maxValueFound = data->maxPerCol[col];
				}
				if (data->minPerCol[col] < sample->minValueFound) {
					sample->minValueFound = data->minPerCol[col];
				}
			}
		}
	}

	return !hadAnyTroubleLoading;
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

	// Keep the sample's running peak up to date too, so brightness normalisation is pre-warmed.
	sample->maxValueFound = std::max(sample->maxValueFound, peak.max);
	sample->minValueFound = std::min(sample->minValueFound, peak.min);

	AudioEngine::routineWithClusterLoading();

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
		if (deluge::sync::sd_busy() || AudioEngine::audioRoutineLocked) {
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

void col_bar_positions(int32_t xDisplay, const WaveformRenderData* data, int32_t* min24, int32_t* max24,
                       int32_t valueCentrePoint, int32_t valueSpan) {
	*min24 = ((int64_t)(data->minPerCol[xDisplay] - valueCentrePoint) << 24) / valueSpan;
	*max24 = ((int64_t)(data->maxPerCol[xDisplay] - valueCentrePoint) << 24) / valueSpan;

	// Ensure we're going to draw at least 1 pixel's width
	if (*max24 - *min24 < kMaxSampleValue) {
		int32_t midPoint = (*max24 >> 1) + (*min24 >> 1);
		*max24 = midPoint + 8388608;
		*min24 = midPoint - 8388608;
	}
}

} // namespace deluge::sample
