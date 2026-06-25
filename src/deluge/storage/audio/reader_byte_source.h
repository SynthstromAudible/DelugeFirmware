/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

#include "storage/audio/audio_byte_source.h"
#include "storage/audio/audio_file_reader.h"

/// Transitional adapter exposing a legacy `AudioFileReader` (SampleReader / WaveTableReader) as an
/// `AudioByteSource`, so the parser can move to the new interface before the cluster/deserializer-backed
/// sources are written. Retired once `ClusterByteSource` / `DeserializerByteSource` replace the readers.
class ReaderByteSource final : public AudioByteSource {
public:
	explicit ReaderByteSource(AudioFileReader& reader) : reader_(reader) {}

	[[nodiscard]] Error read(std::span<std::byte> dest) override {
		return reader_.readBytes(reinterpret_cast<char*>(dest.data()), static_cast<int32_t>(dest.size()));
	}
	[[nodiscard]] uint32_t pos() override { return reader_.getBytePos(); }
	void seekForwardTo(uint32_t absolutePos) override { reader_.jumpForwardToBytePos(absolutePos); }
	[[nodiscard]] uint32_t size() const override { return reader_.fileSize; }

private:
	AudioFileReader& reader_;
};
