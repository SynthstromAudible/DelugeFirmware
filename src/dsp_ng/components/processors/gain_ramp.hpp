#pragma once
#include "dsp_ng/core/processor.hpp"
#include <argon.hpp>

namespace deluge::dsp::processors {
template <typename T, typename GainType>
struct GainRamp : BlockProcessor<T> {
	GainRamp(GainType start, GainType end) : start_{start}, end_{end} {}

	void renderBlock(Signal<T> in, Buffer<T> out) final {
		GainType single_step = (end_ - start_) / (in.size() - 1);
		GainType current = start_;
		for (size_t i = 0; i < in.size(); ++i) {
			out[i] = in[i] * current; // VCA: [signal] * [amplitude]
			current += single_step;
		}
	}

private:
	GainType start_;
	GainType end_;
};
} // namespace deluge::dsp::processors
