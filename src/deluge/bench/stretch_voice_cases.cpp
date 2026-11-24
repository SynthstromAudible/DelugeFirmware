/// @file
/// @brief The whole stretched sample voice case: `SampleStretcher` driving both read heads through its port over a
/// resident region, as a sample voice renders minus the app.

#include "bench/cases.h"
#include "board_config.h" // PLACE_SDRAM_BSS
#include "definitions_cxx.hpp"
#include "dsp/core/fixed_point.h"
#include "dsp/fixed/interpolation/windowed_sinc_kernel.h"
#include "dsp/fixed/noise/cong.h"
#include "dsp/fixed/sample_read/read_head.h"
#include "synth/sources/sample/sample_stretch_port.h"
#include "synth/sources/sample/sample_stretcher.h"
#include "synth/sources/sample/stretch_context.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <tuple>
#include <variant>

namespace deluge::bench {

namespace {

namespace sr = dsp::fixed::sample_read;
using dsp::Q;
using dsp::UQ;
using sr::Direction;
using sr::ReadHead;
using synth::SampleStretchPort;

/// Frames of the region every chunk aliases.
constexpr int32_t kChunkFrames = 4096;
/// Frames of the sample the stretcher sees: chunks of the region, far more than a run renders.
constexpr int32_t kSampleFrames = 1 << 26;
constexpr int32_t kChannels = 2;
constexpr int32_t kByteDepth = 2;
constexpr int32_t kFrameBytes = kChannels * kByteDepth;
/// The sample's data start, a WAV's.
constexpr int32_t kDataStart = 44;
/// Bytes before the region's first sample, which a 32-bit read ending at a 16-bit sample's top byte reaches.
constexpr int32_t kLead = 4;
/// Where the voice is when stretching begins.
constexpr int32_t kStartFrame = 1000;

/// The region: 16-bit stereo, a saw per channel, in SDRAM as a sample's chunks are.
alignas(16) PLACE_SDRAM_BSS std::array<uint8_t, kLead + (kChunkFrames * kFrameBytes) + 8> voice_region;

/// A head's read source: one chunk of the aliased sample at a time, as the app's lease holds one.
class AliasSource {
public:
	[[nodiscard]] std::optional<sr::FrameRun> run(int32_t frame, Direction direction) const {
		if (!chunk_ || frame / kChunkFrames != *chunk_) {
			return std::nullopt;
		}
		const int32_t in_chunk = frame % kChunkFrames;
		const bool forward = direction == Direction::FORWARD;
		return sr::FrameRun{.first = reinterpret_cast<const std::byte*>(voice_region.data()) + kLead
		                             + (in_chunk * kFrameBytes) + kByteDepth - 4,
		                    .frames = forward ? kChunkFrames - in_chunk : in_chunk + 1,
		                    .stride = forward ? kFrameBytes : -kFrameBytes,
		                    .channels = kChannels,
		                    .byte_depth = kByteDepth};
	}
	[[nodiscard]] int32_t channels() const { return kChannels; }
	[[nodiscard]] int32_t length_frames() const { return kSampleFrames; }
	[[nodiscard]] int32_t byte_depth() const { return kByteDepth; }
	sr::Residency move_to(int32_t frame, Direction /*direction*/) {
		chunk_ = frame / kChunkFrames;
		return sr::Residency::READY;
	}
	void let_go() { chunk_.reset(); }
	[[nodiscard]] std::optional<sr::ChunkSpan> span(Direction direction) const {
		if (!chunk_) {
			return std::nullopt;
		}
		const int32_t first = *chunk_ * kChunkFrames;
		const int32_t last = first + kChunkFrames - 1;
		const bool final = last == kSampleFrames - 1;
		if (direction == Direction::FORWARD) {
			return sr::ChunkSpan{.boundary = last + 1, .floor = first, .final = final};
		}
		return sr::ChunkSpan{.boundary = first - 1, .floor = last, .final = first == 0};
	}

private:
	std::optional<int32_t> chunk_;
};
static_assert(sr::read_source<AliasSource>);

/// The stretcher's port over the aliased sample and two read heads, as the app's port over a voice-sample.
class BenchStretchPort final : public SampleStretchPort {
public:
	explicit BenchStretchPort(uint32_t increment) : increment_(increment) {}

