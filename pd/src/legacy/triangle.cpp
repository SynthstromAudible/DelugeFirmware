/**
 * Copyright (c) 2025 Katherine Whitlock
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

#include "dsp_ng/oscillators/legacy/triangle.hpp"
#include "dsp_ng/components/converters/fixed_float.hpp"
#include "dsp_ng/components/waves.hpp"
#include "dsp_ng/core/converter.hpp"
#include "dsp_ng/core/generator.hpp"
#include "dsp_ng/core/processor.hpp"
#include "dsp_ng/oscillators/legacy/oscillator.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <m_pd.h>
#include <memory>
#include <numbers>

alignas(32) std::array<int, 1024> output_buffer{};

static t_class* triangle_tilde_class;

using namespace deluge::dsp;

struct triangle_tilde {
	t_object x_obj;
	std::unique_ptr<oscillator::Triangle> osc;
	float x_f;       // Frequency value for signal inlet
	t_outlet* x_out; // Signal outlet

	static t_int* render(t_int* w) {
		auto* x = reinterpret_cast<triangle_tilde*>(w[1]);
		auto* out = reinterpret_cast<t_sample*>(w[2]);
		auto n = static_cast<int32_t>(w[3]);
		std::span<float> out_span{out, static_cast<size_t>(n)};
		std::span<float> out_buffer_float_span{reinterpret_cast<float*>(output_buffer.data()), static_cast<size_t>(n)};

		assert(n % 4 == 0);
		assert(alignof(decltype(output_buffer)) == 32);

		x->osc->renderBlock(std::span{output_buffer}.first(n));
		Converter<Argon<float>, Argon<int32_t>>().renderBlock(std::span{output_buffer}.first(n), out_buffer_float_span);

		std::ranges::copy_n(out_buffer_float_span.begin(), n, out);

		return (w + 4);
	}

	static void dsp(triangle_tilde* x, t_signal** sp) {
		// x->osc->setSampleRate(sp[0]->s_sr);
		dsp_add(render, 3, x, sp[0]->s_vec, sp[0]->s_n);
	}

	static void* create(t_floatarg f) {
		auto* x = reinterpret_cast<triangle_tilde*>(pd_new(triangle_tilde_class));
		x->osc = std::make_unique<oscillator::Triangle>();
		x->osc->setFrequency(f);
		x->x_out = outlet_new(&x->x_obj, &s_signal);
		return (void*)x;
	}

	static void free(triangle_tilde* x) {
		x->osc.reset();
		outlet_free(x->x_out);
	}
};

extern "C" void deluge_tri_setup(void) {
	triangle_tilde_class = class_new(        //
	    gensym("deluge_tri~"),               // name
	    (t_newmethod)triangle_tilde::create, // new
	    (t_method)triangle_tilde::free,      // free
	    sizeof(triangle_tilde), CLASS_DEFAULT, A_DEFFLOAT, 0);

	class_addmethod(triangle_tilde_class, (t_method)triangle_tilde::dsp, gensym("dsp"), A_CANT, 0);
	CLASS_MAINSIGNALIN(triangle_tilde_class, triangle_tilde, x_f);
}
