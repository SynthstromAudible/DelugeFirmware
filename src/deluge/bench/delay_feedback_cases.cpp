/// @file
/// @brief The delay feedback cases' setup and blocks, built with the DSP library's flags.
/// @details The delay ships from deluge_dsp_fixed, which builds in ARM mode with -finline-functions and no LTO, where
/// the app builds in Thumb mode at an inline threshold of 5. The cases time the impulse response as the library builds
/// it, so this file takes the library's flags
/// (src/deluge/CMakeLists.txt) and the cases time the delay's feedback as the delay runs it: in place.

#include "bench/cases.h"
#include "dsp-next/core/q31_ops.h"
#include "dsp-next/fixed/shaping/tanh.h"

#include <algorithm>
#include <cstddef>

namespace deluge::bench {

namespace {

/// @brief Fills the lanes input, copies its first @p n frames into the lanes output and returns those output frames:
/// the block an in-place case renders first.
dsp::Buffer<StereoSample> in_place_work(std::size_t n) {
	const FrameSpans<StereoSample> spans = stereo_frame_spans(n);
	fill_delay_input();
	std::ranges::copy(spans.in, spans.out.begin());
	return spans.out;
}

} // namespace

template <bool Analog>
void DelayFeedbackPassesCase<Analog>::setup(std::size_t n) {
	work_ = in_place_work(n);
	shaper_.configure(delay_feedback_settings(Analog));
	shaper_.reset();
	high_pass_.configure({});
	high_pass_.reset();
}

template <bool Analog>
void DelayFeedbackPassesCase<Analog>::block(std::size_t /*n*/) {
	shaper_.render_block(work_, work_);
	high_pass_.render_block(work_, work_);
}

template class DelayFeedbackPassesCase<true>;
template class DelayFeedbackPassesCase<false>;

template <bool Analog, Placement P>
void DelayFeedbackChainCase<Analog, P>::setup(std::size_t n) {
	if constexpr (P == Placement::IN_PLACE) {
		work_ = in_place_work(n);
	}
	else {
		spans_ = stereo_frame_spans(n);
		fill_delay_input();
	}
	chain_.configure(DelayFeedbackChain::Settings{delay_feedback_settings(Analog), DelayHighPass::Settings{}});
	chain_.reset();
}

template <bool Analog, Placement P>
void DelayFeedbackChainCase<Analog, P>::block(std::size_t /*n*/) {
	if constexpr (P == Placement::IN_PLACE) {
		chain_.render_block(work_, work_);
	}
	else {
		chain_.render_block(spans_.in, spans_.out);
	}
}

template class DelayFeedbackChainCase<true>;
template class DelayFeedbackChainCase<false>;
template class DelayFeedbackChainCase<false, Placement::OUT_OF_PLACE>;

void DelayFeedbackLegacyAnalogCase::setup(std::size_t n) {
	work_ = in_place_work(n);
	response_ = {};
	high_pass_l_ = 0;
	high_pass_r_ = 0;
	feedback_ = kDelayFeedback;
	drive_ = kDelayDrive;
}

// The legacy feedback's arithmetic, raw on purpose: what the bench compares the kernel against.
void DelayFeedbackLegacyAnalogCase::block(std::size_t /*n*/) {
	for (StereoSample& sample : work_) {
		sample = response_.process(sample);
	}
	// Less headroom than digital: the analog saturation sounds fine driven.
	// The legacy chain's raw arithmetic on the Q31 words.
	for (StereoSample& sample : work_) {
		sample.l = dsp::Q<31>::from_raw(
		    dsp::fixed::tanh(dsp::Q<31>::from_raw(multiply_32x32_rshift32(sample.l.raw(), feedback_)), drive_).raw()
		    << 2);
		sample.r = dsp::Q<31>::from_raw(
		    dsp::fixed::tanh(dsp::Q<31>::from_raw(multiply_32x32_rshift32(sample.r.raw(), feedback_)), drive_).raw()
		    << 2);
	}
	for (StereoSample& sample : work_) {
		int32_t l = sample.l.raw();
		int32_t r = sample.r.raw();
		high_pass_l_ += (l - high_pass_l_) >> 11;
		l -= high_pass_l_;
		high_pass_r_ += (r - high_pass_r_) >> 11;
		r -= high_pass_r_;
		sample = {.l = dsp::Q<31>::from_raw(l), .r = dsp::Q<31>::from_raw(r)};
	}
}

} // namespace deluge::bench
