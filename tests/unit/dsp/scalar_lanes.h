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

#include "dsp/core/kernel.h"
#include "dsp/core/mono.h"
#include "dsp/core/sample.h"

#include <algorithm>
#include <array>
#include <cstddef>

/// @file
/// @brief N scalar copies of a kernel, stepped as Poly steps its lanes: what Poly and Bank must equal bit for bit,
/// where a Mono each would take its own runs.
namespace run_test {

/// N scalar copies of @p K, each with its own settings, their blocks split at the least run of them all.
template <class K, std::size_t N>
class ScalarLanes {
public:
	void configure(std::size_t l, const typename K::Settings& settings) { c_[l] = K::coefficients(settings); }
	/// Runs @p Event on copy @p l.
	template <auto Event, class... A>
	void on(std::size_t l, A... args) {
		(void)deluge::dsp::detail::run_event<K, Event>(c_[l], s_[l], args...);
	}
	[[nodiscard]] const K::State& state(std::size_t l) const { return s_[l]; }

	/// One block of each generator copy.
	void render_block(const std::array<deluge::dsp::fp::Buffer, N>& out)
	requires deluge::dsp::generator_kernel<K>
	{
		run(out[0].size(), [&](std::size_t l, std::size_t i) { out[l][i] = step(l); });
	}
	/// One block of each processor copy.
	void render_block(const std::array<deluge::dsp::fp::Signal, N>& in,
	                  const std::array<deluge::dsp::fp::Buffer, N>& out)
	requires deluge::dsp::processor_kernel<K>
	{
		run(out[0].size(), [&](std::size_t l, std::size_t i) { out[l][i] = step(l, in[l][i]); });
	}

private:
	void run(std::size_t n, const auto& sample) {
		using namespace deluge::dsp;
		for (std::size_t l = 0; l < N; ++l) {
			block_[l] = kernel_ops::prepare_block<K>(c_[l], s_[l], n);
		}
		for (std::size_t i = 0; i < n;) {
			std::size_t m = n - i;
			for (std::size_t l = 0; l < N; ++l) {
				m = kernel_ops::run_length<K>(c_[l], s_[l], m);
			}
			for (std::size_t l = 0; l < N; ++l) {
				kernel_ops::prepare_run<K>(c_[l], s_[l], m);
				for (std::size_t t = 0; t < m; ++t) {
					sample(l, i + t);
				}
			}
			i += m;
		}
	}
	deluge::dsp::f32 step(std::size_t l, auto... x) {
		using namespace deluge::dsp;
		f32 y = 0.f;
		kernel_ops::dispatch_mode<K>(c_[l],
		                             [&]<auto M> { y = kernel_ops::step_at<K, M>(c_[l], block_[l], s_[l], x...); });
		return y;
	}

	std::array<typename K::Coefficients, N> c_{};
	std::array<typename K::State, N> s_{};
	std::array<deluge::dsp::kernel_block_t<K>, N> block_{};
};

} // namespace run_test
