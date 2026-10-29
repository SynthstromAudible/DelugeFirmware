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
#include "engines/deluge/dx7_init_voice.h"
#include "math_lut.h"
#include "util/functions.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <math.h>
#include <stdlib.h>
#include <tuple>

using std::max;
using std::min;

const int32_t coarsemul[] = {-16777216, 0,        16777216, 26591258, 33554432, 38955489, 43368474, 47099600,
                             50331648,  53182516, 55732705, 58039632, 60145690, 62083076, 63876816, 65546747,
                             67108864,  68576247, 69959732, 71268397, 72509921, 73690858, 74816848, 75892776,
                             76922906,  77910978, 78860292, 79773775, 80654032, 81503396, 82323963, 83117622};

// 0, 66, 109, 255
static const uint32_t ampmodsenstab[] = {0, 4342338, 7171437, 16777216};

static const uint8_t pitchmodsenstab[] = {0, 10, 20, 33, 55, 92, 153, 255};

static int dxNoteToFreq(int note) {
	const int base = 50857777; // (1 << 24) * (log(440) / log(2) - 69/12)
	const int step = (1 << 24) / 12;
	return base + step * note;
}

DxPatch::DxPatch() {
	memcpy(params, deluge::instrument::kDx7InitVoice.data(), sizeof params);
	// amp_mod = 0;
	// pitch_mod = 0;
	eg_mod = 127;
	random_detune = 0;

	setEngineMode(0);
}

void DxPatch::updateEngineMode() {
	using deluge::dsp::fixed::dx7::Engine;
	engine = Engine::MODERN;
	if (engineMode == 2) {
		engine = Engine::MKI;
	}
	else if (engineMode == 0) {
		int algo = params[134];
		int feedback = params[135];
		// only MkI implements the feedback loops of algo 4 and 6
		if (feedback > 0 && (algo == 3 || algo == 5)) {
			engine = Engine::MKI;
		}
	}
}

void DxPatch::setEngineMode(int mode) {
	engineMode = (uint8_t)mode;

	updateEngineMode();
}

void DxPatch::computeLfo(int n) {
	lfo.configure({params[137], params[142], &dxEngine->tables});
	lfo_value = lfo.advance(static_cast<std::size_t>(n));
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
		outlevel = deluge::dsp::fixed::dx7::Envelope::scale_output_level(outlevel);
		int level_scaling =
		    ScaleLevel(midinote, patch[off + 8], patch[off + 9], patch[off + 10], patch[off + 11], patch[off + 12]);
		outlevel += level_scaling;
		outlevel = min(127, outlevel);
		outlevel = outlevel << 5;
		outlevel += ScaleVelocity(velocity, patch[off + 15]);
		outlevel = max(0, outlevel);
		int rate_scaling = ScaleRate(midinote, patch[off + 13]);
		outlevel_[op] = outlevel;
		rate_scaling_[op] = rate_scaling;
		env_[op].configure(env_settings(op, 0));
		env_[op].on<&deluge::dsp::fixed::dx7::Envelope::start>();

		int mode = patch[op * 21 + 17];
		int coarse = patch[off + 18];
		int fine = patch[off + 19];
		int detune = patch[off + 20];

		detune_per_voice[op] = getNoise() >> 16;

		int32_t freq = osc_freq(logFreq, mode, coarse, fine, detune, detune_per_voice[op]);
		basepitch_[op] = freq;
	}
	pitchenv_.configure({pitch_params()});
	pitchenv_.on<&deluge::dsp::fixed::dx7::PitchEnvelope::start>();

	// TODO: in LFO sync mode it would be best with LFO per voice
	if (patch[141]) {
		newp.lfo.on<&deluge::dsp::fixed::dx7::Lfo::key_sync>();
	}

	if (patch[136]) {
		oscSync();
	}
	else {
		oscUnSync();
	}

	lfo_delay_.configure({patch[138]});
	lfo_delay_.reset();
}

bool DxVoice::compute(int32_t* buf, int n, int base_pitch, const DxPatch* ctrls, const DxVoiceCtrl* voice_ctrls) {
	// assert(n <= deluge::dsp::fixed::dx7::kMaxBlock);
	// LFO delay
	int32_t lfo_delay = lfo_delay_.advance(static_cast<std::size_t>(n));
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
	pitchenv_.configure({pitch_params()});
	pitch_mod = pitchenv_.advance(static_cast<std::size_t>(n)) + (pitch_mod * (senslfo < 0 ? -1 : 1));

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

	DxGraph::Settings settings{
	    .engine = ctrls->engine, .algorithm = patch[134], .feedback = patch[135], .tables = &dxEngine->tables};
	// ==== OP RENDER ====
	for (int op = 0; op < 6; op++) {
		if (!ctrls->opSwitch(op)) {
			// advance the envelope even if it is not playing
			env_[op].configure(env_settings(op, 0));
			std::ignore = env_[op].advance(static_cast<std::size_t>(n));
			settings.level_in[op] = 0;
			settings.freq[op] = 0;
		}
		else {
			int off = op * 21;
			// int32_t gain = pow(2, 10 + level * (1.0 / (1 << 24)));

			int mode = patch[off + 17];
			if (mode)
				settings.freq[op] = Freqlut::lookup(basepitch_[op]);
			else
				settings.freq[op] = Freqlut::lookup(basepitch_[op] + pitch_mod);

			// TODO: set ratemod sensitivity per operator
			env_[op].configure(env_settings(op, voice_ctrls->ratemod));
			int32_t level = env_[op].advance(static_cast<std::size_t>(n));
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
			settings.level_in[op] = level;
		}
	}
	graph_.renderBlock(settings, {buf, static_cast<std::size_t>(n)}, dxEngine->buses());
	bool any_active_op = false;
	for (int op = 0; op < 6; op++) {
		if (graph_.gain_out(op) >= deluge::dsp::fixed::dx7::kGainLevelThresh) {
			any_active_op = true;
		}
	}

	// if key is down, operators might become active soon
	return pitchenv_.state().down || any_active_op;
}

void DxVoice::keyup() {
	for (int op = 0; op < 6; op++) {
		env_[op].configure(env_settings(op, 0));
		env_[op].on<&deluge::dsp::fixed::dx7::Envelope::release>();
	}
	pitchenv_.configure({pitch_params()});
	pitchenv_.on<&deluge::dsp::fixed::dx7::PitchEnvelope::release>();
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
		outlevel = deluge::dsp::fixed::dx7::Envelope::scale_output_level(outlevel);
		int level_scaling =
		    ScaleLevel(midinote, patch[off + 8], patch[off + 9], patch[off + 10], patch[off + 11], patch[off + 12]);
		outlevel += level_scaling;
		outlevel = min(127, outlevel);
		outlevel = outlevel << 5;
		outlevel += ScaleVelocity(velocity, patch[off + 15]);
		outlevel = max(0, outlevel);
		int rate_scaling = ScaleRate(midinote, patch[off + 13]);
		outlevel_[op] = outlevel;
		rate_scaling_[op] = rate_scaling;
		env_[op].configure(env_settings(op, 0));
		env_[op].on<&deluge::dsp::fixed::dx7::Envelope::retune>();
	}

	// TODO: did we the pitchenv?
}

void DxVoice::oscSync() {
	graph_.restart(std::array<deluge::dsp::q31, 6>{});
}

void DxVoice::oscUnSync() {
	std::array<deluge::dsp::q31, 6> phases{};
	for (auto& phase : phases) {
		phase = getNoise();
	}
	graph_.restart(phases);
}
