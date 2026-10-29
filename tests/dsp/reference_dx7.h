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
/// @brief The DX7 engine (src/deluge/dsp/dx, the Dexed/MSFA code) as the app renders it on the host, verbatim but
/// for the allowed edits below. Copied from:
///   - math_lut.h (Exp2::lookup, Sin::lookup's SIN_DELTA branch) and math_lut.cpp (exp2_init, sin_init,
///     freq_lut_init, Freqlut::lookup, dx_init_lut_data without tanh_init);
///   - fm_op_kernel.cpp (compute, compute_pure, compute_fb: the scalar branches);
///   - fm_core.h (div_n, DX_MAX_N, kGainLevelThresh, the flags) and fm_core.cpp (the algorithm table, FmCore::render);
///   - EngineMkI.cpp (its tables, sinLog, the constructor's table loops, mkiSin, every compute*, render);
///   - env.h/env.cpp (Env, without getPosition and transfer) and pitchenv.h/pitchenv.cpp (PitchEnv, without
///     getPosition);
///   - dx7note.cpp (DxPatch's render half, the scale functions and tables, DxVoice's init, getdelay, compute, keyup,
///     update, oscSync, oscUnSync, osc_freq);
///   - dx_voice_adapter.cpp (render's log2f pitch).
/// Allowed edits (the arithmetic is untouched by every one):
///   1. The tables are a Tables struct passed by const Tables& instead of read through dxEngine; MkI's two static
///      tables are members of it; tanhtab and Tanh are dropped (nothing reads them). Voice::init takes the tables first
///      and keeps the pointer; Core::render and CoreMkI::render take them first. The table-size macros are constexprs.
///   2. Core and CoreMkI each hold their own two bus buffers (the app shares one pair per engine across voices, which
///      renders identically: voices render one at a time).
///   3. Core::render computes simd_n exactly as fm_core.cpp does with neon set (the app's setEngineMode default) but
///      always takes the scalar loops: the path the x86 and ARM sim builds take under DELUGE_DX_SCALAR.
///   4. Patch's core is a bool mki, chosen by updateEngineMode exactly as dx7note.cpp chooses the engine.
///   5. getNoise() is Noise::render(), Noise a template parameter (a static policy).
///   6. Env::sr_multiplier and PitchEnv::unit_ are inline static members, set by init_tables.
///   7. Functions whose plain signed sums or shifts the dsp-ubsan tree flags are marked
///      DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC.
///   8. Each class has digest(Digest&) const, folding its members in declaration order; members the app leaves
///      uninitialised are value-initialised ({}), as the goldens' DELUGE_DETERMINISTIC_ALLOC leaves them.
///   9. The core seam: Voice is templated on its Cores, and calls cores_.mki or cores_.modern by Patch::mki where
///      DxVoice::compute calls ctrls->core->render.
///  10. Every class is a struct with every member public.
///  11. The operator kernels are free functions taking the tables first; FmOpKernel's lose their neon argument.
///  12. Counters counts each branch of Core::render and CoreMkI::render (counting only).

#pragma once

#include "engines/deluge/dx7_init_voice.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

/// @brief Marks a reference function whose legacy signed sums may overflow or whose shifts may take a negative or
/// over-wide amount: UBSan's overflow and shift checks are off in its body (and only there), wherever it is inlined.
#define DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC __attribute__((no_sanitize("signed-integer-overflow", "shift")))