	/// Seats the newer head at @p frame, as the voice's unstretched read leaves it before stretching begins.
	void seat_newer(int32_t frame) {
		newer_ = 0;
		std::ignore = ReadHead::seat(heads_[0], sources_[0], frame, UQ<24>{}, Direction::FORWARD);
	}

	[[nodiscard]] synth::SourceFacts facts() const override {
		return synth::SourceFacts{.length_frames = kSampleFrames,
		                          .channels = kChannels,
		                          .byte_depth = kByteDepth,
		                          .data_start_byte = kDataStart,
		                          .start_marker_frame = 0,
		                          .start_byte = kDataStart,
		                          .end_or_loop_byte = kDataStart + (kSampleFrames * kFrameBytes),
		                          .loop_ahead = std::nullopt,
		                          .sample_rate = 44100};
	}
	[[nodiscard]] std::optional<int64_t> synced_position() const override { return std::nullopt; }
	[[nodiscard]] std::optional<sr::FrameRun> run(int32_t frame, Direction direction) const override {
		if (frame < 0 || frame >= kSampleFrames) {
			return std::nullopt;
		}
		AliasSource source;
		source.move_to(frame, direction);
		return source.run(frame, direction);
	}
	[[nodiscard]] std::optional<dsp::fixed::timestretch::PercWindow> perc_at(int32_t /*pixel*/,
	                                                                         Direction /*direction*/) override {
		return std::nullopt;
	}
	synth::PercFill fill_perc(int32_t /*from*/, int32_t to, Direction /*direction*/, int32_t /*max_frames*/) override {
		synth::PercFill fill;
		fill.filled_end = to;
		return fill;
	}
	void shadow() override {
		const int spare = 1 - newer_;
		heads_[spare] = ReadHead::level_copy(heads_[newer_]);
		heads_[spare].width_last = 0;
		sources_[spare] = sources_[newer_];
	}
	synth::SeatResult seat_and_swap(int32_t frame, UQ<24> phase, LoopType /*loop*/) override {
		const int spare = 1 - newer_;
		if (ReadHead::seat(heads_[spare], sources_[spare], frame, phase, Direction::FORWARD) != sr::Residency::READY
		    || !ReadHead::walk(heads_[spare], sources_[spare], {}, Direction::FORWARD)) {
			return synth::SeatResult::NOT_READY;
		}
		newer_ = spare;
		return synth::SeatResult::STARTED;
	}
	bool read(Head head, std::span<Q<31>> out, int32_t frames, synth::HeadGain gain) override {
		const int which = slot(head);
		ReadHead::Output output{.out = out, .out_channels = kChannels, .amplitude = gain.level, .step = gain.step};
		// The port holds the case's rate as its 8.24 word, as the app's port holds the voice's
		return ReadHead::read(heads_[which], sources_[which], {}, output, frames, sr::Increment::from_raw(increment_),
		                      kInterpolationMaxNumSamples,
		                      dsp::fixed::WindowedSincKernel::variant_for(static_cast<int32_t>(increment_)), nullptr,
		                      Direction::FORWARD);
	}
	[[nodiscard]] synth::HeadMark mark(Head head) const override {
		const ReadHead::State& state = heads_[slot(head)];
		const sr::ReadFacts facts{.data_start_byte = kDataStart,
		                          .end_byte = kDataStart + (kSampleFrames * kFrameBytes),
		                          .channels = kChannels,
		                          .byte_depth = kByteDepth};
		return synth::HeadMark{.play_byte = ReadHead::play_byte(state, facts, Direction::FORWARD, true),
		                       .phase = state.phase};
	}
	[[nodiscard]] bool cursors_meet() const override {
		const auto& newer = heads_[newer_].cursor;
		const auto& older = heads_[1 - newer_].cursor;
		if (newer.index() != older.index()) {
			return false;
		}
		if (const auto* reading = std::get_if<ReadHead::Reading>(&newer)) {
			return reading->frame == std::get<ReadHead::Reading>(older).frame;
		}
		return std::get<ReadHead::Ended>(newer).zero_taps == std::get<ReadHead::Ended>(older).zero_taps;
	}
	void keep(synth::Reservation& reservation, int32_t /*frame*/, Direction /*direction*/) override {
		reservation.held = &token_;
	}
	void let_go(synth::Reservation& reservation) override { reservation.held = nullptr; }
	void hold(synth::PercChunk /*chunk*/) override {}
	void drop(synth::PercChunk /*chunk*/) override {}

private:
	[[nodiscard]] int slot(Head head) const { return head == Head::NEWER ? newer_ : 1 - newer_; }

