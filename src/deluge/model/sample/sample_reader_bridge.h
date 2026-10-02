/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

/// @file
/// A small C++23 bridge over `libdeluge/sample_reader.h` (the Rust `deluge_sample_reader_*`
/// C-ABI) for non-voice consumers that read raw-PCM frame RANGES (pitch detect, waveform
/// overview, crossfade averages, perc-cache, wavetable build, hop search) instead of reaching
/// `StreamedChunk` internals directly. This is the residency read path for those consumers.

#include "libdeluge/sample_reader.h"

#include <cstddef>
#include <cstdint>

class Sample;

namespace deluge::sample {

/// @brief Resolve @p sample's resource-manager Asset id -- the `source_id` every
///        `SampleFrameReader`/`SampleFrameReader::read` call needs.
///
/// Thin wrapper over `deluge_streaming_define_asset()`: idempotent (returns the cached id on
/// later calls), and resolves to the SAME asset the voice region port
/// (`sample_source.h`'s `DelugeSampleSource`) already reads through -- so a reader opened here and
/// the playing voice share one residency per sample, exactly like two `peek()`s of the same
/// `SampleStream` do today.
/// @param sample The sample whose Asset id to resolve.
/// @return The Asset id.
uint32_t source_id_for(const Sample& sample);

/// @brief RAII handle over a `DelugeSampleReader` -- a zero-copy streaming frame-cursor over one
///        sample's source residency.
///
/// Non-copyable, movable: exactly one `SampleFrameReader` ever owns a given `DelugeSampleReader*`
/// at a time, and the destructor unconditionally closes it, so a move must null the moved-from
/// handle to keep that invariant true (mirrors `deluge::io::File`'s RAII shape over its own C-ABI
/// handle, `io/file.hpp`).
class SampleFrameReader {
public:
	/// @brief Open a reader over @p source_id's sample residency, positioned at @p start_frame and
	///        reading in @p direction. See `deluge_sample_reader_open`'s header doc for the full
	///        contract.
	/// @param source_id   The resource-manager Asset id (see `source_id_for`).
	/// @param start_frame The frame to position the cursor at.
	/// @param direction   +1 forward, -1 reverse.
	/// @param hint        Eviction-sharing hint -- see `DelugeReadHint`.
	SampleFrameReader(uint32_t source_id, uint64_t start_frame, int8_t direction, DelugeReadHint hint);

	SampleFrameReader(const SampleFrameReader&) = delete;
	SampleFrameReader& operator=(const SampleFrameReader&) = delete;

	/// @brief Move-construct, nulling @p other's handle so its destructor becomes a no-op.
	SampleFrameReader(SampleFrameReader&& other) noexcept;
	/// @brief Move-assign: close whatever this reader currently holds, then steal @p other's
	///        handle and null it, so @p other's destructor becomes a no-op.
	SampleFrameReader& operator=(SampleFrameReader&& other) noexcept;

	/// @brief Closes the reader and releases any pin it still holds.
	~SampleFrameReader();

	/// @brief The contiguous run of valid, already-converted frames at the cursor. BLOCKS to make
	///        the data resident. See `deluge_sample_reader_window`'s header doc for the full
	///        contract, including how `frame_count == 0` disambiguates end-of-audio from a hard
	///        failure via `ok()`.
	[[nodiscard]] DelugeFrameWindow window();

	/// @brief Advance the cursor @p frames frames, in this reader's own direction. See
	///        `deluge_sample_reader_advance`'s header doc for the full contract.
	void advance(uint32_t frames);

	/// @brief Random-access reposition to @p frame. See `deluge_sample_reader_seek`'s header doc
	///        for the full contract.
	void seek(uint64_t frame);

	/// @brief True unless the last `window()` call failed to make its data resident -- distinct
	///        from ordinary end-of-audio (`window().frame_count == 0` with `ok()` still true). See
	///        `deluge_sample_reader_ok`'s header doc for the full contract.
	[[nodiscard]] bool ok() const;

	/// @brief Stateless convenience: copy `[start_frame, start_frame + num_frames)` native-format
	///        frames of @p source_id's sample into @p dest. Forwards verbatim to
	///        `deluge_sample_read` -- see that function's header doc for the full contract
	///        (blocking, internally an open/window/close loop, never writes past @p dest_bytes).
	/// @param source_id   The resource-manager Asset id (see `source_id_for`).
	/// @param start_frame The first frame to copy.
	/// @param num_frames  How many frames to copy.
	/// @param dest        Destination buffer.
	/// @param dest_bytes  Bound on the write -- never exceeded.
	/// @return Frames actually written (short at end-of-audio).
	static uint32_t read(uint32_t source_id, uint64_t start_frame, uint32_t num_frames, void* dest, size_t dest_bytes);

private:
	DelugeSampleReader* reader_;
};

} // namespace deluge::sample
