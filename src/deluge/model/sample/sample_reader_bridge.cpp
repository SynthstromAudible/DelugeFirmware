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

#include "model/sample/sample_reader_bridge.h"

#include "storage/audio/stream/chunk_residency.h" // deluge_streaming_define_asset()

namespace deluge::sample {

uint32_t source_id_for(const Sample& sample) {
	// deluge_streaming_define_asset() only needs an address to register/look up -- Sample stays an
	// incomplete type here (chunk_residency.h forward-declares it), matching that function's own
	// non-const `Sample*` parameter (it may lazily define the asset on first call).
	return deluge_streaming_define_asset(const_cast<Sample*>(&sample));
}

SampleFrameReader::SampleFrameReader(uint32_t source_id, uint64_t start_frame, int8_t direction, DelugeReadHint hint)
    : reader_(deluge_sample_reader_open(source_id, start_frame, direction, hint)) {
}

SampleFrameReader::SampleFrameReader(SampleFrameReader&& other) noexcept : reader_(other.reader_) {
	other.reader_ = nullptr;
}

SampleFrameReader& SampleFrameReader::operator=(SampleFrameReader&& other) noexcept {
	if (this != &other) {
		if (reader_ != nullptr) {
			deluge_sample_reader_close(reader_);
		}
		reader_ = other.reader_;
		other.reader_ = nullptr;
	}
	return *this;
}

SampleFrameReader::~SampleFrameReader() {
	if (reader_ != nullptr) {
		deluge_sample_reader_close(reader_);
	}
}

DelugeFrameWindow SampleFrameReader::window() {
	return deluge_sample_reader_window(reader_);
}

void SampleFrameReader::advance(uint32_t frames) {
	deluge_sample_reader_advance(reader_, frames);
}

void SampleFrameReader::seek(uint64_t frame) {
	deluge_sample_reader_seek(reader_, frame);
}

bool SampleFrameReader::ok() const {
	return deluge_sample_reader_ok(reader_);
}

uint32_t SampleFrameReader::read(uint32_t source_id, uint64_t start_frame, uint32_t num_frames, void* dest,
                                 size_t dest_bytes) {
	return deluge_sample_read(source_id, start_frame, num_frames, dest, dest_bytes);
}

} // namespace deluge::sample