namespace dx7_reference {

/// @brief A 64-bit FNV-1a digest of 32-bit words.
struct Digest {
	uint64_t value = 14695981039346656037ULL; ///< The digest so far.
	void add(uint32_t word) { value = (value ^ word) * 1099511628211ULL; }
	void add(int32_t word) { add(static_cast<uint32_t>(word)); }
	void add(uint16_t word) { add(static_cast<uint32_t>(word)); }
	void add(int16_t word) { add(static_cast<uint32_t>(static_cast<int32_t>(word))); }
	void add(uint8_t word) { add(static_cast<uint32_t>(word)); }
	void add(bool word) { add(static_cast<uint32_t>(word ? 1 : 0)); }
};

// ---- engine.h: the table sizes ----

constexpr int EXP2_LG_N_SAMPLES = 10;
constexpr int EXP2_N_SAMPLES = 1 << EXP2_LG_N_SAMPLES;
constexpr int SIN_LG_N_SAMPLES = 10;
constexpr int SIN_N_SAMPLES = 1 << SIN_LG_N_SAMPLES;
constexpr int FREQ_LG_N_SAMPLES = 10;
constexpr int FREQ_N_SAMPLES = 1 << FREQ_LG_N_SAMPLES;

// ---- EngineMkI.cpp: its table sizes ----

static const uint16_t NEGATIVE_BIT = 0x8000;
static const uint16_t ENV_BITDEPTH = 14;
static const uint16_t SINLOG_BITDEPTH = 10;
static const uint16_t SINLOG_TABLESIZE = 1 << SINLOG_BITDEPTH;
static const uint16_t SINEXP_BITDEPTH = 10;
static const uint16_t SINEXP_TABLESIZE = 1 << SINEXP_BITDEPTH;
const uint16_t ENV_MAX = 1 << ENV_BITDEPTH;

/// @brief Every table the engine reads (edit 1).
struct Tables {
	int32_t exp2tab[EXP2_N_SAMPLES << 1];   // NOLINT(*-avoid-c-arrays): verbatim
	int32_t sintab[SIN_N_SAMPLES << 1];     // NOLINT(*-avoid-c-arrays): verbatim
	int32_t freq_lut[FREQ_N_SAMPLES + 1];   // NOLINT(*-avoid-c-arrays): verbatim
	uint16_t sinLogTable[SINLOG_TABLESIZE]; // NOLINT(*-avoid-c-arrays): verbatim
	uint16_t sinExpTable[SINEXP_TABLESIZE]; // NOLINT(*-avoid-c-arrays): verbatim
};

// ---- math_lut.cpp ----

inline void exp2_init(int32_t* exp2tab) {
	double inc = exp2(1.0 / EXP2_N_SAMPLES);
	double y = 1 << 30;
	for (int i = 0; i < EXP2_N_SAMPLES; i++) {
		exp2tab[(i << 1) + 1] = (int32_t)floor(y + 0.5);
		y *= inc;
	}
	for (int i = 0; i < EXP2_N_SAMPLES - 1; i++) {
		exp2tab[i << 1] = exp2tab[(i << 1) + 3] - exp2tab[(i << 1) + 1];
	}
	exp2tab[(EXP2_N_SAMPLES << 1) - 2] = (1U << 31) - exp2tab[(EXP2_N_SAMPLES << 1) - 1];
}

#define R (1 << 29)
inline void sin_init(int32_t* sintab) {
	double dphase = 2 * M_PI / SIN_N_SAMPLES;
	int32_t c = (int32_t)floor(cos(dphase) * (1 << 30) + 0.5);
	int32_t s = (int32_t)floor(sin(dphase) * (1 << 30) + 0.5);
	int32_t u = 1 << 30;
	int32_t v = 0;
	for (int i = 0; i < SIN_N_SAMPLES / 2; i++) {
		sintab[(i << 1) + 1] = (v + 32) >> 6;
		sintab[((i + SIN_N_SAMPLES / 2) << 1) + 1] = -((v + 32) >> 6);
		int32_t t = ((int64_t)u * (int64_t)s + (int64_t)v * (int64_t)c + R) >> 30;
		u = ((int64_t)u * (int64_t)c - (int64_t)v * (int64_t)s + R) >> 30;
		v = t;
	}
	for (int i = 0; i < SIN_N_SAMPLES - 1; i++) {
		sintab[i << 1] = sintab[(i << 1) + 3] - sintab[(i << 1) + 1];
	}
	sintab[(SIN_N_SAMPLES << 1) - 2] = -sintab[(SIN_N_SAMPLES << 1) - 1];
}
#undef R

#define SAMPLE_SHIFT (24 - FREQ_LG_N_SAMPLES)
#define MAX_LOGFREQ_INT 20
inline void freq_lut_init(int32_t* freq_lut, double sample_rate) {
	double y = (1LL << (24 + MAX_LOGFREQ_INT)) / sample_rate;
	double inc = pow(2, 1.0 / FREQ_N_SAMPLES);
	for (int i = 0; i < FREQ_N_SAMPLES + 1; i++) {
		freq_lut[i] = (int32_t)floor(y + 0.5);
		y *= inc;
	}
}

DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC inline int32_t freq_lookup(const Tables& t, int32_t logfreq) {
	int ix = (logfreq & 0xffffff) >> SAMPLE_SHIFT;

	int32_t y0 = t.freq_lut[ix];
	int32_t y1 = t.freq_lut[ix + 1];
	int lowbits = logfreq & ((1 << SAMPLE_SHIFT) - 1);
	int32_t y = y0 + ((((int64_t)(y1 - y0) * (int64_t)lowbits)) >> SAMPLE_SHIFT);
	int hibits = logfreq >> 24;
	return y >> (MAX_LOGFREQ_INT - hibits);
}
#undef SAMPLE_SHIFT
#undef MAX_LOGFREQ_INT

// ---- math_lut.h ----

DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC inline int32_t exp2_lookup(const Tables& t, int32_t x) {
	const int SHIFT = 24 - EXP2_LG_N_SAMPLES;
	int lowbits = x & ((1 << SHIFT) - 1);
	int x_int = (x >> (SHIFT - 1)) & ((EXP2_N_SAMPLES - 1) << 1);
	int dy = t.exp2tab[x_int];
	int y0 = t.exp2tab[x_int + 1];

	int y = y0 + (((int64_t)dy * (int64_t)lowbits) >> SHIFT);
	return y >> (6 - (x >> 24));
}

DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC inline int32_t sin_lookup(const Tables& t, int32_t phase) {
	const int SHIFT = 24 - SIN_LG_N_SAMPLES;
	int lowbits = phase & ((1 << SHIFT) - 1);
	int phase_int = (phase >> (SHIFT - 1)) & ((SIN_N_SAMPLES - 1) << 1);
	int dy = t.sintab[phase_int];
	int y0 = t.sintab[phase_int + 1];

	return y0 + (((int64_t)dy * (int64_t)lowbits) >> SHIFT);
}

// ---- EngineMkI.cpp: the tables and the log-sin ----

/// @brief EngineMkI's constructor's table loops.
inline void init_mki_tables(Tables& t) {
	float bitReso = SINLOG_TABLESIZE;

	for (int i = 0; i < SINLOG_TABLESIZE; i++) {
		float x1 = sin(((0.5 + i) / bitReso) * M_PI / 2.0);
		t.sinLogTable[i] = round(-1024 * log2(x1));
	}

	bitReso = SINEXP_TABLESIZE;
	for (int i = 0; i < SINEXP_TABLESIZE; i++) {
		float x1 = (pow(2, float(i) / bitReso) - 1) * 4096;
		t.sinExpTable[i] = round(x1);
	}
}

inline uint16_t sinLog(const Tables& t, uint16_t phi) {
	const uint16_t SINLOG_TABLEFILTER = SINLOG_TABLESIZE - 1;
	const uint16_t index = (phi & SINLOG_TABLEFILTER);

	switch ((phi & (SINLOG_TABLESIZE * 3))) {
	case 0:
		return t.sinLogTable[index];
	case SINLOG_TABLESIZE:
		return t.sinLogTable[index ^ SINLOG_TABLEFILTER];
	case SINLOG_TABLESIZE * 2:
		return t.sinLogTable[index] | NEGATIVE_BIT;
	default:
		return t.sinLogTable[index ^ SINLOG_TABLEFILTER] | NEGATIVE_BIT;
	}
}

DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC inline int32_t mki_sin(const Tables& t, int32_t phase, uint16_t env) {
	uint16_t expVal = sinLog(t, phase >> (22 - SINLOG_BITDEPTH)) + (env);

	const bool isSigned = expVal & NEGATIVE_BIT;
	expVal &= ~NEGATIVE_BIT;

	const uint16_t SINEXP_FILTER = 0x3FF;
	uint16_t result = 4096 + t.sinExpTable[(expVal & SINEXP_FILTER) ^ SINEXP_FILTER];

	result >>= (expVal >> 10); // exp

	if (isSigned)
		return (-result - 1) << 13;
	else
		return result << 13;
}

// ---- Env and PitchEnv (env.h, env.cpp, pitchenv.h, pitchenv.cpp) ----

struct EnvParams {
	uint8_t rates[4];  // NOLINT(*-avoid-c-arrays): verbatim, read through the patch bytes
	uint8_t levels[4]; // NOLINT(*-avoid-c-arrays): verbatim
};

inline const int levellut[] = {0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46};

inline const int statics[] = {
    1764000, 1764000, 1411200, 1411200, 1190700, 1014300, 992250, 882000, 705600, 705600, 584325, 507150, 502740,
    441000,  418950,  352800,  308700,  286650,  253575,  220500, 220500, 176400, 145530, 145530, 125685, 110250,
    110250,  88200,   88200,   74970,   61740,   61740,   55125,  48510,  44100,  37485,  31311,  30870,  27562,
    27562,   22050,   18522,   17640,   15435,   14112,   13230,  11025,  9261,   9261,   7717,   6615,   6615,
    5512,    5512,    4410,    3969,    3969,    3439,    2866,   2690,   2249,   1984,   1896,   1808,   1411,
    1367,    1234,    1146,    926,     837,     837,     705,    573,    573,    529,    441,    441};

struct Env {
	inline static uint32_t sr_multiplier = (1 << 24);

	int32_t level_{};
	int targetlevel_{};
	int ix_{};
	int inc_{};
	int staticcount_{};

	int outlevel_{};
	int rate_scaling_{};
	bool down_{};
	bool rising_{};

	static void init_sr(double sampleRate) { sr_multiplier = (44100.0 / sampleRate) * (1 << 24); }

	void init(const EnvParams& p, int ol, int rate_scaling) {
		outlevel_ = ol;
		rate_scaling_ = rate_scaling;
		level_ = 0;
		down_ = true;
		advance(p, 0, 0);
	}

	DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC int32_t getsample(const EnvParams& p, int n, int extra_rate) {
		if (staticcount_) {
			staticcount_ -= n;
			if (staticcount_ <= 0) {
				staticcount_ = 0;
				advance(p, ix_ + 1, extra_rate);
			}
		}

		if (ix_ < 3 || ((ix_ < 4) && !down_)) {
			if (staticcount_) {
				;
			}
			else if (rising_) {
				const int jumptarget = 1716;
				if (level_ < (jumptarget << 16)) {
					level_ = jumptarget << 16;
				}
				level_ += (((17 << 24) - level_) >> 24) * inc_ * n;
				if (level_ >= targetlevel_) {
					level_ = targetlevel_;
					advance(p, ix_ + 1, extra_rate);
				}
			}
			else { // !rising
				level_ -= inc_ * n;
				if (level_ <= targetlevel_) {
					level_ = targetlevel_;
					advance(p, ix_ + 1, extra_rate);
				}
			}
		}
		return level_;
	}

