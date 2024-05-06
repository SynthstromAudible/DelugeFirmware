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

#ifndef SYNTH_DX7NOTE_H_
#define SYNTH_DX7NOTE_H_

// This is the logic to put together a note from the MIDI description
// and run the low-level modules.

// It will continue to evolve a bit, as note-stealing logic, scaling,
// and real-time control of parameters live here.

#include "env.h"
#include "fm_core.h"
#include "pitchenv.h"
#include <memory>

struct VoiceStatus {
	uint32_t amp[6];
	char ampStep[6];
	char pitchStep;
};

class DxPatch {
public:
	DxPatch();

	uint8_t params[156];
	FmCore* core;
	bool opSwitch(int op) const { return (params[155] >> op) & 1; }
	void setOpSwitch(int op, bool on) { params[155] = (params[155] & ~(1 << op)) | (on ? 1 : 0) * (1 << op); }
	uint8_t engineMode;

	// extra parameters
	int32_t random_detune;

	// TODO: these should be per voice with MPE
	// int amp_mod;
	int pitch_mod;
	int eg_mod;

	uint32_t lfo_phase; // Q32
	uint32_t lfo_delta;
	uint32_t lfo_value;

	// only needs to be called when rate (param 137) changes
	void updateLfo();
	void computeLfo(int n);

	void setEngineMode(int mode, bool neon = true);
	void updateEngineMode();
};

struct DxVoiceCtrl {
	int32_t ampmod{};
	int32_t velmod{}; // offset to the start velocity
	int32_t ratemod{};

	DxVoiceCtrl() = default;
};

class DxVoice {
public:
	DxVoice();
	void init(DxPatch& p, int midinote, int velocity);
	// Note: this _adds_ to the buffer. Interesting question whether it's
	// worth it...
	bool compute(int32_t* buf, int n, int pitch, const DxPatch* ctrls_patch, const DxVoiceCtrl* ctrls_voice);
	int32_t getdelay(int n);

	void keyup();

	// TODO: some way of indicating end-of-note. Maybe should be a return
	// value from the compute method? (Having a count return from keyup
	// is also tempting, but if there's a dynamic parameter change after
	// keyup, that won't work.

	// PG:add the update
	void update(DxPatch& p, int midinote);
	void updateBasePitches(int logFreq);
	void transferState(DxVoice& src);
	void transferSignal(DxVoice& src);
	void oscSync();
	void oscUnSync();

	int32_t osc_freq(int log_freq, int mode, int coarse, int fine, int detune, int random_detune);

private:
	Env env_[6];
	PitchEnv pitchenv_;
	int32_t phase[6];
	int32_t gain_out[6];
	int32_t basepitch_[6];
	int32_t fb_buf_[2];

	// for LFO
	uint32_t delaystate_;
	uint32_t delayinc_;
	uint32_t delayinc2_;

	int16_t detune_per_voice[6];

	uint8_t* patch; // raw patch, same as p->currentPatch
	int random_detune_scale;
	uint8_t lastVelocity;

	EnvParams& env_p(int op) { return *(EnvParams*)&patch[op * 21]; }
	EnvParams& pitchenv_p() { return *(EnvParams*)&patch[126]; }

public:
	DxVoice* nextUnassigned;
	bool preallocated;
};

#endif // SYNTH_DX7NOTE_H_
