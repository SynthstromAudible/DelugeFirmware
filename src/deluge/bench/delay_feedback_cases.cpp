/// @file
/// @brief The delay feedback cases' setup and blocks, built with the DSP library's flags.
/// @details The delay ships from deluge_dsp_fixed, which builds in ARM mode with -finline-functions and no LTO, where
/// the app builds in Thumb mode with -fno-inline-functions. Under the app's flags the impulse response's
/// `std::array` indexing is an out-of-line call per tap, so this file takes the library's flags
/// (src/deluge/CMakeLists.txt) and the cases time the delay's feedback as the delay runs it.

#include "bench/cases.h"
#include "dsp-next/fixed/shaping/tanh.h"
#include "util/fixedpoint.h"

#include <cstddef>

namespace deluge::bench {

template <bool Analog>
void DelayFeedbackPassesCase<Analog>::setup(std::size_t n) {
	spans_ = stereo_frame_spans(n);
	shaped_ = spans_.out;
	fill_delay_input();
	shaper_.configure(delay_feedback_settings(Analog));
	shaper_.reset();
	high_pass_.configure({});
	high_pass_.reset();
}

template <bool Analog>
void DelayFeedbackPassesCase<Analog>::block(std::size_t /*n*/) {
	shaper_.renderBlock(spans_.in, spans_.out);
	high_pass_.renderBlock(shaped_, spans_.out);
}

template class DelayFeedbackPassesCase<true>;
template class DelayFeedbackPassesCase<false>;

template <bool Analog>
void DelayFeedbackChainCase<Analog>::setup(std::size_t n) {
	spans_ = stereo_frame_spans(n);
	fill_delay_input();
	chain_.configure(DelayFeedbackChain::Settings{delay_feedback_settings(Analog), DelayHighPass::Settings{}});
	chain_.reset();
}

template <bool Analog>
void DelayFeedbackChainCase<Analog>::block(std::size_t /*n*/) {
	chain_.renderBlock(spans_.in, spans_.out);
}

template class DelayFeedbackChainCase<true>;
template class DelayFeedbackChainCase<false>;

void DelayFeedbackLegacyAnalogCase::setup(std::size_t n) {
	const FrameSpans<StereoSample> spans = stereo_frame_spans(n);
	in_ = spans.in;
	work_ = spans.out;
	fill_delay_input();
	response_ = {};
	high_pass_l_ = 0;
	high_pass_r_ = 0;
	feedback_ = kDelayFeedback;
	drive_ = kDelayDrive;
}

void DelayFeedbackLegacyAnalogCase::block(std::size_t /*n*/) {
	for (std::size_t i = 0; i < work_.size(); ++i) {
		work_[i] = response_.process(in_[i]);
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