	void keydown(const EnvParams& p, bool d) {
		if (down_ != d) {
			down_ = d;
			advance(p, d ? 0 : 3, 0);
		}
	}

	static int scaleoutlevel(int outlevel) { return outlevel >= 20 ? 28 + outlevel : levellut[outlevel]; }

	DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC void advance(const EnvParams& p, int newix, int extra_rate) {
		ix_ = newix;
		if (ix_ < 4) {
			int newlevel = p.levels[ix_];
			int actuallevel = scaleoutlevel(newlevel) >> 1;
			actuallevel = (actuallevel << 6) + outlevel_ - 4256;
			actuallevel = actuallevel < 16 ? 16 : actuallevel;
			targetlevel_ = actuallevel << 16;
			rising_ = (targetlevel_ > level_);

			int qrate = (p.rates[ix_] * 41) >> 6;
			qrate += rate_scaling_ + extra_rate;
			qrate = std::min(qrate, 63);

			if (targetlevel_ == level_ || (ix_ == 0 && newlevel == 0)) {
				int staticrate = p.rates[ix_];
				staticrate += rate_scaling_ + extra_rate;
				staticrate = std::min(staticrate, 99);
				staticcount_ = staticrate < 77 ? statics[staticrate] : 20 * (99 - staticrate);
				if (staticrate < 77 && (ix_ == 0 && newlevel == 0)) {
					staticcount_ /= 20; // attack is scaled faster
				}
				staticcount_ = (int)(((int64_t)staticcount_ * (int64_t)sr_multiplier) >> 24);
			}
			else {
				staticcount_ = 0;
			}
			inc_ = ((4 + (qrate & 3)) << (2 + (qrate >> 2)));
			inc_ = (int)(((int64_t)inc_ * (int64_t)sr_multiplier) >> 24);
		}
	}

	DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC void update(const EnvParams& p, int ol, int rate_scaling) {
		outlevel_ = ol;
		rate_scaling_ = rate_scaling;
		if (down_) {
			int newlevel = p.levels[2];
			int actuallevel = scaleoutlevel(newlevel) >> 1;
			actuallevel = (actuallevel << 6) - 4256;
			actuallevel = actuallevel < 16 ? 16 : actuallevel;
			targetlevel_ = actuallevel << 16;
			advance(p, 2, 0);
		}
	}

	void digest(Digest& d) const {
		d.add(level_);
		d.add(static_cast<int32_t>(targetlevel_));
		d.add(static_cast<int32_t>(ix_));
		d.add(static_cast<int32_t>(inc_));
		d.add(static_cast<int32_t>(staticcount_));
		d.add(static_cast<int32_t>(outlevel_));
		d.add(static_cast<int32_t>(rate_scaling_));
		d.add(down_);
		d.add(rising_);
	}
};

inline const uint8_t pitchenv_rate[] = {
    1,   2,   3,   3,   4,   4,   5,   5,   6,   6,   7,   7,   8,   8,   9,   9,   10,  10,  11,  11,
    12,  12,  13,  13,  14,  14,  15,  16,  16,  17,  18,  18,  19,  20,  21,  22,  23,  24,  25,  26,
    27,  28,  30,  31,  33,  34,  36,  37,  38,  39,  41,  42,  44,  46,  47,  49,  51,  53,  54,  56,
    58,  60,  62,  64,  66,  68,  70,  72,  74,  76,  79,  82,  85,  88,  91,  94,  98,  102, 106, 110,
    115, 120, 125, 130, 135, 141, 147, 153, 159, 165, 171, 178, 185, 193, 202, 211, 232, 243, 254, 255};

inline const int8_t pitchenv_tab[] = {
    -128, -116, -104, -95, -85, -76, -68, -61, -56, -52, -49, -46, -43, -41, -39, -37, -35, -33, -32, -31,
    -30,  -29,  -28,  -27, -26, -25, -24, -23, -22, -21, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11,
    -10,  -9,   -8,   -7,  -6,  -5,  -4,  -3,  -2,  -1,  0,   1,   2,   3,   4,   5,   6,   7,   8,   9,
    10,   11,   12,   13,  14,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,
    30,   31,   32,   33,  34,  35,  38,  40,  43,  46,  49,  53,  58,  65,  73,  82,  92,  103, 115, 127};

struct PitchEnv {
	inline static int unit_ = 0;
	int32_t level_{};
	int targetlevel_{};
	bool rising_{};
	int ix_{};
	int inc_{};
	bool down_{};

	static void init(double sample_rate) { unit_ = (1 << 24) / (21.3 * sample_rate) + 0.5; }

	void set(const EnvParams& p) {
		level_ = pitchenv_tab[p.levels[3]] << 19;
		down_ = true;
		advance(p, 0);
	}

	DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC int32_t getsample(const EnvParams& p, int n) {
		if (ix_ < 3 || ((ix_ < 4) && !down_)) {
			if (rising_) {
				level_ += n * inc_;
				if (level_ >= targetlevel_) {
					level_ = targetlevel_;
					advance(p, ix_ + 1);
				}
			}
			else { // !rising
				level_ -= n * inc_;
				if (level_ <= targetlevel_) {
					level_ = targetlevel_;
					advance(p, ix_ + 1);
				}
			}
		}
		return level_;
	}

	void keydown(const EnvParams& p, bool d) {
		if (down_ != d) {
			down_ = d;
			advance(p, d ? 0 : 3);
		}
	}

	[[nodiscard]] bool isDown() const { return down_; }

	DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC void advance(const EnvParams& p, int newix) {
		ix_ = newix;
		if (ix_ < 4) {
			int newlevel = p.levels[ix_];
			targetlevel_ = pitchenv_tab[newlevel] << 19;
			rising_ = (targetlevel_ > level_);
			inc_ = pitchenv_rate[p.rates[ix_]] * unit_;
		}
	}

