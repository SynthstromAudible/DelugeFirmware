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

#include "storage/audio/peak_sidecars.h"
#include "board_config.h" // PLACE_SDRAM_TEXT: thread mode, an idle tick at a time
#include "libdeluge/file_bytes.h"
#include "memory/heaps.h"
#include "model/sample/sample.h"
#include "storage/audio/audio_file_vector.h"

namespace deluge::storage {

namespace {

/// @brief The loaded sample with @p serial, if it is still loaded.
PLACE_SDRAM_TEXT Sample* sampleBySerial(AudioFileVector& files, uint64_t serial) {
	for (AudioFile* f : files) {
		if (AudioFile::isSample(f) && static_cast<Sample*>(f)->serial == serial) {
			return static_cast<Sample*>(f);
		}
	}
	return nullptr;
}

} // namespace

PLACE_SDRAM_TEXT std::optional<DelugePeakKey> sidecarKey(const Sample& sample) {
	const std::optional<DelugeFileIdentity> id = sample.fileIdentity();
	if (!id) {
		return std::nullopt;
	}
	return DelugePeakKey{.first_cluster = id->first_cluster,
	                     .file_size = id->size,
	                     .fat_datetime = id->fat_datetime,
	                     .sample_rate = sample.sampleRate,
	                     .frames = sample.lengthInSamples,
	                     .channels = static_cast<uint8_t>(sample.numChannels),
	                     .byte_depth = static_cast<uint8_t>(sample.byteDepth),
	                     .reserved = 0};
}

template <typename T, size_t N>
PLACE_SDRAM_TEXT void PeakSidecars::Ring<T, N>::push(T v) {
	if (count == N) {
		head = (head + 1) & (N - 1); // full: the oldest goes
		count--;
	}
	items[(head + count) & (N - 1)] = v;
	count++;
}

template <typename T, size_t N>
PLACE_SDRAM_TEXT std::optional<T> PeakSidecars::Ring<T, N>::pop() {
	if (count == 0) {
		return std::nullopt;
	}
	const T v = items[head];
	head = (head + 1) & (N - 1);
	count--;
	return v;
}

PLACE_SDRAM_TEXT void PeakSidecars::wantLoad(const Sample& sample) {
	loads_.push(sample.serial);
}

PLACE_SDRAM_TEXT bool PeakSidecars::busy() const {
	return op_ != Op::None || loads_.count != 0 || unlinks_.count != 0;
}

PLACE_SDRAM_TEXT void PeakSidecars::tick(AudioFileVector& files, bool cardReady) {
	if (pendingFree_ != nullptr && !deluge_file_bytes_holds(pendingFree_)) {
		deluge::memory::dealloc(pendingFree_);
		pendingFree_ = nullptr;
	}
	switch (op_) {
	case Op::Read:
		collectRead(files);
		return;
	case Op::Unlink:
		collectUnlink();
		return;
	case Op::None:
		break;
	}
	if (!cardReady || pendingFree_ != nullptr) {
		return;
	}
	if (unlinks_.count != 0) {
		startUnlink();
	}
	else if (loads_.count != 0) {
		startRead(files);
	}
}

PLACE_SDRAM_TEXT void PeakSidecars::startRead(AudioFileVector& files) {
	const std::optional<uint64_t> serial = loads_.pop();
	Sample* sample = serial ? sampleBySerial(files, *serial) : nullptr;
	if (sample == nullptr) {
		return;
	}
	const std::optional<DelugePeakKey> key = sidecarKey(*sample);
	DelugePeakIndex* peaks = sample->peaks();
	if (!key || peaks == nullptr || sample->unloadable) {
		sample->sidecar.looked = true;
		return;
	}
	const uint32_t peakLen = deluge_peaks_sidecar_len(peaks, nullptr, 0);
	if (peakLen == 0) {
		sample->sidecar.looked = true;
		return;
	}
	bufferCap_ = peakLen + kExtraCap;
	buffer_ = static_cast<uint8_t*>(deluge::memory::alloc_sdram(bufferCap_));
	if (buffer_ == nullptr) {
		loads_.push(*serial); // memory is short: ask again on a later tick
		return;
	}
	opPath_ = sidecarPath(key->first_cluster, SidecarFile::PK);
	if (!deluge_file_bytes_request_read_into(opPath_.data(), buffer_, bufferCap_)) {
		loads_.push(*serial); // refused, not failed: the channel is busy
		releaseBuffer();
		return;
	}
	opSerial_ = *serial;
	op_ = Op::Read;
}

PLACE_SDRAM_TEXT void PeakSidecars::collectRead(AudioFileVector& files) {
	uint32_t got = 0;
	DelugeStatus error = DELUGE_OK;
	uint32_t needed = 0;
	const DelugeChannelStatus status = deluge_file_bytes_collect_read_into(opPath_.data(), &got, &error, &needed);
	if (status == DELUGE_CHANNEL_PENDING) {
		return;
	}
	op_ = Op::None;
	Sample* sample = sampleBySerial(files, opSerial_);
	const std::optional<DelugePeakKey> key = sample != nullptr ? sidecarKey(*sample) : std::nullopt;
	if (sample != nullptr) {
		sample->sidecar.looked = true;
	}
	if (status == DELUGE_CHANNEL_READY && key) {
		const bool sound = deluge_peaks_sidecar_check(buffer_, got, &*key) == 0
		                   && deluge_peaks_sidecar_restore(sample->peaks(), &*key, buffer_, got) >= 0;
		if (!sound) {
			unlinks_.push(key->first_cluster);
		}
	}
	else if (status == DELUGE_CHANNEL_FAILED && error == DELUGE_ERR_NO_MEMORY && key) {
		unlinks_.push(key->first_cluster); // larger than any sidecar this firmware writes for this sample
	}
	releaseBuffer();
}

PLACE_SDRAM_TEXT void PeakSidecars::startUnlink() {
	const std::optional<uint32_t> cluster = unlinks_.pop();
	if (!cluster) {
		return;
	}
	opPath_ = sidecarPath(*cluster, SidecarFile::PK);
	if (!deluge_file_bytes_request_unlink(opPath_.data())) {
		unlinks_.push(*cluster);
		return;
	}
	op_ = Op::Unlink;
}

PLACE_SDRAM_TEXT void PeakSidecars::collectUnlink() {
	DelugeStatus result = DELUGE_OK;
	if (deluge_file_bytes_collect_unlink(opPath_.data(), &result) == DELUGE_CHANNEL_PENDING) {
		return;
	}
	op_ = Op::None;
}

PLACE_SDRAM_TEXT void PeakSidecars::releaseBuffer() {
	if (buffer_ == nullptr) {
		return;
	}
	if (deluge_file_bytes_holds(buffer_)) {
		pendingFree_ = buffer_;
	}
	else {
		deluge::memory::dealloc(buffer_);
	}
	buffer_ = nullptr;
	bufferCap_ = 0;
}

template struct PeakSidecars::Ring<uint64_t, 16>;
template struct PeakSidecars::Ring<uint32_t, 8>;

} // namespace deluge::storage