	uint32_t increment_;
	std::array<ReadHead::State, 2> heads_{};
	std::array<AliasSource, 2> sources_{};
	int newer_ = 0;
	int token_ = 0;
};

/// Frames rendered untimed at setup, so the hops are in their steady pattern: a second.
constexpr int32_t kVoiceWarmUpFrames = 44'100;

/// The rig: the port, the stretcher, the context the engine would own, and the output.
struct VoiceRig {
	explicit VoiceRig(uint32_t increment) : port(increment) {}
	BenchStretchPort port;
	synth::SampleStretcher stretcher{};
	synth::HopBudget budget{};
	dsp::fixed::Cong noise{0x1234'5678};
	synth::TailBuffers tails{};
	synth::StretchContext context{.budget = &budget, .noise = &noise, .tails = &tails};
	std::array<Q<31>, kMaxBlock * 2> out{};
};

/// The rig's storage, in SDRAM: the tail buffers make it about 16 KB.
alignas(8) PLACE_SDRAM_BSS std::array<uint8_t, sizeof(VoiceRig)> voice_rig_storage;
VoiceRig* voice_rig = nullptr;

synth::SampleStretcher::Motion voice_motion(const StretchVoiceConfig& config) {
	// The case's configuration holds its ratio and rate as 8.24 words, as the reader cases hold theirs
	return {.ratio = synth::Ratio::from_raw(config.ratio),
	        .increment = synth::Increment::from_raw(config.pitch_increment),
	        .direction = Direction::FORWARD};
}

synth::SampleStretcher::Part voice_part(const StretchVoiceConfig& config, int32_t frames) {
	return {.frames = frames,
	        .motion = voice_motion(config),
	        .loop = LoopType::NONE,
	        .writing_cache = false,
	        .cache_in_use = false,
	        .out_channels = kChannels};
}

/// Renders one block of @p n frames: the budget afresh, the block's perc fill, then one part.
void voice_block(VoiceRig& rig, const StretchVoiceConfig& config, std::size_t n) {
	const auto frames = static_cast<int32_t>(n);
	rig.budget.new_routine();
	rig.stretcher.prepare(rig.port, rig.context, voice_motion(config), frames, false);
	// A steady note's amplitude word, as the stretcher specs give it
	const synth::StretchAmplitude amplitude{.amplitude = Q<31>::from_raw(1 << 26), .step = Q<31>{}};
	std::ignore = rig.stretcher.render(rig.port, rig.context, voice_part(config, frames),
	                                   std::span<Q<31>>(rig.out.data(), n * kChannels), amplitude);
}

} // namespace

void StretchVoiceCase::setup(std::size_t /*n*/) {
	for (int32_t f = 0; f < kChunkFrames; ++f) {
		for (int32_t c = 0; c < kChannels; ++c) {
			const auto v = static_cast<uint16_t>(f * (97 + (c * 31)) * 16);
			const int32_t at = kLead + (((f * kChannels) + c) * kByteDepth);
			voice_region[at] = static_cast<uint8_t>(v);
			voice_region[at + 1] = static_cast<uint8_t>(v >> 8);
		}
	}
	if (voice_rig != nullptr) {
		std::destroy_at(voice_rig);
	}
	voice_rig = new (voice_rig_storage.data()) VoiceRig(config_.pitch_increment); // NOLINT
	voice_rig->port.seat_newer(kStartFrame);
	std::ignore = voice_rig->stretcher.begin(voice_rig->port, voice_rig->context,
	                                         synth::SourcePosition(int64_t{kStartFrame} << 24), voice_motion(config_),
	                                         LoopType::NONE, synth::SampleStretcher::Plain{});
	for (int32_t done = 0; done < kVoiceWarmUpFrames; done += kMaxBlock) {
		voice_block(*voice_rig, config_, kMaxBlock);
	}
}

void StretchVoiceCase::block(std::size_t n) {
	voice_block(*voice_rig, config_, n);
}

} // namespace deluge::bench
