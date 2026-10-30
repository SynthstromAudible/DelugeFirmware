#pragma once

#include "util/lookuptables/lookuptables.h"
#include "util/synth_noise.h"

/// The synth's noise stream, drawn where a call site has not yet been given it explicitly.
#define CONG (deluge::synth_noise.next())

// The analytic waves now live in dsp-next: the square/triangle kernels (deluge::dsp::fixed::square /
// triangle / triangle_half_scale, dsp-next/fixed/bandlimited/waves.h) and the sine LUT evaluator
// (deluge::dsp::fixed::sine_table, dsp-next/fixed/osc/sine_table.h) — the canonical home shared by the
// crude oscillators, the LFO, the envelope and GUI easing.
