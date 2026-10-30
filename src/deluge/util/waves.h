#pragma once

#include "dsp-next/fixed/noise/congruential_step.h"
#include "util/lookuptables/lookuptables.h"

#define CONG (jcong = deluge::dsp::fixed::CongruentialStep::next(jcong))

extern uint32_t jcong;

// The analytic waves now live in dsp-next: the square/triangle kernels (deluge::dsp::fixed::square /
// triangle / triangle_half_scale, dsp-next/fixed/bandlimited/waves.h) and the sine LUT evaluator
// (deluge::dsp::fixed::sine_table, dsp-next/fixed/osc/sine_table.h) — the canonical home shared by the
// crude oscillators, the LFO, the envelope and GUI easing.
