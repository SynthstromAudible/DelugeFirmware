/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#pragma once

#include "dsp/fixed/timestretch/hop_planner.h"
#include <optional>

#include "definitions_cxx.hpp"
#include "dsp/core/fixed_point.h"
#include "model/sample/overview_cache_entry.h"
#include "model/sample/sample_length_sentinel.h"
#include "model/sample/sample_perc_cache_zone.h"
#include "storage/audio/audio_file.h"
#include "storage/audio/stream/convert.h"
#include "storage/audio/stream/sample_stream.h"
#include "storage/cluster/cluster.h" // Cluster::size_magnitude, for geometricClusterCount()
#include "synth/sources/sample/perc_fill.h"
#include "util/containers.h"
#include "util/functions.h"
#include "util/segmented_vector.h" // deluge::SegmentedVector, for overviewCache_
#include <array>
#include <cstdint>

/// A sample's waveform peak index (`deluge_storage_sample_peaks.h`).
struct DelugePeakIndex;

#define SAMPLE_DO_LOCKS 1 // Re-entrancy would corrupt percCacheZones, so this is on in all builds

// RawDataFormat now lives in storage/audio/audio_file_format.h (a file-format facet, not a Sample one);
// it reaches here via sample.h's include of audio_file.h.

const float MIDI_NOTE_UNSET = (-999);
const float MIDI_NOTE_ERROR = (-1000);
class LoadedSamplePosReason;
class SampleCache;

/// One entry in Sample::caches, keyed by the playback parameters the cache was rendered with
struct SampleCacheElement {
	int32_t phaseIncrement;
	int32_t timeStretchRatio;
	int32_t skipSamplesAtStart;
	bool reversed;
	SampleCache* cache;

	[[nodiscard]] std::array<uint32_t, 4> key() const {
		return {static_cast<uint32_t>(phaseIncrement), static_cast<uint32_t>(timeStretchRatio),
		        static_cast<uint32_t>(skipSamplesAtStart), static_cast<uint32_t>(reversed)};
	}
};
class MultisampleRange;
class SampleHolder;

class Sample final : public AudioFile {
public:
	Sample();
	~Sample() override;

	[[nodiscard]] size_t allocatedSize() const override { return sizeof(Sample); }

	void workOutBitMask();
	Error initialize(int32_t numClusters);
	void markAsUnloadable();
	/// @brief Settle midiNote without scanning, if it can be: already known, a single cycle, or the note the file
	///        names. @return Whether it is settled; false means a pitch scan must find it.
	bool midiNoteWithoutScan(bool doingSingleCycle);
	/// @brief Set midiNote from a detected frequency in Hz; 0 (a failed detection) sets MIDI_NOTE_ERROR.
	void setMidiNoteFromFrequency(float hz);
	/// @name The pitch scan's analysis: deluge::sample::PitchScan fills the buffer and calls these.
	/// @{
	/// @brief What one analysis of a filled pitch window decided.
	struct PitchVerdict {
		enum class Kind : uint8_t { Failed, Found, LongerLength } kind;
		float hz; ///< The fundamental in Hz when Found.
	};
	/// @brief Bytes of the scan's buffer: the FFT input, its output, and the float index table, back to back.
	static size_t pitchBufferBytes();
	/// @brief FFT and fundamental search over a filled window in @p buffer (pitchBufferBytes() long).
	PitchVerdict analysePitchWindow(int32_t* buffer, int32_t lengthDoublings, int32_t defaultLengthDoublings,
	                                float minFreqHz, float maxFreqHz, bool doPrimeTest);
	/// @}
	uint32_t getLengthInMSec();
	SampleCache* getOrCreateCache(SampleHolder* sampleHolder, int32_t phaseIncrement, int32_t timeStretchRatio,
	                              bool reversed, bool mayCreate, bool* created);
	void deleteCache(SampleCache* cache);
	int32_t getFirstClusterIndexWithAudioData();
	int32_t getFirstClusterIndexWithNoAudioData();
	/// @brief Fills the perc store from @p startPosSamples towards @p endPosSamples in @p playDirection, at most
	/// @p maxNumSamplesToProcess frames, through the shared perc analysis; returns what the time stretcher reading it
	/// must keep and reserve.
	deluge::synth::PercFill fillPercCache(int32_t startPosSamples, int32_t endPosSamples, int32_t playDirection,
	                                      int32_t maxNumSamplesToProcess);
	void percCacheClusterStolen(ComputedChunk* cluster);
	void deletePercCache(bool beingDestructed = false);
	/// @brief The perc bytes a hop's search may read around @p pixel, played @p direction (B3): bytes[0] is the
	/// window's lowest pixel. Nullopt where no filled zone holds the pixel.
	std::optional<deluge::dsp::fixed::timestretch::PercWindow>
	perc_at(int32_t pixel, deluge::dsp::fixed::sample_read::Direction direction);
	/// @brief The lowest and highest sample value the peak index has found, at 32-bit scale.
	struct FoundRange {
		int32_t min{0}; ///< the lowest value
		int32_t max{0}; ///< the highest value
	};
	/// @return The range over every block scanned so far; zeros before the first.
	[[nodiscard]] FoundRange foundRange() const;

