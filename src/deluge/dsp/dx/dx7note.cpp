/*
 * Copyright 2016-2017 Pascal Gauthier.
 * Copyright 2012 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "dx7note.h"
#include "engine.h"
#include "math_lut.h"
#include "util/functions.h"
#include <algorithm>
#include <cmath>
#include <math.h>
#include <stdlib.h>

using std::max;
using std::min;

const int FEEDBACK_BITDEPTH = 8;

const int32_t coarsemul[] = {-16777216, 0,        16777216, 26591258, 33554432, 38955489, 43368474, 47099600,
                             50331648,  53182516, 55732705, 58039632, 60145690, 62083076, 63876816, 65546747,
                             67108864,  68576247, 69959732, 71268397, 72509921, 73690858, 74816848, 75892776,
                             76922906,  77910978, 78860292, 79773775, 80654032, 81503396, 82323963, 83117622};

// 0, 66, 109, 255
static const uint32_t ampmodsenstab[] = {0, 4342338, 7171437, 16777216};

static const uint8_t pitchmodsenstab[] = {0, 10, 20, 33, 55, 92, 153, 255};

static const uint8_t init_voice[] = {
    99, 99, 99, 99, 99, 99, 99, 00, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  0,  7,  99, 99, 99, 99, 99,
    99, 99, 00, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  0,  7,  99, 99, 99, 99, 99, 99, 99, 00, 0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  1,  0,  7,  99, 99, 99, 99, 99, 99, 99, 00, 0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  1,  0,  7,  99, 99, 99, 99, 99, 99, 99, 00, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  0,
    7,  99, 99, 99, 99, 99, 99, 99, 00, 0,  0,  0,  0,  0,  0,  0,  0,  99, 0,  1,  0,  7,  99, 99, 99, 99,
    50, 50, 50, 50, 0,  0,  1,  35, 0,  0,  0,  1,  0,  3,  24, 73, 78, 73, 84, 32, 86, 79, 73, 67, 69, 63};

static int dxNoteToFreq(int note) {
	const int base = 50857777; // (1 << 24) * (log(440) / log(2) - 69/12)
	const int step = (1 << 24) / 12;
	return base + step * note;
}

DxPatch::DxPatch() {
	memcpy(params, init_voice, sizeof params);
	// amp_mod = 0;
	// pitch_mod = 0;
	eg_mod = 127;
	random_detune = 0;

	setEngineMode(0);
}

void DxPatch::updateEngineMode() {
	core = &dxEngine->engineModern;
	if (engineMode == 2) {
		core = &dxEngine->engineMkI;
	}
	else if (engineMode == 0) {
		int algo = params[134];
		int feedback = params[135];
		// oly engineMkI implements the feedback loops of algo 4 and 6
		if (feedback > 0 && (algo == 3 || algo == 5)) {
			core = &dxEngine->engineMkI;
		}
	}
}

void DxPatch::setEngineMode(int mode, bool neon) {
	// TODO: neon should be the only option for modern in release :P
	dxEngine->engineModern.neon = neon;

	engineMode = (uint8_t)mode;

	updateEngineMode();
}

static int32_t lfoPhaseToValue(int32_t phase_, int waveform) {
	int32_t x;
	// TODO: triangle no worky?
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
		return (1 << 23) + (Sin::lookup(phase_ >> 8) >> 1);
	default:
		return 1 << 23;
	}
}

constexpr int32_t lfo_unit = (int32_t)(25190424 / (float)44100 + 0.5);
void DxPatch::computeLfo(int n) {
	int rate = params[137]; // 0..99
	int sr = rate == 0 ? 1 : (165 * rate) >> 6;
	sr *= sr < 160 ? 11 : (11 + ((sr - 160) >> 4));
	lfo_delta = lfo_unit * sr;

	lfo_phase += n * lfo_delta;
	int waveform_ = params[142];
	if (waveform_ == 5) {
		// TODO: s&h
		// likely we will do that per voice just because™
	}
	lfo_value = lfoPhaseToValue(lfo_phase, waveform_);
}

int32_t DxVoice::osc_freq(int logFreq_for_detune, int mode, int coarse, int fine, int detune, int random_detune) {

	int32_t logfreq;
	if (mode == 0) {
		logfreq = 0;

		// could use more precision, closer enough for now. those numbers comes from my DX7
		// // TODO: in our flow detune should not happen here at all, but figuring it out is going to require doing MAFF
		double detuneRatio = 0.0209 * exp(-0.396 * (((float)logFreq_for_detune) / (1 << 24))) / 7;
		int random_scaled =
		    (random_detune * random_detune_scale) >> (17); // the magic is just this seems a reasonable range
		logfreq += detuneRatio * logFreq_for_detune * (detune - 7 + random_scaled);

		logfreq += coarsemul[coarse & 31];
		if (fine) {
			// (1 << 24) / log(2)
			logfreq += (int32_t)floor(24204406.323123 * log(1 + 0.01 * fine) + 0.5);
		}

		// // This was measured at 7.213Hz per count at 9600Hz, but the exact
		// // value is somewhat dependent on midinote. Close enough for now.
		// //logfreq += 12606 * (detune -7);
	}
	else {
		// ((1 << 24) * log(10) / log(2) * .01) << 3
		logfreq = (4458616 * ((coarse & 3) * 100 + fine)) >> 3;
		logfreq += detune > 7 ? 13457 * (detune - 7) : 0;
	}
	return logfreq;
}

const uint8_t velocity_data[64] = {0,   70,  86,  97,  106, 114, 121, 126, 132, 138, 142, 148, 152, 156, 160, 163,
                                   166, 170, 173, 174, 178, 181, 184, 186, 189, 190, 194, 196, 198, 200, 202, 205,
                                   206, 209, 211, 214, 216, 218, 220, 222, 224, 225, 227, 229, 230, 232, 233, 235,
                                   237, 238, 240, 241, 242, 243, 244, 246, 246, 248, 249, 250, 251, 252, 253, 254};

// See "velocity" section of notes. Returns velocity delta in microsteps.
int ScaleVelocity(int velocity, int sensitivity) {
	int clamped_vel = max(0, min(127, velocity));
	int vel_value = velocity_data[clamped_vel >> 1] - 239;
	int scaled_vel = ((sensitivity * vel_value + 7) >> 3) << 4;
	return scaled_vel;
}

int ScaleRate(int midinote, int sensitivity) {
	int x = min(31, max(0, midinote / 3 - 7));
	int qratedelta = (sensitivity * x) >> 3;
#ifdef SUPER_PRECISE
	int rem = x & 7;
	if (sensitivity == 3 && rem == 3) {
		qratedelta -= 1;
	}
	else if (sensitivity == 7 && rem > 0 && rem < 4) {
		qratedelta += 1;
	}
#endif
	return qratedelta;
}

const uint8_t exp_scale_data[] = {0,  1,  2,  3,  4,  5,  6,   7,   8,   9,   11,  14,  16,  19,  23,  27, 33,
                                  39, 47, 56, 66, 80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250};

int ScaleCurve(int group, int depth, int curve) {
	int scale;
	if (curve == 0 || curve == 3) {
		// linear
		scale = (group * depth * 329) >> 12;
	}
	else {
		// exponential
		int n_scale_data = sizeof(exp_scale_data);
		int raw_exp = exp_scale_data[min(group, n_scale_data - 1)];
		scale = (raw_exp * depth * 329) >> 15;
	}
	if (curve < 2) {
		scale = -scale;
	}
	return scale;
}

int ScaleLevel(int midinote, int break_pt, int left_depth, int right_depth, int left_curve, int right_curve) {
	int offset = midinote - break_pt - 17;
	if (offset >= 0) {
		return ScaleCurve((offset + 1) / 3, right_depth, right_curve);
	}
	else {
		return ScaleCurve(-(offset - 1) / 3, left_depth, left_curve);
	}
}

DxVoice::DxVoice() {
	preallocated = false;
	for (int op = 0; op < 6; op++) {
		phase[op] = 0;
		gain_out[op] = 0;
	}
}

// TODO: recalculate Scale() using logfreq
void DxVoice::init(DxPatch& newp, int midinote, int velocity) {
	patch = newp.params;
	random_detune_scale = newp.random_detune;
	lastVelocity = velocity;

	// TODO: this is not used for the base pitch, but it is used for calculating the detune ratios.
	// We might want to base that on note+transpose+masterTranspose, or more likely, completely
	// change over how "detune" is calculated (post-LUT)
	int logFreq = dxNoteToFreq(midinote);

	for (int op = 0; op < 6; op++) {
		int off = op * 21;
		int outlevel = patch[off + 16];
		outlevel = Env::scaleoutlevel(outlevel);
		int level_scaling =
		    ScaleLevel(midinote, patch[off + 8], patch[off + 9], patch[off + 10], patch[off + 11], patch[off + 12]);
		outlevel += level_scaling;
		outlevel = min(127, outlevel);
		outlevel = outlevel << 5;
		outlevel += ScaleVelocity(velocity, patch[off + 15]);
		outlevel = max(0, outlevel);
		int rate_scaling = ScaleRate(midinote, patch[off + 13]);
		env_[op].init(env_p(op), outlevel, rate_scaling);

		int mode = patch[op * 21 + 17];
		int coarse = patch[off + 18];
		int fine = patch[off + 19];
		int detune = patch[off + 20];

		detune_per_voice[op] = getNoise() >> 16;

		int32_t freq = osc_freq(logFreq, mode, coarse, fine, detune, detune_per_voice[op]);
		basepitch_[op] = freq;
	}
	pitchenv_.set(pitchenv_p());

	// TODO: in LFO sync mode it would be best with LFO per voice
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
		a = max(0x80, a);
		delayinc2_ = lfo_unit * a;
	}

	delaystate_ = 0;
}

int32_t DxVoice::getdelay(int n) {
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

bool DxVoice::compute(int32_t* buf, int n, int base_pitch, const DxPatch* ctrls, const DxVoiceCtrl* voice_ctrls) {
	// assert(n <= DX_MAX_N);
	// LFO delay
	int32_t lfo_delay = getdelay(n);
	int32_t lfo_val = ctrls->lfo_value;

	// ==== PITCH ====
	int pitchmoddepth = (patch[139] * 165) >> 6;
	int pitchmodsens = pitchmodsenstab[patch[143] & 7];
	uint32_t pmd = pitchmoddepth * lfo_delay; // Q32
	int32_t senslfo = pitchmodsens * (lfo_val - (1 << 23));
	int32_t pmod_1 = (((int64_t)pmd) * (int64_t)senslfo) >> 39;
	pmod_1 = abs(pmod_1);
	// int32_t pmod_2 = (int32_t)(((int64_t)ctrls->pitch_mod * (int64_t)senslfo) >> 14);
	// pmod_2 = abs(pmod_2);
	int32_t pmod_2 = 0;
	int32_t pitch_mod = max(pmod_1, pmod_2);
	pitch_mod = pitchenv_.getsample(pitchenv_p(), n) + (pitch_mod * (senslfo < 0 ? -1 : 1));

	pitch_mod += base_pitch;

	// ==== AMP MOD ====
	lfo_val = (1 << 24) - lfo_val;
	int ampmoddepth = (patch[140] * 165) >> 6;
	uint32_t amod_1 = (uint32_t)(((int64_t)ampmoddepth * (int64_t)lfo_delay) >> 8); // Q24 :D
	amod_1 = (uint32_t)(((int64_t)amod_1 * (int64_t)lfo_val) >> 24);
	int amp_mod_source = 0;                                                          // patch cable in here plz
	uint32_t amod_2 = (uint32_t)(((int64_t)amp_mod_source * (int64_t)lfo_val) >> 7); // Q?? :|
	uint32_t amd_mod = max(amod_1, amod_2);

	// ==== EG AMP MOD ====
	uint32_t amod_3 = (ctrls->eg_mod + 1) << 17;
	amd_mod = max((1 << 24) - amod_3, amd_mod);

	FmOpParams params[6];
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
			// int32_t gain = pow(2, 10 + level * (1.0 / (1 << 24)));

			int mode = patch[off + 17];
			if (mode)
				params[op].freq = Freqlut::lookup(basepitch_[op]);
			else
				params[op].freq = Freqlut::lookup(basepitch_[op] + pitch_mod);

			// TODO: set ratemod sensitivity per operator
			int32_t level = env_[op].getsample(env_p(op), n, voice_ctrls->ratemod);
			int ampmodsens = ampmodsenstab[patch[off + 14] & 3];
			if (ampmodsens != 0) {
				uint32_t sensamp = (uint32_t)(((uint64_t)amd_mod) * ((uint64_t)ampmodsens) >> 24);

				// TODO: mehhh.. this needs some real tuning.
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
	ctrls->core->render(buf, n, params, algorithm, fb_buf_, fb_shift);
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

void DxVoice::keyup() {
	for (int op = 0; op < 6; op++) {
		env_[op].keydown(env_p(op), false);
	}
	pitchenv_.keydown(pitchenv_p(), false);
}

void DxVoice::updateBasePitches(int logFreq_for_detune) {
	for (int op = 0; op < 6; op++) {
		int off = op * 21;
		int mode = patch[off + 17];
		int coarse = patch[off + 18];
		int fine = patch[off + 19];
		int detune = patch[off + 20];
		basepitch_[op] = osc_freq(logFreq_for_detune, mode, coarse, fine, detune, detune_per_voice[op]);
	}
}

// TODO: can share yet more codes with ::init()
void DxVoice::update(DxPatch& newp, int midinote) {
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
		outlevel = min(127, outlevel);
		outlevel = outlevel << 5;
		outlevel += ScaleVelocity(velocity, patch[off + 15]);
		outlevel = max(0, outlevel);
		int rate_scaling = ScaleRate(midinote, patch[off + 13]);
		env_[op].update(env_p(op), outlevel, rate_scaling);
	}

	// TODO: did we the pitchenv?
}

/**
 * Used in monophonic mode to transfer voice state from different notes
 */
void DxVoice::transferState(DxVoice& src) {
	for (int i = 0; i < 6; i++) {
		env_[i].transfer(src.env_[i]);
		gain_out[i] = src.gain_out[i];
		phase[i] = src.phase[i];
	}
}

void DxVoice::transferSignal(DxVoice& src) {
	for (int i = 0; i < 6; i++) {
		gain_out[i] = src.gain_out[i];
		phase[i] = src.phase[i];
	}
}

void DxVoice::oscSync() {
	for (int i = 0; i < 6; i++) {
		gain_out[i] = 0;
		phase[i] = 0;
	}
}

void DxVoice::oscUnSync() {
	for (int i = 0; i < 6; i++) {
		gain_out[i] = 0;
		phase[i] = getNoise();
	}
}