	void digest(Digest& d) const {
		d.add(level_);
		d.add(static_cast<int32_t>(targetlevel_));
		d.add(rising_);
		d.add(static_cast<int32_t>(ix_));
		d.add(static_cast<int32_t>(inc_));
		d.add(down_);
	}
};

/// @brief Every table, built as the engine builds them (dx_init_lut_data without tanh, EngineMkI's constructor) and
/// the envelopes' rate units at 44.1 kHz.
inline void init_tables(Tables& t) {
	init_mki_tables(t);
	exp2_init(t.exp2tab);
	sin_init(t.sintab);
	freq_lut_init(t.freq_lut, 44100);
	PitchEnv::init(44100);
	Env::init_sr(44100);
}

// ---- fm_op_kernel.h and fm_op_kernel.cpp: the scalar kernels ----

struct FmOpParams {
	int32_t level_in; // value to be computed (from level to gain[0])
	int32_t gain_out; // computed value (gain[1] to gain[0])
	int32_t freq;
	int32_t phase;
};

DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC inline void compute(const Tables& t, int32_t* output, int n,
                                                           const int32_t* input, int32_t phase0, int32_t freq,
                                                           int32_t gain1, int32_t gain2, int32_t dgain, bool add) {
	int32_t gain = gain1;
	int32_t phase = phase0;
	if (add) {
		for (int i = 0; i < n; i++) {
			gain += dgain;
			int32_t y = sin_lookup(t, phase + input[i]);
			int32_t y1 = ((int64_t)y * (int64_t)gain) >> 24;
			output[i] += y1;
			phase += freq;
		}
	}
	else {
		for (int i = 0; i < n; i++) {
			gain += dgain;
			int32_t y = sin_lookup(t, phase + input[i]);
			int32_t y1 = ((int64_t)y * (int64_t)gain) >> 24;
			output[i] = y1;
			phase += freq;
		}
	}
}

DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC inline void compute_pure(const Tables& t, int32_t* output, int n, int32_t phase0,
                                                                int32_t freq, int32_t gain1, int32_t gain2,
                                                                int32_t dgain, bool add) {
	int32_t gain = gain1;
	int32_t phase = phase0;
	if (add) {
		for (int i = 0; i < n; i++) {
			gain += dgain;
			int32_t y = sin_lookup(t, phase);
			int32_t y1 = ((int64_t)y * (int64_t)gain) >> 24;
			output[i] += y1;
			phase += freq;
		}
	}
	else {
		for (int i = 0; i < n; i++) {
			gain += dgain;
			int32_t y = sin_lookup(t, phase);
			int32_t y1 = ((int64_t)y * (int64_t)gain) >> 24;
			output[i] = y1;
			phase += freq;
		}
	}
}

DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC inline void compute_fb(const Tables& t, int32_t* output, int n, int32_t phase0,
                                                              int32_t freq, int32_t gain1, int32_t gain2, int32_t dgain,
                                                              int32_t* fb_buf, int fb_shift, bool add) {
	int32_t gain = gain1;
	int32_t phase = phase0;
	int32_t y0 = fb_buf[0];
	int32_t y = fb_buf[1];
	if (add) {
		for (int i = 0; i < n; i++) {
			gain += dgain;
			int32_t scaled_fb = (y0 + y) >> (fb_shift + 1);
			y0 = y;
			y = sin_lookup(t, phase + scaled_fb);
			y = ((int64_t)y * (int64_t)gain) >> 24;
			output[i] += y;
			phase += freq;
		}
	}
	else {
		for (int i = 0; i < n; i++) {
			gain += dgain;
			int32_t scaled_fb = (y0 + y) >> (fb_shift + 1);
			y0 = y;
			y = sin_lookup(t, phase + scaled_fb);
			y = ((int64_t)y * (int64_t)gain) >> 24;
			output[i] = y;
			phase += freq;
		}
	}
	fb_buf[0] = y0;
	fb_buf[1] = y;
}

// ---- EngineMkI.cpp: the MkI kernels ----

/// Silence to add to, for an operator that does not add into its output.
inline const int32_t __attribute__((aligned(16))) kMkIZeros[132] = {0}; // NOLINT(*-avoid-c-arrays): verbatim

DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC inline void mki_compute(const Tables& t, int32_t* output, int n,
                                                               const int32_t* input, int32_t phase0, int32_t freq,
                                                               int32_t gain1, int32_t gain2, int32_t dgain, bool add) {
	int32_t gain = gain1;
	int32_t phase = phase0;
	const int32_t* adder = add ? output : kMkIZeros;

	for (int i = 0; i < n; i++) {
		gain += dgain;
		int32_t y = mki_sin(t, (phase + input[i]), gain);
		output[i] = y + adder[i];
		phase += freq;
	}
}

DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC inline void mki_compute_pure(const Tables& t, int32_t* output, int n,
                                                                    int32_t phase0, int32_t freq, int32_t gain1,
                                                                    int32_t gain2, int32_t dgain, bool add) {
	int32_t gain = gain1;
	int32_t phase = phase0;
	const int32_t* adder = add ? output : kMkIZeros;

	for (int i = 0; i < n; i++) {
		gain += dgain;
		int32_t y = mki_sin(t, phase, gain);
		output[i] = y + adder[i];
		phase += freq;
	}
}

DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC inline void mki_compute_fb(const Tables& t, int32_t* output, int n,
                                                                  int32_t phase0, int32_t freq, int32_t gain1,
                                                                  int32_t gain2, int32_t dgain, int32_t* fb_buf,
                                                                  int fb_shift, bool add) {
	int32_t gain = gain1;
	int32_t phase = phase0;
	const int32_t* adder = add ? output : kMkIZeros;
	int32_t y0 = fb_buf[0];
	int32_t y = fb_buf[1];

	for (int i = 0; i < n; i++) {
		gain += dgain;
		int32_t scaled_fb = (y0 + y) >> (fb_shift + 1);
		y0 = y;
		y = mki_sin(t, (phase + scaled_fb), gain);
		output[i] = y + adder[i];
		phase += freq;
	}

	fb_buf[0] = y0;
	fb_buf[1] = y;
}

// exclusively used for ALGO 6 with feedback
DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC inline void mki_compute_fb2(const Tables& t, int32_t* output, int n,
                                                                   FmOpParams* parms, int32_t gain01, int32_t gain02,
                                                                   int32_t dgain0, int32_t* fb_buf, int fb_shift) {
	int32_t dgain[2]; // NOLINT(*-avoid-c-arrays): verbatim
	int32_t gain[2];  // NOLINT(*-avoid-c-arrays): verbatim
	int32_t phase[2]; // NOLINT(*-avoid-c-arrays): verbatim
	int32_t y0 = fb_buf[0];
	int32_t y = fb_buf[1];

	phase[0] = parms[0].phase;
	phase[1] = parms[1].phase;

	parms[1].gain_out = (ENV_MAX - (parms[1].level_in >> (28 - ENV_BITDEPTH)));

	gain[0] = gain01;
	gain[1] = parms[1].gain_out == 0 ? (ENV_MAX - 1) : parms[1].gain_out;

	dgain[0] = dgain0;
	dgain[1] = (parms[1].gain_out - (parms[1].gain_out == 0 ? (ENV_MAX - 1) : parms[1].gain_out));

	for (int i = 0; i < n; i++) {
		int32_t scaled_fb = (y0 + y) >> (fb_shift + 1);

		// op 0
		gain[0] += dgain[0];
		y0 = y;
		y = mki_sin(t, phase[0] + scaled_fb, gain[0]);
		phase[0] += parms[0].freq;

		// op 1
		gain[1] += dgain[1];
		y = mki_sin(t, phase[1] + y, gain[1]);
		phase[1] += parms[1].freq;

		output[i] = y;
	}
	fb_buf[0] = y0;
	fb_buf[1] = y;
}

// exclusively used for ALGO 4 with feedback
DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC inline void mki_compute_fb3(const Tables& t, int32_t* output, int n,
                                                                   FmOpParams* parms, int32_t gain01, int32_t gain02,
                                                                   int32_t dgain0, int32_t* fb_buf, int fb_shift) {
	int32_t dgain[3]; // NOLINT(*-avoid-c-arrays): verbatim
	int32_t gain[3];  // NOLINT(*-avoid-c-arrays): verbatim
	int32_t phase[3]; // NOLINT(*-avoid-c-arrays): verbatim
	int32_t y0 = fb_buf[0];
	int32_t y = fb_buf[1];

	phase[0] = parms[0].phase;
	phase[1] = parms[1].phase;
	phase[2] = parms[2].phase;

	parms[1].gain_out = (ENV_MAX - (parms[1].level_in >> (28 - ENV_BITDEPTH)));
	parms[2].gain_out = (ENV_MAX - (parms[2].level_in >> (28 - ENV_BITDEPTH)));

	gain[0] = gain01;
	gain[1] = parms[1].gain_out == 0 ? (ENV_MAX - 1) : parms[1].gain_out;
	gain[2] = parms[2].gain_out == 0 ? (ENV_MAX - 1) : parms[2].gain_out;

	dgain[0] = dgain0;
	dgain[1] = (parms[1].gain_out - (parms[1].gain_out == 0 ? (ENV_MAX - 1) : parms[1].gain_out));
	dgain[2] = (parms[2].gain_out - (parms[2].gain_out == 0 ? (ENV_MAX - 1) : parms[2].gain_out));

	for (int i = 0; i < n; i++) {
		int32_t scaled_fb = (y0 + y) >> (fb_shift + 1);

		// op 0
		gain[0] += dgain[0];
		y0 = y;
		y = mki_sin(t, phase[0] + scaled_fb, gain[0]);
		phase[0] += parms[0].freq;

		// op 1
		gain[1] += dgain[1];
		y = mki_sin(t, phase[1] + y, gain[1]);
		phase[1] += parms[1].freq;

		// op 2
		gain[2] += dgain[2];
		y = mki_sin(t, phase[2] + y, gain[2]);
		phase[2] += parms[2].freq;

		output[i] = y;
	}
	fb_buf[0] = y0;
	fb_buf[1] = y;
}

// ---- fm_core.h and fm_core.cpp ----

#define div_n(base, inv_n) ((int)((((int64_t)(base)) * (int64_t)(inv_n)) >> 30))

const static int DX_MAX_N = 132;

// operator should be considered inaudible when gain_out is below this
const int kGainLevelThresh = 1120;

enum FmOperatorFlags {
	OUT_BUS_ONE = 1 << 0,
	OUT_BUS_TWO = 1 << 1,
	OUT_BUS_ADD = 1 << 2,
	IN_BUS_ONE = 1 << 4,
	IN_BUS_TWO = 1 << 5,
	FB_IN = 1 << 6,
	FB_OUT = 1 << 7
};

struct FmAlgorithm {
	int ops[6]; // NOLINT(*-avoid-c-arrays): verbatim
};

inline const FmAlgorithm algorithms[32] = {
    {{0xc1, 0x11, 0x11, 0x14, 0x01, 0x14}}, // 1
    {{0x01, 0x11, 0x11, 0x14, 0xc1, 0x14}}, // 2
    {{0xc1, 0x11, 0x14, 0x01, 0x11, 0x14}}, // 3
    {{0xc1, 0x11, 0x94, 0x01, 0x11, 0x14}}, // 4
    {{0xc1, 0x14, 0x01, 0x14, 0x01, 0x14}}, // 5
    {{0xc1, 0x94, 0x01, 0x14, 0x01, 0x14}}, // 6
    {{0xc1, 0x11, 0x05, 0x14, 0x01, 0x14}}, // 7
    {{0x01, 0x11, 0xc5, 0x14, 0x01, 0x14}}, // 8
    {{0x01, 0x11, 0x05, 0x14, 0xc1, 0x14}}, // 9
    {{0x01, 0x05, 0x14, 0xc1, 0x11, 0x14}}, // 10
    {{0xc1, 0x05, 0x14, 0x01, 0x11, 0x14}}, // 11
    {{0x01, 0x05, 0x05, 0x14, 0xc1, 0x14}}, // 12
    {{0xc1, 0x05, 0x05, 0x14, 0x01, 0x14}}, // 13
    {{0xc1, 0x05, 0x11, 0x14, 0x01, 0x14}}, // 14
    {{0x01, 0x05, 0x11, 0x14, 0xc1, 0x14}}, // 15
    {{0xc1, 0x11, 0x02, 0x25, 0x05, 0x14}}, // 16
    {{0x01, 0x11, 0x02, 0x25, 0xc5, 0x14}}, // 17
    {{0x01, 0x11, 0x11, 0xc5, 0x05, 0x14}}, // 18
    {{0xc1, 0x14, 0x14, 0x01, 0x11, 0x14}}, // 19
    {{0x01, 0x05, 0x14, 0xc1, 0x14, 0x14}}, // 20
    {{0x01, 0x14, 0x14, 0xc1, 0x14, 0x14}}, // 21
    {{0xc1, 0x14, 0x14, 0x14, 0x01, 0x14}}, // 22
    {{0xc1, 0x14, 0x14, 0x01, 0x14, 0x04}}, // 23
    {{0xc1, 0x14, 0x14, 0x14, 0x04, 0x04}}, // 24
    {{0xc1, 0x14, 0x14, 0x04, 0x04, 0x04}}, // 25
    {{0xc1, 0x05, 0x14, 0x01, 0x14, 0x04}}, // 26
    {{0x01, 0x05, 0x14, 0xc1, 0x14, 0x04}}, // 27
    {{0x04, 0xc1, 0x11, 0x14, 0x01, 0x14}}, // 28
    {{0xc1, 0x14, 0x01, 0x14, 0x04, 0x04}}, // 29
    {{0x04, 0xc1, 0x11, 0x14, 0x04, 0x04}}, // 30
    {{0xc1, 0x14, 0x04, 0x04, 0x04, 0x04}}, // 31
    {{0xc4, 0x04, 0x04, 0x04, 0x04, 0x04}}, // 32
};

// ---- aligned_buf.h ----

template <typename T, size_t size, size_t alignment = 16>
struct AlignedBuf {
	T* get() { return (T*)((((intptr_t)storage_) + alignment - 1) & -alignment); }
	unsigned char storage_[size * sizeof(T) + alignment]{}; // NOLINT(*-avoid-c-arrays): verbatim
};

/// @brief How often each branch of the two cores ran, over every core since the last reset (edit 12).
struct Counters {
	std::size_t modern_pure, modern_bus, modern_fb, mki_pure, mki_bus, mki_fb, mki_fb2, mki_fb3, mki_alg32_fb, gated;
	inline static Counters& instance() {
		static Counters counters{};
		return counters;
	}
	static void reset() { instance() = {}; }
	static const Counters& get() { return instance(); }
};

/// @brief FmCore: the modern engine, scalar (edit 3).
struct Core {
	AlignedBuf<int32_t, DX_MAX_N> buf_[2]{}; // NOLINT(*-avoid-c-arrays): verbatim

	DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC void render(const Tables& t, int32_t* output, int n, FmOpParams* params,
	                                                   int algorithm, int32_t* fb_buf, int32_t feedback_shift) {
		const FmAlgorithm alg = algorithms[algorithm];

		// tricky: (n%12) == 8 is not allowed in neon_fm_kernel().
		// otherwise, round up to the nearest multiple of 4
		int nmod = 1 + (n + 11) % 12;
		int simd_n = nmod == 8 ? n + 4 : (n + 3) & ~3;

		const int inv_n = (1 << 30) / n;
		bool has_contents[3] = {true, false, false}; // NOLINT(*-avoid-c-arrays): verbatim
		for (int op = 0; op < 6; op++) {
			int flags = alg.ops[op];
			bool add = (flags & OUT_BUS_ADD) != 0;
			FmOpParams& param = params[op];
			int inbus = (flags >> 4) & 3;
			int outbus = flags & 3;
			int32_t* outptr = (outbus == 0) ? output : buf_[outbus - 1].get();
			int32_t gain1 = param.gain_out;
			int32_t gain2 = exp2_lookup(t, param.level_in - (14 * (1 << 24)));
			param.gain_out = gain2;
			int32_t dgain = div_n(gain2 - gain1 + (n >> 1), inv_n);

			if (gain1 >= kGainLevelThresh || gain2 >= kGainLevelThresh) {
				if (!has_contents[outbus]) {
					add = false;
				}
				if (inbus == 0 || !has_contents[inbus]) {
					if ((flags & 0xc0) == 0xc0 && feedback_shift < 16) {
						++Counters::instance().modern_fb;
						compute_fb(t, outptr, n, param.phase, param.freq, gain1, gain2, dgain, fb_buf, feedback_shift,
						           add);
					}
					else {
						++Counters::instance().modern_pure;
						compute_pure(t, outptr, simd_n, param.phase, param.freq, gain1, gain2, dgain, add);
					}
				}
				else {
					++Counters::instance().modern_bus;
					compute(t, outptr, simd_n, buf_[inbus - 1].get(), param.phase, param.freq, gain1, gain2, dgain,
					        add);
				}
				has_contents[outbus] = true;
			}
			else {
				++Counters::instance().gated;
				if (!add) {
					has_contents[outbus] = false;
				}
			}
			param.phase += param.freq * n;
		}
	}
};

/// @brief EngineMkI's render.
struct CoreMkI {
	AlignedBuf<int32_t, DX_MAX_N> buf_[2]{}; // NOLINT(*-avoid-c-arrays): verbatim

	DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC void render(const Tables& t, int32_t* output, int n, FmOpParams* params,
	                                                   int algorithm, int32_t* fb_buf, int32_t feedback_shift) {
		const int kLevelThresh = ENV_MAX - 100;
		FmAlgorithm alg = algorithms[algorithm];
		bool has_contents[3] = {true, false, false}; // NOLINT(*-avoid-c-arrays): verbatim
		bool fb_on = feedback_shift < 16;
		const int inv_n = (1 << 30) / n;

		switch (algorithm) {
		case 3:
		case 5:
			if (fb_on)
				alg.ops[0] = 0xc4;
		}

		for (int op = 0; op < 6; op++) {
			int flags = alg.ops[op];
			bool add = (flags & OUT_BUS_ADD) != 0;
			FmOpParams& param = params[op];
			int inbus = (flags >> 4) & 3;
			int outbus = flags & 3;
			int32_t* outptr = (outbus == 0) ? output : buf_[outbus - 1].get();
			int32_t gain1 = param.gain_out == 0 ? (ENV_MAX - 1) : param.gain_out;
			int32_t gain2 = ENV_MAX - (param.level_in >> (28 - ENV_BITDEPTH));
			param.gain_out = gain2;
			int32_t dgain = div_n(gain2 - gain1 + (n >> 1), inv_n);

			if (gain1 <= kLevelThresh || gain2 <= kLevelThresh) {

				if (!has_contents[outbus]) {
					add = false;
				}

				if (inbus == 0 || !has_contents[inbus]) {
					if ((flags & 0xc0) == 0xc0 && fb_on) {
						switch (algorithm) {
						case 3:
							++Counters::instance().mki_fb3;
							mki_compute_fb3(t, outptr, n, params, gain1, gain2, dgain, fb_buf,
							                std::min((feedback_shift + 2), (int32_t)16));
							params[1].phase += params[1].freq * n;
							params[2].phase += params[2].freq * n;
							op += 2;
							break;
						case 5:
							++Counters::instance().mki_fb2;
							mki_compute_fb2(t, outptr, n, params, gain1, gain2, dgain, fb_buf,
							                std::min((feedback_shift + 2), (int32_t)16));
							params[1].phase += params[1].freq * n;
							op++;
							break;
						case 31:
							++Counters::instance().mki_alg32_fb;
							mki_compute_fb(t, outptr, n, param.phase, param.freq, gain1, gain2, dgain, fb_buf,
							               std::min((feedback_shift + 2), (int32_t)16), add);
							break;
						default:
							++Counters::instance().mki_fb;
							mki_compute_fb(t, outptr, n, param.phase, param.freq, gain1, gain2, dgain, fb_buf,
							               feedback_shift, add);
							break;
						}
					}
					else {
						++Counters::instance().mki_pure;
						mki_compute_pure(t, outptr, n, param.phase, param.freq, gain1, gain2, dgain, add);
					}
				}
				else {
					++Counters::instance().mki_bus;
					mki_compute(t, outptr, n, buf_[inbus - 1].get(), param.phase, param.freq, gain1, gain2, dgain, add);
				}

				has_contents[outbus] = true;
			}
			else {
				++Counters::instance().gated;
				if (!add) {
					has_contents[outbus] = false;
				}
			}
			param.phase += param.freq * n;
		}
	}
};

#undef div_n

/// @brief The cores a Voice renders through by default.
struct ReferenceCores {
	Core modern;
	CoreMkI mki;
};

// ---- dx7note.cpp ----

const int FEEDBACK_BITDEPTH = 8;

inline const int32_t coarsemul[] = {-16777216, 0,        16777216, 26591258, 33554432, 38955489, 43368474, 47099600,
                                    50331648,  53182516, 55732705, 58039632, 60145690, 62083076, 63876816, 65546747,
                                    67108864,  68576247, 69959732, 71268397, 72509921, 73690858, 74816848, 75892776,
                                    76922906,  77910978, 78860292, 79773775, 80654032, 81503396, 82323963, 83117622};

// 0, 66, 109, 255
inline const uint32_t ampmodsenstab[] = {0, 4342338, 7171437, 16777216};

inline const uint8_t pitchmodsenstab[] = {0, 10, 20, 33, 55, 92, 153, 255};

inline int dxNoteToFreq(int note) {
	const int base = 50857777; // (1 << 24) * (log(440) / log(2) - 69/12)
	const int step = (1 << 24) / 12;
	return base + step * note;
}

DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC inline int32_t lfoPhaseToValue(const Tables& t, int32_t phase_, int waveform) {
	int32_t x;
	if (waveform == 0)
		waveform = 4;
	switch (waveform) {
	case 0: // triangle
		x = phase_ >> 7;
		x ^= -(phase_ >> 31);
		x &= (1 << 24) - 1;
		return x;
	case 1: // sawtooth down
		return (~phase_ ^ (1U << 31)) >> 8;
	case 2: // sawtooth up
		return (phase_ ^ (1U << 31)) >> 8;
	case 3: // square
		return ((~phase_) >> 7) & (1 << 24);
	case 4: // sine
		return (1 << 23) + (sin_lookup(t, phase_ >> 8) >> 1);
	default:
		return 1 << 23;
	}
}

constexpr int32_t lfo_unit = (int32_t)(25190424 / (float)44100 + 0.5);

/// @brief DxPatch's render half (edit 4: core is the bool mki).
struct Patch {
	uint8_t params[156]{}; // NOLINT(*-avoid-c-arrays): verbatim
	bool mki{};
	uint8_t engineMode{};
	int32_t random_detune{};
	int pitch_mod{};
	int eg_mod{};
	uint32_t lfo_phase{}; // Q32
	uint32_t lfo_delta{};
	uint32_t lfo_value{};

	Patch() {
		memcpy(params, deluge::instrument::kDx7InitVoice.data(), sizeof params);
		eg_mod = 127;
		random_detune = 0;

		setEngineMode(0);
	}

