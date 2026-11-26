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

#pragma once

/// @file
/// @brief The card's peak sidecars: what the firmware works out from a sample's audio, kept beside it.

#include "deluge_storage_sample_peaks.h"
#include "storage/audio/sidecar_path.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

class AudioFileVector;
class Sample;

namespace deluge::storage {

/// @brief The key of @p sample's sidecar: its file's identity and the shape of its samples. Nothing until the sample
///        knows its file's identity.
[[nodiscard]] std::optional<DelugePeakKey> sidecarKey(const Sample& sample);

/// @brief The card's peak sidecars, read and written one byte-channel operation at a time from the idle job.
///
/// A sample's sidecar is looked up when its index is made (wantLoad); the next tick reads it, checks it against the
/// sample's key and restores it into the index. A sidecar that fails a check is deleted, so a correct one replaces
/// it. At most once a second, the loaded samples are searched for one whose sidecar is due (shouldWriteSidecar):
/// it is written as `.TMP`, the old `.PK` removed, and the `.TMP` renamed over it, so a crash leaves the old sidecar
/// or none, never a torn one. After the session's first successful write the folder is marked hidden, once. Every
/// operation starts only when the card may be used and the byte channel has finished nothing for kQuietSeconds, so the
/// sidecars, which are idle work, never keep the browser's own requests waiting, and is collected on a later tick,
/// never waited for.
class PeakSidecars {
public:
	/// @brief Room past the peak section that a read allows for the other sections this firmware writes.
	static constexpr uint32_t kExtraCap = 1024;
	/// @brief How long the byte channel must have finished nothing before a sidecar operation starts, in seconds.
	static constexpr double kQuietSeconds = 0.3;

	/// @brief Look up @p sample's sidecar on a later tick. Thread mode.
	void wantLoad(const Sample& sample);
	/// @brief One step: collect the operation in flight, or start the next. Thread mode, idle job.
	/// @param files     The loaded audio files, searched by serial.
	/// @param cardReady Whether a new card operation may start: the card is present and not busy, and the audio
	///                  routine is not held.
	void tick(AudioFileVector& files, bool cardReady);
	/// @brief Whether an operation is in flight or queued.
	[[nodiscard]] bool busy() const;

private:
	enum class Op : uint8_t { None, Read, Unlink, WriteTmp, ReplaceUnlink, Rename, Hide };

	/// @brief A small ring of values, oldest first; a full ring drops its oldest.
	template <typename T, size_t N>
	struct Ring {
		std::array<T, N> items{};
		size_t head{0};
		size_t count{0};
		void push(T v);
		[[nodiscard]] std::optional<T> pop();
	};

	void startRead(AudioFileVector& files);
	void collectRead(AudioFileVector& files);
	void startUnlink();
	void collectUnlink();
	/// @brief Find a sample whose sidecar is due and start writing it.
	void startWrite(AudioFileVector& files);
	/// @brief Start the step @p step of the write in progress.
	void startWriteStep(Op step);
	void collectWriteStep(AudioFileVector& files);
	/// @brief End the write in progress: record its outcome on its sample.
	void finishWrite(AudioFileVector& files, bool ok);
	/// @brief Let go of the read buffer, now or once the channel no longer holds it.
	void releaseBuffer();

	Ring<uint64_t, 16> loads_;
	Op next_{Op::None};              ///< the write's next step, started when the card may be used
	std::array<char, 32> tmpPath_{}; ///< the write's `.TMP`
	std::array<char, 32> pkPath_{};  ///< the write's `.PK`
	uint32_t writtenComplete_{0};    ///< complete level-1 entries in the sidecar being written
	double lastSearch_{-1};          ///< when the loaded samples were last searched for a due write, in seconds
	bool hideWanted_{false};         ///< a write succeeded and the folder is not hidden yet this session
	bool hidden_{false};             ///< the folder was marked hidden this session
	uint32_t channelMark_{0};        ///< deluge_file_bytes_progress() when last read
	bool channelMarkSeen_{false};    ///< channelMark_ holds a reading
	double quietSince_{0};           ///< when the channel last finished something, in seconds
	Ring<uint32_t, 8> unlinks_;
	Op op_{Op::None};
	uint64_t opSerial_{0};
	std::array<char, 32> opPath_{};
	uint8_t* buffer_{nullptr};
	uint32_t bufferCap_{0};
	uint8_t* pendingFree_{nullptr};
};

} // namespace deluge::storage
