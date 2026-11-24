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

#include "synth/sources/sample/sample_stretcher.h"

#include "dsp/fixed/timestretch/hop_planner.h"
#include "dsp/fixed/timestretch/hop_tuning.h"
#include "dsp/fixed/timestretch/waveform_aligner.h"

#include <utility>

namespace deluge::synth {

namespace {
namespace ts = dsp::fixed::timestretch;
using dsp::fixed::sample_read::FrameRun;

/// The port's frames as the aligner reads them.
class PortFrames {
public:
	PortFrames(const SampleStretchPort& port, const SourceFacts& facts)
	    : port_(&port), channels_(facts.channels), length_(facts.length_frames) {}
	[[nodiscard]] std::optional<FrameRun> run(int32_t frame, Direction direction) const {
		return port_->run(frame, direction);
	}
	[[nodiscard]] int32_t channels() const { return channels_; }
	[[nodiscard]] int32_t length_frames() const { return length_; }

private:
	const SampleStretchPort* port_;
	int32_t channels_;
	int32_t length_;
};
static_assert(dsp::fixed::sample_read::frame_source<PortFrames>);

int32_t frame_bytes(const SourceFacts& facts) {
	return facts.channels * facts.byte_depth;
}

/// A byte's frame from the data's start, read unsigned, as the legacy's frame counts are.
int32_t frame_of(const SourceFacts& facts, int32_t byte) {
	return static_cast<int32_t>(static_cast<uint32_t>(byte - facts.data_start_byte)
	                            / static_cast<uint8_t>(frame_bytes(facts)));
}

constexpr ts::CrossfadeRamp kRampDone{};
} // namespace

bool SampleStretcher::newer_reading() const {
	return std::holds_alternative<Shadowed>(phase_) || std::holds_alternative<Solo>(phase_)
	       || std::holds_alternative<Crossfading>(phase_);
}

bool SampleStretcher::sounding() const {
	return !std::holds_alternative<Silent>(phase_);
}

SampleStretcher::Heads SampleStretcher::heads() const {
	if (const auto* c = std::get_if<Crossfading>(&phase_)) {
		return {.newer = true, .older = true, .ramp = c->ramp, .older_head = c->older, .tail = c->tail};
	}
	if (const auto* f = std::get_if<Fading>(&phase_)) {
		return {.newer = false, .older = true, .ramp = f->ramp, .older_head = f->older, .tail = f->tail};
	}
	const bool shadowed = std::holds_alternative<Shadowed>(phase_);
	return {.newer = shadowed || std::holds_alternative<Solo>(phase_),
	        .older = shadowed,
	        .ramp = kRampDone,
	        .older_head = SpareHead{},
	        .tail = std::nullopt};
}

void SampleStretcher::set_heads(Heads h, StretchContext& context) {
	if (!h.older && h.tail) {
		context.tails->give_back(*h.tail);
		h.tail.reset();
	}
	if (h.newer && h.older) {
		phase_ = Crossfading{.ramp = h.ramp, .older = h.older_head, .tail = h.tail};
	}
	else if (h.newer) {
		phase_ = Solo{};
	}
	else if (h.older) {
		phase_ = Fading{.ramp = h.ramp, .older = h.older_head, .tail = h.tail};
	}
	else {
		phase_ = Silent{};
	}
}

void SampleStretcher::give_back_tail(StretchContext& context) {
	auto give = [&](std::optional<TailLoan>& tail) {
		if (tail) {
			context.tails->give_back(*tail);
			tail.reset();
		}
	};
	if (auto* c = std::get_if<Crossfading>(&phase_)) {
		give(c->tail);
	}
	else if (auto* f = std::get_if<Fading>(&phase_)) {
		give(f->tail);
	}
}

void SampleStretcher::keep_hop_back(SampleStretchPort& port, const SourceFacts& facts, int64_t position_frames,
                                    const Motion& motion) {
	if (facts.length_frames < 1) {
		return;
	}
	port.keep(leases_.hop_back,
	          hop_back_frame(position_frames, motion.ratio, motion.increment, ts::hop_shape(motion.ratio),
	                         facts.sample_rate, motion.direction, facts.length_frames),
	          motion.direction);
}

bool SampleStretcher::begin(SampleStretchPort& port, StretchContext& context, SourcePosition position, Motion motion,
                            LoopType loop, Opening opening) {
	position_ = position;
	perc_fill_ = PercFillMemo{};
	leases_.perc = {};
	begun_this_block_ = true;
	const SourceFacts facts = port.facts();

	// A fudge with no frames to the loop begins plain, as the legacy's init did
	if (const auto* fudge = std::get_if<Fudge>(&opening); fudge != nullptr && fudge->output_frames_to_loop != 0) {
		fudge_ = true;
		const int32_t into = fudge_into(fudge->output_frames_to_loop, motion.increment);
		const auto d = static_cast<int32_t>(motion.direction);
		const int32_t start = frame_of(facts, facts.start_byte) - (into * d);
		const int32_t first = motion.direction == Direction::FORWARD ? 0 : facts.length_frames - 1;
		if ((start - first) * d < 0) {
			phase_ = Silent{};
			return false;
		}
		const SeatResult seat = port.seat_and_swap(start, dsp::UQ<24>{}, loop);
		if (seat == SeatResult::NOT_READY) {
			phase_ = Silent{};
			return false;
		}
		clock_ = HopClock::never();
		const ts::CrossfadeRamp ramp = ts::CrossfadeRamp::over_fudge(into);
		if (seat == SeatResult::STARTED) {
			phase_ = Crossfading{.ramp = ramp, .older = SpareHead{}, .tail = std::nullopt};
		}
		else {
			phase_ = Fading{.ramp = ramp, .older = SpareHead{}, .tail = std::nullopt};
		}
		return true;
	}

	fudge_ = false;
	port.shadow();
	clock_ = HopClock::first_hop(context.noise->next());
	phase_ = Shadowed{};
	keep_hop_back(port, facts, position_.frame(motion.direction), motion);

	if (const auto* cached = std::get_if<FromCacheTail>(&opening)) {
		const int32_t frames = ts::cache_tail_frames(cached->window, clock_.to_hop(), cached->channels);
		if (frames > 0) {
			// With every buffer lent there is no crossfade (decision 2): the begin stays plain
			if (const std::optional<TailLoan> loan = context.tails->lend()) {
				phase_ = Crossfading{
				    .ramp = ts::CrossfadeRamp::over_cache_tail(frames),
				    .older = ts::CacheTail(loan->words, cached->window.frame_origin, frames, cached->channels),
				    .tail = loan};
			}
		}
	}
	return true;
}

void SampleStretcher::remember(SampleStretchPort& port, PercChunk chunk) {
	if (leases_.perc[0] == chunk || leases_.perc[1] == chunk) {
		return;
	}
	port.hold(chunk);
	if (const std::optional<PercChunk> oldest = leases_.perc[0]; oldest.has_value()) {
		port.drop(*oldest);
	}
	leases_.perc[0] = leases_.perc[1];
	leases_.perc[1] = chunk;
}

void SampleStretcher::prepare(SampleStretchPort& port, StretchContext& /*context*/, const Motion& motion,
                              int32_t block_frames, bool writing_cache) {
	const int32_t at = position_.frame(motion.direction);
	const auto d = static_cast<int8_t>(motion.direction);
	// The memo takes the voice's words: perc_fill.h names no DSP type
	const std::optional<FillRange> range =
	    perc_fill_.wanted(at, motion.increment.raw(), motion.ratio.raw(), d, block_frames, writing_cache);
	if (!range) {
		return;
	}
	const PercFill fill = port.fill_perc(range->from, range->to, motion.direction, range->max_frames);
	for (uint8_t i = 0; i < fill.touched_count; i++) {
		remember(port, fill.touched[i]);
	}
	if (fill.lookahead == PercFill::Lookahead::LET_GO) {
		port.let_go(leases_.lookahead);
	}
	else if (fill.lookahead == PercFill::Lookahead::MOVE_TO) {
		const SourceFacts facts = port.facts();
		port.keep(leases_.lookahead, frame_of(facts, fill.lookahead_byte), motion.direction);
	}
	perc_fill_.filled(at, fill, d);
}

void SampleStretcher::hop(SampleStretchPort& port, StretchContext& context, const Part& part) {
	const Motion& m = part.motion;
	// The older head takes the newer's activity before the new head starts
	const bool older_reads = newer_reading();
	const HeadMark old = port.mark(SampleStretchPort::Head::NEWER);
	const SourceFacts facts = port.facts();
	const int32_t bytes = frame_bytes(facts);

	if (const std::optional<int64_t> frames_in = port.synced_position()) {
		position_ = SourcePosition::synced(facts.data_start_byte + (facts.start_marker_frame * bytes),
		                                   facts.data_start_byte, bytes, *frames_in, m.direction);
	}
	const int64_t position_frames = position_.frame(m.direction);
	const ts::HopShape shape = ts::jittered(ts::hop_shape(m.ratio), *context.noise);

	ts::HopRequest request{.position_frames = position_frames,
	                       .ratio = m.ratio,
	                       .increment = m.increment,
	                       .combined = combined_increment(m.ratio, m.increment),
	                       .direction = m.direction,
	                       .length_frames = facts.length_frames,
	                       .shape = shape,
	                       .loop = std::nullopt};
	if (part.loop == LoopType::TIMESTRETCHER_LEVEL_IF_ACTIVE && facts.loop_ahead) {
		request.loop = facts.loop_ahead;
		request.loop->cache_in_use = part.cache_in_use;
	}
	bool loop_crossfade = false;
	if (request.loop) {
		loop_crossfade = ts::loop_crossfade(*request.loop, request.position_frames, request.combined, request.ratio,
		                                    request.increment, shape.min_width, m.direction, facts.length_frames)
		                     .kind
		                 == ts::LoopPlan::Kind::CROSSFADE;
	}
	std::optional<ts::PercWindow> perc;
	if (!loop_crossfade) {
		perc = port.perc_at(static_cast<int32_t>(position_frames) >> 7, m.direction);
	}
	const ts::HopPlan plan = ts::plan_hop(request, perc ? &*perc : nullptr);
	clock_ = HopClock::after(plan.hop_frames);

	// The previous older head, a cache tail included, is dropped: the previous newer becomes the older
	give_back_tail(context);

	if (plan.kind == ts::HopPlan::Kind::PAST_END) {
		set_heads({.newer = false, .older = older_reads, .ramp = plan.ramp, .older_head = NewerSlot{}, .tail = {}},
		          context);
		return;
	}

	auto resident = [&](int32_t frame) { return port.run(frame, m.direction).has_value(); };
	int32_t start = plan.start_frame;
	if (plan.kind == ts::HopPlan::Kind::HEAD) {
		start = ts::clamp_start(start, facts.start_marker_frame, m.direction, resident(start));
	}
	dsp::UQ<24> phase{};
	if (plan.align && older_reads && old.play_byte >= facts.data_start_byte) {
		const PortFrames frames(port, facts);
		const ts::AlignSetup setup = ts::align_setup(m.increment, plan.crossfade_frames);
		const std::optional<ts::Averages> old_totals = ts::moving_averages(
		    frames, frame_of(facts, old.play_byte), setup.crossfade_source_frames, m.direction, setup.each);
		if (old_totals) {
			const std::optional<ts::Alignment> aligned = ts::align(
			    frames, ts::AlignRequest{.start_frame = start,
			                             .direction = m.direction,
			                             .increment = m.increment,
			                             .setup = setup,
			                             .limit = ts::search_limit(plan.hop_frames, m.increment, facts.sample_rate),
			                             .old_totals = *old_totals,
			                             .older_phase = old.phase});
			if (aligned) {
				start = ts::aligned_start(start, *aligned, m.direction, facts.length_frames);
				start = ts::clamp_start(start, facts.start_marker_frame, m.direction, resident(start));
				phase = aligned->phase;
			}
		}
	}

	const SeatResult seat = port.seat_and_swap(start, phase, part.loop);
	if (seat == SeatResult::NOT_READY) {
		// S4: the newer head reads on, the spare stops, and the hop comes again in 500 frames
		clock_ = HopClock::retry();
		set_heads({.newer = older_reads,
		           .older = false,
		           .ramp = {.progress = 0, .increment = 0},
		           .older_head = SpareHead{},
		           .tail = {}},
		          context);
		if (!resident(start)) {
			port.keep(leases_.hop_retry, start, m.direction);
		}
		else {
			port.let_go(leases_.hop_retry);
		}
	}
	else {
		port.let_go(leases_.hop_retry);
		set_heads({.newer = seat == SeatResult::STARTED,
		           .older = older_reads,
		           .ramp = plan.ramp,
		           .older_head = SpareHead{},
		           .tail = {}},
		          context);
	}
	keep_hop_back(port, facts, position_frames, m);
}

void SampleStretcher::read_heads(SampleStretchPort& port, StretchContext& context, const Part& part,
                                 std::span<dsp::Q<31>> segment, std::span<dsp::Q<31>> to_part_end, int32_t frames,
                                 HeadLevel level) {
	Heads h = heads();
	const bool older_audible = h.older && !h.ramp.done();
	HeadGain newer_gain{.level = level.level, .step = level.step};
	HeadGain older_gain{.level = dsp::Q<31>{}, .step = dsp::Q<31>{}};
	if (older_audible) {
		const ts::HeadGains gains = ts::segment_gains(h.ramp, frames, level.level, level.step);
		newer_gain = {.level = gains.newer, .step = gains.newer_step};
		older_gain = {.level = gains.older, .step = gains.older_step};
	}
	if (h.newer) {
		h.newer = port.read(SampleStretchPort::Head::NEWER, segment, frames, newer_gain);
	}
	if (h.older) {
		if (auto* tail = std::get_if<ts::CacheTail>(&h.older_head); tail != nullptr && older_audible) {
			// A stereo tail into a mono output runs on past the segment, as the legacy's did (C4's quirk)
			tail->mix_into(to_part_end, frames, part.out_channels, older_gain.level, older_gain.step);
		}
		else {
			// The older head's read; a plain begin's shadow copy reads here once at level 0 (X3)
			const SampleStretchPort::Head where = std::holds_alternative<NewerSlot>(h.older_head)
			                                          ? SampleStretchPort::Head::NEWER
			                                          : SampleStretchPort::Head::OLDER;
			h.older = port.read(where, segment, frames, older_gain);
		}
		if (h.ramp.done()) {
			h.older = false;
		}
	}
	set_heads(h, context);
}

SampleStretcher::PartOutcome SampleStretcher::render(SampleStretchPort& port, StretchContext& context, const Part& part,
                                                     std::span<dsp::Q<31>> out, StretchAmplitude amplitude) {
	begun_this_block_ = false;
	const uint64_t combined = combined_increment(part.motion.ratio, part.motion.increment);
	int32_t rest = part.frames;
	size_t done = 0;
	while (rest != 0) {
		int32_t frames = rest;
		if (newer_reading()) {
			const HopClock::Segment s = clock_.segment(*context.budget, rest);
			if (s.turn == HopClock::Turn::HOP) {
				hop(port, context, part);
				if (!part.writing_cache && part.loop == LoopType::NONE && !sounding()) {
					return {.note_ended = true, .amplitude = amplitude};
				}
				frames = clock_.frames(rest);
			}
			else {
				frames = s.frames;
			}
		}

		const std::span<dsp::Q<31>> segment =
		    out.subspan(done * part.out_channels, static_cast<size_t>(frames) * part.out_channels);
		read_heads(port, context, part, segment, out.subspan(done * part.out_channels), frames,
		           segment_level(amplitude, part.writing_cache));

		position_.advance(combined, frames, part.motion.direction);
		if (!part.writing_cache && part.loop == LoopType::NONE && !sounding()) {
			return {.note_ended = true, .amplitude = amplitude};
		}
		clock_.advance(frames);
		step_amplitude(amplitude, frames, part.writing_cache);
		rest -= frames;
		done += static_cast<size_t>(frames);
	}
	return {.note_ended = false, .amplitude = amplitude};
}

void SampleStretcher::wrap_to(SampleStretchPort& port, StretchContext& context, SourcePosition position,
                              const Part& part) {
	position_ = position;
	if (!newer_reading()) {
		context.budget->spend();
		hop(port, context, part);
	}
}

void SampleStretcher::end(SampleStretchPort& port, StretchContext& context) {
	port.let_go(leases_.lookahead);
	for (std::optional<PercChunk>& chunk : leases_.perc) {
		if (chunk) {
			port.drop(*chunk);
			chunk.reset();
		}
	}
	port.let_go(leases_.hop_back);
	port.let_go(leases_.hop_retry);
	give_back_tail(context);
	phase_ = Silent{};
	fudge_ = false;
	begun_this_block_ = false;
}

bool SampleStretcher::may_end(const SampleStretchPort& port, Ratio ratio, bool synced, int32_t sync_lag) const {
	return synth::may_end(ExitFacts{.begun_this_block = begun_this_block_,
	                                .fudge = fudge_,
	                                .ratio = ratio,
	                                .cursors_meet = port.cursors_meet(),
	                                .synced = synced,
	                                .sync_lag = sync_lag});
}

} // namespace deluge::synth