	[[nodiscard]] bool opSwitch(int op) const { return (params[155] >> op) & 1; }

	void updateEngineMode() {
		mki = false;
		if (engineMode == 2) {
			mki = true;
		}
		else if (engineMode == 0) {
			int algo = params[134];
			int feedback = params[135];
			if (feedback > 0 && (algo == 3 || algo == 5)) {
				mki = true;
			}
		}
	}

	void setEngineMode(int mode) {
		engineMode = (uint8_t)mode;

		updateEngineMode();
	}

	DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC void computeLfo(const Tables& t, int n) {
		int rate = params[137]; // 0..99
		int sr = rate == 0 ? 1 : (165 * rate) >> 6;
		sr *= sr < 160 ? 11 : (11 + ((sr - 160) >> 4));
		lfo_delta = lfo_unit * sr;

		lfo_phase += n * lfo_delta;
		int waveform_ = params[142];
		lfo_value = lfoPhaseToValue(t, lfo_phase, waveform_);
	}

	void digest(Digest& d) const {
		d.add(lfo_phase);
		d.add(lfo_delta);
		d.add(lfo_value);
		d.add(mki);
		d.add(engineMode);
	}
};

struct VoiceCtrl {
	int32_t ampmod{};
	int32_t velmod{};
	int32_t ratemod{};
};

inline const uint8_t velocity_data[64] = {
    0,   70,  86,  97,  106, 114, 121, 126, 132, 138, 142, 148, 152, 156, 160, 163, 166, 170, 173, 174, 178, 181,
    184, 186, 189, 190, 194, 196, 198, 200, 202, 205, 206, 209, 211, 214, 216, 218, 220, 222, 224, 225, 227, 229,
    230, 232, 233, 235, 237, 238, 240, 241, 242, 243, 244, 246, 246, 248, 249, 250, 251, 252, 253, 254};

inline int ScaleVelocity(int velocity, int sensitivity) {
	int clamped_vel = std::max(0, std::min(127, velocity));
	int vel_value = velocity_data[clamped_vel >> 1] - 239;
	int scaled_vel = ((sensitivity * vel_value + 7) >> 3) << 4;
	return scaled_vel;
}

inline int ScaleRate(int midinote, int sensitivity) {
	int x = std::min(31, std::max(0, midinote / 3 - 7));
	int qratedelta = (sensitivity * x) >> 3;
	return qratedelta;
}

inline const uint8_t exp_scale_data[] = {0,  1,  2,  3,  4,  5,  6,   7,   8,   9,   11,  14,  16,  19,  23,  27, 33,
                                         39, 47, 56, 66, 80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250};

inline int ScaleCurve(int group, int depth, int curve) {
	int scale;
	if (curve == 0 || curve == 3) {
		scale = (group * depth * 329) >> 12;
	}
	else {
		int n_scale_data = sizeof(exp_scale_data);
		int raw_exp = exp_scale_data[std::min(group, n_scale_data - 1)];
		scale = (raw_exp * depth * 329) >> 15;
	}
	if (curve < 2) {
		scale = -scale;
	}
	return scale;
}

inline int ScaleLevel(int midinote, int break_pt, int left_depth, int right_depth, int left_curve, int right_curve) {
	int offset = midinote - break_pt - 17;
	if (offset >= 0) {
		return ScaleCurve((offset + 1) / 3, right_depth, right_curve);
	}
	else {
		return ScaleCurve(-(offset - 1) / 3, left_depth, left_curve);
	}
}

/// @brief DxVoice (edits 1, 5, 9, 10).
template <class Noise, class Cores = ReferenceCores>
struct Voice {
	const Tables* tables_ = nullptr;
	Cores cores_{};
	Env env_[6]{}; // NOLINT(*-avoid-c-arrays): verbatim
	PitchEnv pitchenv_{};
	int32_t phase[6]{};      // NOLINT(*-avoid-c-arrays): verbatim
	int32_t gain_out[6]{};   // NOLINT(*-avoid-c-arrays): verbatim
	int32_t basepitch_[6]{}; // NOLINT(*-avoid-c-arrays): verbatim
	int32_t fb_buf_[2]{};    // NOLINT(*-avoid-c-arrays): verbatim
	uint32_t delaystate_{};
	uint32_t delayinc_{};
	uint32_t delayinc2_{};
	int16_t detune_per_voice[6]{}; // NOLINT(*-avoid-c-arrays): verbatim
	uint8_t* patch{};
	int random_detune_scale{};
	uint8_t lastVelocity{};
	FmOpParams params_[6]{}; // NOLINT(*-avoid-c-arrays): compute's local params, kept for specs to read

	EnvParams& env_p(int op) { return *(EnvParams*)&patch[op * 21]; }
	EnvParams& pitchenv_p() { return *(EnvParams*)&patch[126]; }

	DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC int32_t osc_freq(int logFreq_for_detune, int mode, int coarse, int fine,
	                                                        int detune, int random_detune) {
		int32_t logfreq;
		if (mode == 0) {
			logfreq = 0;

			double detuneRatio = 0.0209 * exp(-0.396 * (((float)logFreq_for_detune) / (1 << 24))) / 7;
			int random_scaled = (random_detune * random_detune_scale) >> (17);
			logfreq += detuneRatio * logFreq_for_detune * (detune - 7 + random_scaled);

			logfreq += coarsemul[coarse & 31];
			if (fine) {
				logfreq += (int32_t)floor(24204406.323123 * log(1 + 0.01 * fine) + 0.5);
			}
		}
		else {
			logfreq = (4458616 * ((coarse & 3) * 100 + fine)) >> 3;
			logfreq += detune > 7 ? 13457 * (detune - 7) : 0;
		}
		return logfreq;
	}

	void init(const Tables& tables, Patch& newp, int midinote, int velocity) {
		tables_ = &tables;
		patch = newp.params;
		random_detune_scale = newp.random_detune;
		lastVelocity = velocity;

		int logFreq = dxNoteToFreq(midinote);

		for (int op = 0; op < 6; op++) {
			int off = op * 21;
			int outlevel = patch[off + 16];
			outlevel = Env::scaleoutlevel(outlevel);
			int level_scaling =
			    ScaleLevel(midinote, patch[off + 8], patch[off + 9], patch[off + 10], patch[off + 11], patch[off + 12]);
			outlevel += level_scaling;
			outlevel = std::min(127, outlevel);
			outlevel = outlevel << 5;
			outlevel += ScaleVelocity(velocity, patch[off + 15]);
			outlevel = std::max(0, outlevel);
			int rate_scaling = ScaleRate(midinote, patch[off + 13]);
			env_[op].init(env_p(op), outlevel, rate_scaling);

			int mode = patch[op * 21 + 17];
			int coarse = patch[off + 18];
			int fine = patch[off + 19];
			int detune = patch[off + 20];

			detune_per_voice[op] = Noise::render() >> 16;

			int32_t freq = osc_freq(logFreq, mode, coarse, fine, detune, detune_per_voice[op]);
			basepitch_[op] = freq;
		}
		pitchenv_.set(pitchenv_p());

		if (patch[141]) {
			newp.lfo_phase = (1U << 31) - 1;
		}

		if (patch[136]) {
			oscSync();
		}
		else {
			oscUnSync();
		}

		int a = 99 - patch[138]; // LFO delay
		if (a == 99) {
			delayinc_ = ~0u;
			delayinc2_ = ~0u;
		}
		else {
			a = (16 + (a & 15)) << (1 + (a >> 4));
			delayinc_ = lfo_unit * a;
			a &= 0xff80;
			a = std::max(0x80, a);
			delayinc2_ = lfo_unit * a;
		}

		delaystate_ = 0;
	}

