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

#include "dsp-next/core/control.h"
#include "dsp-next/fixed/dx7/envelope.h"
#include "dsp-next/fixed/dx7/lfo.h"
#include "dsp-next/fixed/dx7/lfo_delay.h"
#include "dsp-next/fixed/dx7/operator.h"
#include "dsp-next/fixed/dx7/pitch_envelope.h"
#include "modern_asm.h"
#include <memory>

class DxPatch {
public:
	DxPatch();

	uint8_t params[156];
	/// The engine the voices render in, chosen by setEngineMode.
	deluge::dsp::fixed::dx7::Engine engine;
	bool opSwitch(int op) const { return (params[155] >> op) & 1; }
	void setOpSwitch(int op, bool on) { params[155] = (params[155] & ~(1 << op)) | (on ? 1 : 0) * (1 << op); }
	uint8_t engineMode;

	// extra parameters
	int32_t random_detune;

	// TODO: these should be per voice with MPE
	// int amp_mod;
	int pitch_mod;
	int eg_mod;

	/// The patch's LFO, shared by its voices; configured from the patch before each block.
	deluge::dsp::Control<deluge::dsp::fixed::dx7::Lfo> lfo;
	/// The LFO's value after the last block, Q24.
	int32_t lfo_value = 0;

	void computeLfo(int n);

	void setEngineMode(int mode);
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

	void keyup();

	// TODO: some way of indicating end-of-note. Maybe should be a return
	// value from the compute method? (Having a count return from keyup
	// is also tempting, but if there's a dynamic parameter change after
	// keyup, that won't work.

	// PG:add the update
	void update(DxPatch& p, int midinote);
	void oscSync();
	void oscUnSync();

	int32_t osc_freq(int log_freq, int mode, int coarse, int fine, int detune, int random_detune);

private:
	/// Each op's envelope, configured from the patch before each block and each event.
	deluge::dsp::Control<deluge::dsp::fixed::dx7::Envelope> env_[6];
	/// Each op's scaled output level in microsteps, set at note-on and retune.
	int32_t outlevel_[6];
	/// Each op's rate scaling in qRate units, set at note-on and retune.
	int32_t rate_scaling_[6];
	/// Configured from the patch before each block and each event, as the patch is edited live.
	deluge::dsp::Control<deluge::dsp::fixed::dx7::PitchEnvelope> pitchenv_;
	/// The six operators' phases and gains and the feedback history; the per-block settings arrive with each block.
	DxGraph graph_;
	int32_t basepitch_[6];

	/// Configured at note-on only: the voice reads the patch's LFO delay once per note.
	deluge::dsp::Control<deluge::dsp::fixed::dx7::LfoDelay> lfo_delay_;

	int16_t detune_per_voice[6];

	uint8_t* patch; // raw patch, same as p->currentPatch
	int random_detune_scale;
	uint8_t lastVelocity;

	/// @brief Op @p op's envelope settings: its patch bytes, its output level and rate scaling, and @p extra_rate.
	[[nodiscard]] deluge::dsp::fixed::dx7::Envelope::Settings env_settings(int op, int32_t extra_rate) const {
		const uint8_t* p = &patch[op * 21];
		return {.params = {{p[0], p[1], p[2], p[3]}, {p[4], p[5], p[6], p[7]}},
		        .output_level = outlevel_[op],
		        .rate_scaling = rate_scaling_[op],
		        .extra_rate = extra_rate};
	}
	/// @brief The patch's pitch envelope bytes (126 to 133).
	[[nodiscard]] deluge::dsp::fixed::dx7::EnvelopeParams pitch_params() const {
		return {{patch[126], patch[127], patch[128], patch[129]}, {patch[130], patch[131], patch[132], patch[133]}};
	}

public:
	DxVoice* nextUnassigned;
	bool preallocated;
};

#endif // SYNTH_DX7NOTE_H_
