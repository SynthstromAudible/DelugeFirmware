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

/// @file
/// @brief The filter kernels as a workload family (filter_workloads.h): each kernel in its wrapper, behind the
/// interface the verbatim references have, so a @ref filter_workloads::FilterRun plays it block by block and
/// @ref filter_workloads::first_parting compares it with the reference.
///
/// A family is mono or stereo, as the workloads it plays: a kernel's mono and stereo forms are different wrappers
/// (`Mono<Svf>` or `Stereo<Svf>`, `Mono<FadeInMono>` or `Mono<FadeIn>`), so each adapter is built for one form and
/// aborts if handed the other. Each digest folds the same values in the same order as its reference's, so a kernel's
/// state digest equals the reference's exactly when the two states agree. The targets without a kernel yet are the
/// references themselves.

#pragma once

#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/filter/fade_in.h"
#include "dsp-next/fixed/filter/filter_modes.h"
#include "dsp-next/fixed/filter/svf.h"
#include "filter_workloads.h"
#include "reference_filters.h"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <span>
#include <type_traits>

namespace kernel_filters {

using deluge::dsp::q31;
using filter_workloads::Digest;

/// @brief The fade in its wrapper for one form: `Mono<FadeIn>` over stereo frames, `Mono<FadeInMono>` over samples.
template <bool kStereo>
using FadeWrapper = std::conditional_t<kStereo, deluge::dsp::Mono<deluge::dsp::fixed::FadeIn>,
                                       deluge::dsp::Mono<deluge::dsp::fixed::FadeInMono>>;

/// @brief Blends @p block (wet) with the dry copy in @p scratch through @p fade, in the block's form.
template <bool kStereo>
void blend(FadeWrapper<kStereo>& fade, std::span<q31> block, std::span<q31> scratch) {
	const std::span<q31> dry = scratch.first(block.size());
	if constexpr (kStereo) {
		const auto frames = deluge::dsp::as_frames(block);
		fade.renderBlock(deluge::dsp::as_frames(dry), frames, frames);
	}
	else {
		fade.renderBlock(dry, block, block);
	}
}

/// @brief Folds a fade's dry and wet levels into @p d, as @ref filters_reference::ReferenceDryWetFade::digest does.
inline void digest_fade(const deluge::dsp::fixed::FadeIn::State& s, Digest& d) {
	d.add(s.dry);
	d.add(s.wet);
}

/// @brief The fade alone, as @ref filters_reference::ReferenceDryWetFade.
template <bool kStereo>
class KernelFade {
	using FadeIn = deluge::dsp::fixed::FadeIn;

public:
	void begin() { fade_.template on<&FadeIn::begin>(); }
	void zero() { fade_.template on<&FadeIn::zero>(); }
	[[nodiscard]] bool active() const { return FadeIn::active(fade_.state()); }
	void keepDry(std::span<const q31> block, std::span<q31> scratch) const {
		std::ranges::copy(block, scratch.begin());
	}
	void blend(std::span<q31> block, std::span<q31> scratch) { kernel_filters::blend<kStereo>(fade_, block, scratch); }
	void digest(Digest& d) const { digest_fade(fade_.state(), d); }

private:
	FadeWrapper<kStereo> fade_{};
};

/// @brief `Stereo<Svf>` or `Mono<Svf>` with its fade, as @ref filters_reference::ReferenceStateVariableFilter.
template <bool kStereo>
class KernelSvf {
	using Svf = deluge::dsp::fixed::Svf;
	using FadeIn = deluge::dsp::fixed::FadeIn;
	using Filter = std::conditional_t<kStereo, deluge::dsp::Stereo<Svf>, deluge::dsp::Mono<Svf>>;

public:
	[[nodiscard]] q31 configure(q31 gain, const deluge::dsp::fixed::FilterConfig& config,
	                            deluge::dsp::fixed::SvfMode mode) {
		filter_.configure({.gain = gain, .config = config, .mode = mode});
		return Svf::gain(filter_.coefficients());
	}

	void renderMono(std::span<q31> block, std::span<q31> scratch) {
		if constexpr (kStereo) {
			std::abort();
		}
		render(block, scratch);
	}

	void renderStereo(std::span<q31> interleaved, std::span<q31> scratch) {
		if constexpr (!kStereo) {
			std::abort();
		}
		render(interleaved, scratch);
	}

	void reset(bool fade) {
		filter_.reset();
		if (fade) {
			fade_.template on<&FadeIn::begin>();
		}
	}

	void zero() {
		filter_.reset();
		fade_.template on<&FadeIn::zero>();
	}

	/// @brief Folds the integrators (left, then right: zero in mono), the coefficients, and the fade into @p d.
	void digest(Digest& d) const {
		Svf::State left{};
		Svf::State right{};
		if constexpr (kStereo) {
			left = filter_.left();
			right = filter_.right();
		}
		else {
			left = filter_.state();
		}
		for (const Svf::State& channel : {left, right}) {
			d.add(channel.low);
			d.add(channel.band);
		}
		const Svf::Coefficients& c = filter_.coefficients();
		d.add(c.fc);
		d.add(c.q);
		d.add(c.in);
		d.add(c.c_low);
		d.add(c.c_band);
		d.add(c.c_high);
		d.add(c.mode == Svf::Mode::BAND);
		digest_fade(fade_.state(), d);
	}

private:
	void render(std::span<q31> block, std::span<q31> scratch) {
		const bool fading = FadeIn::active(fade_.state());
		if (fading) {
			std::ranges::copy(block, scratch.begin());
		}
		if constexpr (kStereo) {
			const auto frames = deluge::dsp::as_frames(block);
			filter_.renderBlock(frames, frames);
		}
		else {
			filter_.renderBlock(block, block);
		}
		if (fading) {
			blend<kStereo>(fade_, block, scratch);
		}
	}

	Filter filter_{};
	FadeWrapper<kStereo> fade_{};
};

/// @brief The kernels as a workload family, in one form: the SVF and the fade as kernels, the rest the references.
template <bool kStereo>
struct KernelFamily {
	using LowPassLadder = filters_reference::ReferenceLowPassLadder;
	using HighPassLadder = filters_reference::ReferenceHighPassLadder;
	using StateVariableFilter = KernelSvf<kStereo>;
	using DryWetFade = KernelFade<kStereo>;
	using FilterSet = filters_reference::ReferenceFilterSet;
	using Compressor = filters_reference::ReferenceRmsFeedbackCompressor;

	[[nodiscard]] static filters_reference::FilterScratch filter_scratch(std::span<q31> fade, std::span<q31> parallel) {
		return {.fade = fade, .parallel = parallel};
	}

	template <typename T>
	static void digest(const T& object, Digest& d) {
		object.digest(d);
	}
};

/// @return Where @p workload, played through the reference and through the kernels in the workload's form, first
/// parts.
[[nodiscard]] inline std::optional<filter_workloads::Parting>
first_parting(const filter_workloads::FilterWorkload& workload) {
	using filter_workloads::ReferenceFamily;
	return workload.stereo ? filter_workloads::first_parting<ReferenceFamily, KernelFamily<true>>(workload)
	                       : filter_workloads::first_parting<ReferenceFamily, KernelFamily<false>>(workload);
}

/// @return @p workload's record, rendered by the kernels in the workload's form.
[[nodiscard]] inline filter_workloads::WorkloadRecord record(const filter_workloads::FilterWorkload& workload) {
	return workload.stereo ? filter_workloads::record<KernelFamily<true>>(workload)
	                       : filter_workloads::record<KernelFamily<false>>(workload);
}

} // namespace kernel_filters