	int32_t getdelay(int n) {
		uint32_t delta = delaystate_ < (1U << 31) ? delayinc_ : delayinc2_;
		uint64_t d = ((uint64_t)delaystate_) + delta * n;
		if (d > ~0u) {
			return 1 << 24;
		}
		delaystate_ = d;
		if (d < (1U << 31)) {
			return 0;
		}
		else {
			return (d >> 7) & ((1 << 24) - 1);
		}
	}

	DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC bool compute(int32_t* buf, int n, int base_pitch, const Patch* ctrls,
	                                                    const VoiceCtrl* voice_ctrls) {
		int32_t lfo_delay = getdelay(n);
		int32_t lfo_val = ctrls->lfo_value;

		// ==== PITCH ====
		int pitchmoddepth = (patch[139] * 165) >> 6;
		int pitchmodsens = pitchmodsenstab[patch[143] & 7];
		uint32_t pmd = pitchmoddepth * lfo_delay; // Q32
		int32_t senslfo = pitchmodsens * (lfo_val - (1 << 23));
		int32_t pmod_1 = (((int64_t)pmd) * (int64_t)senslfo) >> 39;
		pmod_1 = abs(pmod_1);
		int32_t pmod_2 = 0;
		int32_t pitch_mod = std::max(pmod_1, pmod_2);
		pitch_mod = pitchenv_.getsample(pitchenv_p(), n) + (pitch_mod * (senslfo < 0 ? -1 : 1));

		pitch_mod += base_pitch;

		// ==== AMP MOD ====
		lfo_val = (1 << 24) - lfo_val;
		int ampmoddepth = (patch[140] * 165) >> 6;
		uint32_t amod_1 = (uint32_t)(((int64_t)ampmoddepth * (int64_t)lfo_delay) >> 8); // Q24 :D
		amod_1 = (uint32_t)(((int64_t)amod_1 * (int64_t)lfo_val) >> 24);
		int amp_mod_source = 0;
		uint32_t amod_2 = (uint32_t)(((int64_t)amp_mod_source * (int64_t)lfo_val) >> 7);
		uint32_t amd_mod = std::max(amod_1, amod_2);

		// ==== EG AMP MOD ====
		uint32_t amod_3 = (ctrls->eg_mod + 1) << 17;
		amd_mod = std::max((1 << 24) - amod_3, amd_mod);

		FmOpParams* params = params_;
		// ==== OP RENDER ====
		for (int op = 0; op < 6; op++) {
			params[op].phase = phase[op];
			params[op].gain_out = gain_out[op];
			if (!ctrls->opSwitch(op)) {
				env_[op].getsample(env_p(op), n, 0); // advance the envelop even if it is not playing
				params[op].level_in = 0;
				params[op].freq = 0;
			}
			else {
				int off = op * 21;

				int mode = patch[off + 17];
				if (mode)
					params[op].freq = freq_lookup(*tables_, basepitch_[op]);
				else
					params[op].freq = freq_lookup(*tables_, basepitch_[op] + pitch_mod);

				int32_t level = env_[op].getsample(env_p(op), n, voice_ctrls->ratemod);
				int ampmodsens = ampmodsenstab[patch[off + 14] & 3];
				if (ampmodsens != 0) {
					uint32_t sensamp = (uint32_t)(((uint64_t)amd_mod) * ((uint64_t)ampmodsens) >> 24);

					uint32_t pt = exp(((float)sensamp) / 262144 * 0.07 + 12.2);
					uint32_t ldiff = (uint32_t)(((uint64_t)level) * (((uint64_t)pt << 4)) >> 28);
					level -= ldiff;

					level += ((ampmodsens >> 16) * voice_ctrls->ampmod);
				}
				level += patch[off + 15] * voice_ctrls->velmod;
				params[op].level_in = level;
			}
		}
		int algorithm = patch[134];
		int feedback = patch[135];
		int fb_shift = feedback != 0 ? FEEDBACK_BITDEPTH - feedback : 16;
		if (ctrls->mki) {
			cores_.mki.render(*tables_, buf, n, params, algorithm, fb_buf_, fb_shift);
		}
		else {
			cores_.modern.render(*tables_, buf, n, params, algorithm, fb_buf_, fb_shift);
		}
		bool any_active_op = false;
		for (int op = 0; op < 6; op++) {
			phase[op] = params[op].phase;
			gain_out[op] = params[op].gain_out;
			if (params[op].gain_out >= kGainLevelThresh) {
				any_active_op = true;
			}
		}

		// if key is down, operators might become active soon
		return pitchenv_.isDown() || any_active_op;
	}

	void keyup() {
		for (int op = 0; op < 6; op++) {
			env_[op].keydown(env_p(op), false);
		}
		pitchenv_.keydown(pitchenv_p(), false);
	}

	DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC void update(Patch& newp, int midinote) {
		patch = newp.params;
		random_detune_scale = newp.random_detune;

		int logFreq = dxNoteToFreq(midinote);
		int velocity = lastVelocity;
		for (int op = 0; op < 6; op++) {
			int off = op * 21;
			int mode = patch[off + 17];
			int coarse = patch[off + 18];
			int fine = patch[off + 19];
			int detune = patch[off + 20];
			basepitch_[op] = osc_freq(logFreq, mode, coarse, fine, detune, detune_per_voice[op]);

			int outlevel = patch[off + 16];
			outlevel = Env::scaleoutlevel(outlevel);
			int level_scaling =
			    ScaleLevel(midinote, patch[off + 8], patch[off + 9], patch[off + 10], patch[off + 11], patch[off + 12]);
			outlevel += level_scaling;
			outlevel = std::min(127, outlevel);
			outlevel = outlevel << 5;
			outlevel += ScaleVelocity(velocity, patch[off + 15]);
			outlevel = std::max(0, outlevel);
			int rate_scaling = ScaleRate(midinote, patch[off + 13]);
			env_[op].update(env_p(op), outlevel, rate_scaling);
		}
	}

	void oscSync() {
		for (int i = 0; i < 6; i++) {
			gain_out[i] = 0;
			phase[i] = 0;
		}
	}

	void oscUnSync() {
		for (int i = 0; i < 6; i++) {
			gain_out[i] = 0;
			phase[i] = Noise::render();
		}
	}

	void digest(Digest& d) const {
		for (const Env& e : env_) {
			e.digest(d);
		}
		pitchenv_.digest(d);
		for (int op = 0; op < 6; op++) {
			d.add(phase[op]);
			d.add(gain_out[op]);
			d.add(basepitch_[op]);
			d.add(detune_per_voice[op]);
		}
		d.add(fb_buf_[0]);
		d.add(fb_buf_[1]);
		d.add(delaystate_);
		d.add(delayinc_);
		d.add(delayinc2_);
		d.add(static_cast<int32_t>(random_detune_scale));
		d.add(lastVelocity);
	}
};

/// @brief The adapter's pitch: render's (int)(log2f(phaseIncrement) * (1 << 24)) - 278023814.
inline int32_t adapter_pitch(uint32_t phaseIncrement) {
	int logpitch = (int)(log2f(phaseIncrement) * (1 << 24));
	int adjpitch = logpitch - 278023814;
	return adjpitch;
}

} // namespace dx7_reference