	int32_t getMaxPeakFromZero();
	int32_t getFoundValueCentrePoint();
	int32_t getValueSpan();

	// Discards the cached per-cluster waveform overview (issue #4460) and rewinds the background pre-scan,
	// so it gets rebuilt from scratch. Call when the underlying audio data may have changed on disk.
	void resetOverviewScan();
	void finalizeAfterLoad(uint32_t fileSize) override;

	/// @brief This sample's waveform peak index, or null if it could not be made.
	[[nodiscard]] DelugePeakIndex* peaks() const { return peaks_; }

	/// @brief Make this sample a new, empty peak index, dropping any it had, and bind it to the stream asset if there
	///        is one. Thread mode.
	/// @param frames   The frames the sample holds (a take: those captured so far).
	/// @param growing  True for a take whose length is not known yet.
	/// @param capacity The most frames the index may come to hold.
	void makePeaks(uint64_t frames, bool growing, uint64_t capacity);

	/// @brief Forget every peak: a new, empty index of the sample's length, and the scan starts over. Call when the
	///        audio on the card may have changed.
	void resetPeaks();

	/// @brief The frame the background peak scan looks from next.
	uint64_t peaksScanFrom{0};

	/// @brief This Sample's audio-stream orchestrator.
	///
	/// Owns the read-stream handle, the resource-manager Asset for this Sample's clusters, and the
	/// ReadSource selection.
	/// @return The owned SampleStream, by reference.
	/// @see storage/audio/stream/sample_stream.h
	[[nodiscard]] deluge::audio::stream::SampleStream& stream() { return stream_; }
	/// @copydoc stream()
	///
	/// Const overload -- lets a `const Sample&` consumer reach read-only accessors without dropping
	/// const.
	[[nodiscard]] const deluge::audio::stream::SampleStream& stream() const { return stream_; }

	/// @return The physical entry count of the waveform overview cache (`overviewCache_.size()`).
	[[nodiscard]] size_t overviewCacheSize() const { return overviewCache_.size(); }
	/// @brief Access the waveform overview cache entry for cluster @p index.
	[[nodiscard]] OverviewCacheEntry& overviewCacheEntry(uint32_t index) { return overviewCache_[index]; }
	/// @copydoc overviewCacheEntry(uint32_t)
	[[nodiscard]] const OverviewCacheEntry& overviewCacheEntry(uint32_t index) const { return overviewCache_[index]; }
	/// @brief Resize the waveform overview cache to exactly @p n entries.
	///
	/// Sized at the same hooks a sample's cluster count can change at (initialize, finalize-grow,
	/// truncate-shrink -- see those call sites). Never grown concurrently with a reader, so no
	/// `reserve()` pre-sizing is needed.
	void resizeOverviewCache(size_t n) { overviewCache_.resize(n); }

	// A float sample's native word, Q31, as convert_word gives an integer one's
	[[nodiscard]] int32_t convertToNative(float value) const { return deluge::dsp::from_float<31>(value).raw(); }

	[[nodiscard]] int32_t convertToNative(int32_t value) const {
		return deluge::audio::stream::convert_word(value, rawDataFormat);
	}

	std::string tempFilePathForRecording{};
	uint8_t byteDepth{0};
	uint32_t sampleRate{44100};
	uint32_t audioDataStartPosBytes; // That is, the offset from the start of the WAV file
	uint64_t audioDataLengthBytes;

	/// @brief Sentinel value for `audioDataLengthBytes` / `lengthInSamples` meaning "not yet known".
	///
	/// Set while a recording is in progress and its final length hasn't been determined yet.
	/// @see deluge::sample_length::kUnknownLengthSentinel (sample_length_sentinel.h) -- the same
	///      value, standalone for consumers that don't want the full `Sample` class.
	static constexpr uint64_t kUnknownLengthSentinel = deluge::sample_length::kUnknownLengthSentinel;
	/// @return Whether this Sample's length has been determined (i.e. is not the "unknown" sentinel).
	[[nodiscard]] bool isLengthKnown() const { return audioDataLengthBytes != kUnknownLengthSentinel; }

	/// @return The cluster count implied purely by this sample's audio-data extent (start + length),
	///         rounded up. Only meaningful once `isLengthKnown()` -- the caller's job to check.
	[[nodiscard]] uint32_t geometricClusterCount() const {
		return ((audioDataStartPosBytes + audioDataLengthBytes - 1) >> Cluster::size_magnitude) + 1;
	}

