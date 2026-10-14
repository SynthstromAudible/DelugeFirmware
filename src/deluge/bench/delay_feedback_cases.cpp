/// @file
/// @brief The delay feedback cases' setup and blocks, built with the DSP library's flags.
/// @details The delay ships from deluge_dsp_fixed, which builds in ARM mode with -finline-functions and no LTO, where
/// the app builds in Thumb mode with -fno-inline-functions. Under the app's flags the impulse response's
/// `std::array` indexing is an out-of-line call per tap, so this file takes the library's flags
/// (src/deluge/CMakeLists.txt) and the cases time the delay's feedback as the delay runs it: in place.

#include "bench/cases.h"
#include "dsp-next/fixed/shaping/tanh.h"
#include "util/fixedpoint.h"

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
	shaper_.renderBlock(work_, work_);
	high_pass_.renderBlock(work_, work_);
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
		chain_.renderBlock(work_, work_);
	}
	else {
		chain_.renderBlock(spans_.in, spans_.out);
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

void DelayFeedbackLegacyAnalogCase::block(std::size_t /*n*/) {
	for (StereoSample& sample : work_) {
		sample = response_.process(sample);
	}
	// Less headroom than digital: the analog saturation sounds fine driven.
	for (StereoSample& sample : work_) {
		sample.l = dsp::fixed::tanh(multiply_32x32_rshift32(sample.l, feedback_), drive_) << 2;
		sample.r = dsp::fixed::tanh(multiply_32x32_rshift32(sample.r, feedback_), drive_) << 2;
	}
	for (StereoSample& sample : work_) {
		high_pass_l_ += (sample.l - high_pass_l_) >> 11;
		sample.l -= high_pass_l_;

		high_pass_r_ += (sample.r - high_pass_r_) >> 11;
		sample.r -= high_pass_r_;
	}
}

} // namespace deluge::bench
