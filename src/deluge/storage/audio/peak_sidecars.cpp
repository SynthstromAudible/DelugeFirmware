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
#include "libdeluge/scheduler.h"
#include "memory/heaps.h"
#include "model/sample/sample.h"
#include "storage/audio/audio_file_vector.h"
#include "storage/audio/sidecar_policy.h"

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

/// @brief What the write rule reads about @p sample.
PLACE_SDRAM_TEXT SidecarProgress progressOf(Sample& sample) {
	sample.refreshIdentity(); // a finished take learns its file's identity once its stream is open
	DelugePeakIndex* peaks = sample.peaks();
	return {.keyed = sample.fileIdentity().has_value() && peaks != nullptr,
	        .looked = sample.sidecar.looked,
	        .final = sample.isLengthKnown() && !sample.unloadable,
	        .complete = deluge_peaks_complete_level1(peaks),
	        .entries = deluge_peaks_level1_entries(peaks),
	        .written = sample.sidecar.written,
	        .failures = sample.sidecar.failures,
	        .pitchUnwritten = sample.pitchResults().unwritten()};
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

PLACE_SDRAM_TEXT void PeakSidecars::wantWrite(const Sample& sample) {
	writeHints_.push(sample.serial);
}

PLACE_SDRAM_TEXT bool PeakSidecars::busy() const {
	return op_ != Op::None || next_ != Op::None || loads_.count != 0 || unlinks_.count != 0;
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
	case Op::WriteTmp:
	case Op::ReplaceUnlink:
	case Op::Rename:
		collectWriteStep(files);
		return;
	case Op::None:
		break;
	}
	if (!cardReady) {
		return;
	}
	if (next_ != Op::None) {
		startWriteStep(next_);
		return;
	}
	if (pendingFree_ != nullptr) {
		return;
	}
	if (unlinks_.count != 0) {
		startUnlink();
	}
	else if (loads_.count != 0) {
		startRead(files);
	}
	else {
		startWrite(files);
	}
}

PLACE_SDRAM_TEXT void PeakSidecars::startWrite(AudioFileVector& files) {
	const double now = getSystemTime();
	const bool hinted = writeHints_.count != 0;
	if (!hinted && lastSearch_ >= 0 && now - lastSearch_ < 1.0) {
		return;
	}
	Sample* due = nullptr;
	while (due == nullptr && writeHints_.count != 0) {
		Sample* s = sampleBySerial(files, *writeHints_.pop());
		if (s != nullptr && shouldWriteSidecar(progressOf(*s))) {
			due = s;
		}
	}
	if (due == nullptr) {
		lastSearch_ = now;
		for (AudioFile* f : files) {
			if (AudioFile::isSample(f) && shouldWriteSidecar(progressOf(*static_cast<Sample*>(f)))) {
				due = static_cast<Sample*>(f);
				break;
			}
		}
	}
	if (due == nullptr) {
		return;
	}
	const std::optional<DelugePeakKey> key = sidecarKey(*due);
	DelugePeakIndex* peaks = due->peaks();
	deluge::sample::PitchResults& pitches = due->pitchResults();
	DelugeSidecarSection extra{};
	uint32_t extras = 0;
	if (!pitches.empty()) {
		const std::span<const uint8_t> ptch = pitches.encode();
		extra = {
		    .tag = deluge::sample::PitchResults::kTag, .bytes = ptch.data(), .len = static_cast<uint32_t>(ptch.size())};
		extras = 1;
	}
	const uint32_t len = key ? deluge_peaks_sidecar_len(peaks, &extra, extras) : 0;
	buffer_ = len != 0 ? static_cast<uint8_t*>(deluge::memory::alloc_sdram(len)) : nullptr;
	if (buffer_ == nullptr) {
		return; // memory is short: the next search finds it again
	}
	bufferCap_ = len;
	writtenComplete_ = deluge_peaks_complete_level1(peaks);
	const uint32_t n = deluge_peaks_sidecar_write(peaks, &*key, &extra, extras, buffer_, len);
	if (n == 0) {
		due->sidecar.failures++;
		releaseBuffer();
		return;
	}
	pitches.markWritten(); // marked again if this write fails
	opSerial_ = due->serial;
	tmpPath_ = sidecarPath(key->first_cluster, SidecarFile::TMP);
	pkPath_ = sidecarPath(key->first_cluster, SidecarFile::PK);
	startWriteStep(Op::WriteTmp);
}

PLACE_SDRAM_TEXT void PeakSidecars::startWriteStep(Op step) {
	next_ = Op::None;
	bool asked = false;
	switch (step) {
	case Op::WriteTmp:
		asked = deluge_file_bytes_request_write_from(tmpPath_.data(), buffer_, bufferCap_, /*may_overwrite=*/true);
		break;
	case Op::ReplaceUnlink:
		asked = deluge_file_bytes_request_unlink(pkPath_.data());
		break;
	case Op::Rename:
		asked = deluge_file_bytes_request_rename(tmpPath_.data(), pkPath_.data());
		break;
	default:
		return;
	}
	if (asked) {
		op_ = step;
	}
	else {
		next_ = step; // refused, not failed: ask again on a later tick
	}
}

PLACE_SDRAM_TEXT void PeakSidecars::collectWriteStep(AudioFileVector& files) {
	DelugeStatus result = DELUGE_OK;
	DelugeChannelStatus status = DELUGE_CHANNEL_PENDING;
	switch (op_) {
	case Op::WriteTmp: {
		uint32_t written = 0;
		status = deluge_file_bytes_collect_write_from(tmpPath_.data(), &written, &result);
		if (status == DELUGE_CHANNEL_READY && written != bufferCap_) {
			status = DELUGE_CHANNEL_FAILED;
		}
		break;
	}
	case Op::ReplaceUnlink:
		status = deluge_file_bytes_collect_unlink(pkPath_.data(), &result);
		if (status == DELUGE_CHANNEL_FAILED && result == DELUGE_ERR_NOT_FOUND) {
			status = DELUGE_CHANNEL_READY; // nothing to replace
		}
		break;
	case Op::Rename:
		status = deluge_file_bytes_collect_rename(pkPath_.data(), &result);
		break;
	default:
		return;
	}
	if (status == DELUGE_CHANNEL_PENDING) {
		return;
	}
	const Op done = op_;
	op_ = Op::None;
	if (status != DELUGE_CHANNEL_READY) {
		finishWrite(files, false);
		return;
	}
	if (done == Op::WriteTmp) {
		releaseBuffer(); // on the card now
		next_ = Op::ReplaceUnlink;
	}
	else if (done == Op::ReplaceUnlink) {
		next_ = Op::Rename;
	}
	else {
		finishWrite(files, true);
	}
}

PLACE_SDRAM_TEXT void PeakSidecars::finishWrite(AudioFileVector& files, bool ok) {
	next_ = Op::None;
	releaseBuffer();
	Sample* sample = sampleBySerial(files, opSerial_);
	if (sample == nullptr) {
		return;
	}
	if (ok) {
		sample->sidecar.written = writtenComplete_;
	}
	else {
		sample->sidecar.failures++;
		sample->pitchResults().markUnwritten();
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
		else {
			const uint8_t* ptch = nullptr;
			uint32_t ptchLen = 0;
			if (deluge_peaks_sidecar_section(buffer_, got, &*key, deluge::sample::PitchResults::kTag, &ptch,
			                                 &ptchLen)) {
				sample->restorePitches({ptch, ptchLen});
			}
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
template struct PeakSidecars::Ring<uint64_t, 8>;
template struct PeakSidecars::Ring<uint32_t, 8>;

} // namespace deluge::storage