	/// @return The sample's logical cluster count, derived: the geometry implied by its audio-data extent
	///         once `isLengthKnown()`, else the live recorder's captured-cluster count while recording.
	[[nodiscard]] size_t num_clusters() const;

	uint32_t bitMask{0};

	uint64_t lengthInSamples;

	// These two are for holding a value loaded from file
	uint32_t fileLoopStartSamples; // And during recording, this one also stores the final value once known
	uint32_t fileLoopEndSamples;

	float midiNoteFromFile; // -1 means none

	RawDataFormat rawDataFormat;

	bool unloadable{false}; // Only gets set to true if user has re-inserted the card and the sample appears to have
	                        // been deleted / moved / modified
	bool unplayable{false};
	bool partOfFolderBeingLoaded;
	bool fileExplicitlySpecifiesSelfAsWaveTable{false};

#if SAMPLE_DO_LOCKS
	bool lock;
#endif

	float midiNote; // -999 means not worked out yet. -1000 means error working out

	// int32_t valueSpan; // -2147483648 means both these are uninitialized
	int32_t minValueFound;
	int32_t maxValueFound;

	/// This object's lifetime serial, taken from one counter when it is constructed: never 0 and never taken again, so
	/// a sample constructed later in a freed sample's memory has another. A waveform's identity
	/// (`gui::shell_host::waveform_identity`) is this serial.
	const uint64_t serial;

	// Background "waveform overview" pre-scan cursor (issue #4460). The next cluster index that the idle
	// pre-scan should investigate-whole-length, so that zoomed-out single-row rendering finds per-cluster
	// min/max already cached and never has to load clusters synchronously mid-scroll. Seeded to the first
	// audio cluster by advanceOverviewScan / resetOverviewScan (clusters aren't known at construction).
	int32_t overviewScanNextCluster{0};

	deluge::fast_vector<SampleCacheElement> caches{}; // Sorted ascending by SampleCacheElement::key()

	uint8_t* percCacheMemory[2]{nullptr, nullptr}; // One for each play-direction: 0=forwards; 1=reversed
	// One for each play-direction: 0=forwards; 1=reversed. Sorted ascending by startPos. On the external
	// (non-stealable) region so that growing a zone list can never steal this sample's own perc-cache clusters,
	// which would re-enter these arrays mid-modification.
	deluge::vector<SamplePercCacheZone> percCacheZones[2]{};

	ComputedChunk** percCacheClusters[2]{nullptr, nullptr}; // One for each play-direction: 0=forwards; 1=reversed
	int32_t numPercCacheClusters{};
	// Resource-manager Asset id per play-direction for the perc-cache clusters (construct-only,
	// leased-while-nearby by the stretcher). DELUGE_RESOURCE_NO_ASSET (0xFFFFFFFF) = legacy.
	uint32_t percCacheAssetId[2]{0xFFFFFFFFu, 0xFFFFFFFFu};

	int32_t beginningOffsetForPitchDetection;
	bool beginningOffsetForPitchDetectionFound;

	uint32_t waveTableCycleSize{0}; // In case this later gets used for a WaveTable

	/// @brief Owns the read-stream handle and the resource-manager Asset.
	///
	/// See storage/audio/stream/sample_stream.h.
	/// @note ~Sample releases the Asset explicitly, before `stream_` destructs -- see ~Sample's
	///       definition.
	deluge::audio::stream::SampleStream stream_{*this};

	/// @brief The waveform overview cache: one `OverviewCacheEntry` per cluster of the file.
	///
	/// A stable-address `SegmentedVector` so a `OverviewCacheEntry&` handed out by
	/// overviewCacheEntry() stays valid across later growth -- see `resizeOverviewCache()`.
	deluge::SegmentedVector<OverviewCacheEntry, 256, deluge::memory::fast_allocator> overviewCache_{};

	/// @brief The waveform peak index: one reference, dropped in the destructor or by `makePeaks`.
	DelugePeakIndex* peaks_{nullptr};

protected:
	// Project-relevance hooks (the object's hard-lease 0↔1 transitions): toggle the soft-reference on
	// this sample's resource assets so current-song data is kept resident over no-song data under
	// memory pressure. Present in all builds (the bug-check inside DecreasedToZero is ALPHA-only).
	void numReasonsIncreasedFromZero() override;
	void numReasonsDecreasedToZero(char const* errorCode) override;

private:
	// Soft-reference (on=true) / un-reference (on=false) this sample's cluster asset + every derived
	// cache (repitch caches + the two perc-cache directions) that is currently defined.
	void applyProjectReference(bool on);

protected:
private:
	int32_t investigateFundamentalPitch(int32_t fundamentalIndexProvided, int32_t tableSize, int32_t* heightTable,
	                                    uint64_t* sumTable, float* floatIndexTable, float* getFreq,
	                                    int32_t numDoublings, bool doPrimeTest);
};
