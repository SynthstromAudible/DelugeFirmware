#pragma once

#include "dsp-next/fixed/dx7/lookup.h"
#include "engine.h"

class Exp2 {
public:
	// Q24 in, Q24 out
	static int32_t lookup(int32_t x) { return deluge::dsp::fixed::dx7::exp2_lookup(dxEngine->tables, x); }
};

class Sin {
public:
	static int32_t lookup(int32_t phase) { return deluge::dsp::fixed::dx7::sin_lookup(dxEngine->tables, phase); }
};

class Freqlut {
public:
	static int32_t lookup(int32_t logfreq) { return deluge::dsp::fixed::dx7::freq_lookup(dxEngine->tables, logfreq); }
};
