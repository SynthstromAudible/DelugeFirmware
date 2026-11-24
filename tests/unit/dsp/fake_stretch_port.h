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

// The time stretcher specs' sample port: a synthetic 16-bit stereo saw of a given length, every frame resident unless
// missing_from is set, with scripted seats and a log of every call the stretcher makes.

#include "synth/sources/sample/sample_stretch_port.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace fake_stretch_port {

using deluge::dsp::Q;
using deluge::dsp::UQ;
using deluge::dsp::fixed::sample_read::Direction;
using deluge::dsp::fixed::sample_read::FrameRun;
using deluge::synth::HeadGain;
using deluge::synth::HeadMark;
using deluge::synth::PercChunk;
using deluge::synth::PercFill;
using deluge::synth::Reservation;
using deluge::synth::SampleStretchPort;
using deluge::synth::SeatResult;
using deluge::synth::SourceFacts;

struct SeatCall {
	int32_t frame;
	uint32_t phase;
	LoopType loop;
};
struct ReadCall {
	SampleStretchPort::Head head;
	int32_t frames;
	HeadGain gain;
};
struct KeepCall {
	const Reservation* reservation;
	int32_t frame;
};

class FakeStretchPort final : public SampleStretchPort {
public:
	static constexpr int32_t kDataStart = 44;
	static constexpr int32_t kChannels = 2;
	static constexpr int32_t kByteDepth = 2;

	explicit FakeStretchPort(int32_t length_frames) : length_(length_frames) {
		// Four bytes of slack before the data, as a chunk's payload has, then the saw
		bytes_.assign(static_cast<size_t>(4 + (length_frames * kChannels * kByteDepth) + 8), std::byte{0});
		for (int32_t f = 0; f < length_frames; f++) {
			for (int32_t c = 0; c < kChannels; c++) {
				const auto v = static_cast<int16_t>((f * (97 + (c * 31))) & 0x7FFF);
				const size_t at = 4 + static_cast<size_t>(((f * kChannels) + c) * kByteDepth);
				bytes_[at] = static_cast<std::byte>(v & 0xFF);
				bytes_[at + 1] = static_cast<std::byte>((v >> 8) & 0xFF);
			}
		}
	}

	// Scripts
	std::deque<SeatResult> seats;        ///< Seat answers, STARTED once empty.
	bool meet = false;                   ///< cursors_meet's answer.
	std::optional<int64_t> synced;       ///< synced_position's answer.
	std::optional<int32_t> missing_from; ///< Frames from here on (in the play direction) are not resident.
	int32_t start_byte = kDataStart;     ///< The guide's start byte after a loop.
	int32_t start_marker_frame = 0;
	int32_t mark_byte = kDataStart + (1000 * kChannels * kByteDepth);
	bool newer_alive = true; ///< Whether a read of the newer head goes on.
	bool older_alive = true;

	// Logs
	std::vector<SeatCall> seat_calls;
	std::vector<ReadCall> reads;
	std::vector<KeepCall> keep_calls;
	int32_t shadows = 0;
	int32_t keeps = 0;
	int32_t let_gos = 0;
	int32_t holds = 0;
	int32_t drops = 0;
	int32_t live_reservations = 0;

	[[nodiscard]] SourceFacts facts() const override {
		return SourceFacts{.length_frames = length_,
		                   .channels = kChannels,
		                   .byte_depth = kByteDepth,
		                   .data_start_byte = kDataStart,
		                   .start_marker_frame = start_marker_frame,
		                   .start_byte = start_byte,
		                   .end_or_loop_byte = kDataStart + (length_ * kChannels * kByteDepth),
		                   .loop_ahead = std::nullopt,
		                   .sample_rate = 44100};
	}
	[[nodiscard]] std::optional<int64_t> synced_position() const override { return synced; }
	[[nodiscard]] std::optional<FrameRun> run(int32_t frame, Direction direction) const override {
		if (frame < 0 || frame >= length_) {
			return std::nullopt;
		}
		const auto d = static_cast<int32_t>(direction);
		if (missing_from && (frame - *missing_from) * d >= 0) {
			return std::nullopt;
		}
		const int32_t edge = direction == Direction::FORWARD ? length_ - frame : frame + 1;
		const int32_t to_missing = missing_from ? (*missing_from - frame) * d : edge;
		const int32_t frame_bytes = kChannels * kByteDepth;
		return FrameRun{.first = bytes_.data() + 4 + static_cast<ptrdiff_t>(frame * frame_bytes) + kByteDepth - 4,
		                .frames = to_missing < edge ? to_missing : edge,
		                .stride = frame_bytes * d,
		                .channels = kChannels,
		                .byte_depth = kByteDepth};
	}
	[[nodiscard]] std::optional<deluge::dsp::fixed::timestretch::PercWindow> perc_at(int32_t /*pixel*/,
	                                                                                 Direction /*d*/) override {
		return std::nullopt;
	}
	PercFill fill_perc(int32_t /*from*/, int32_t to, Direction /*d*/, int32_t /*max_frames*/) override {
		PercFill fill;
		fill.filled_end = to;
		return fill;
	}
	void shadow() override {
		shadows++;
		older_alive = newer_alive;
	}
	SeatResult seat_and_swap(int32_t frame, UQ<24> phase, LoopType loop) override {
		seat_calls.push_back({.frame = frame, .phase = phase.raw(), .loop = loop});
		SeatResult result = SeatResult::STARTED;
		if (!seats.empty()) {
			result = seats.front();
			seats.pop_front();
		}
		if (result != SeatResult::NOT_READY) {
			older_alive = newer_alive;
			newer_alive = result == SeatResult::STARTED;
		}
		return result;
	}
	bool read(Head head, std::span<Q<31>> /*out*/, int32_t frames, HeadGain gain) override {
		reads.push_back({.head = head, .frames = frames, .gain = gain});
		return head == Head::NEWER ? newer_alive : older_alive;
	}
	[[nodiscard]] HeadMark mark(Head /*head*/) const override {
		marks_++;
		return HeadMark{.play_byte = mark_byte, .phase = UQ<24>::from_raw(0)};
	}
	[[nodiscard]] int32_t mark_count() const { return marks_; }
	[[nodiscard]] bool cursors_meet() const override { return meet; }
	void keep(Reservation& reservation, int32_t frame, Direction /*d*/) override {
		keeps++;
		keep_calls.push_back({.reservation = &reservation, .frame = frame});
		if (reservation.held == nullptr) {
			reservation.held = &token_;
			live_reservations++;
		}
	}
	void let_go(Reservation& reservation) override {
		if (reservation.held != nullptr) {
			reservation.held = nullptr;
			live_reservations--;
			let_gos++;
		}
	}
	void hold(PercChunk /*chunk*/) override { holds++; }
	void drop(PercChunk /*chunk*/) override { drops++; }

private:
	int32_t length_;
	std::vector<std::byte> bytes_;
	mutable int32_t marks_ = 0;
	int token_ = 0;
};

} // namespace fake_stretch_port
