/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "modulation/params/param.h"
#include "gui/l10n/l10n.h"
#include <cstring>

namespace deluge {
namespace modulation {
namespace params {

char const* getPatchedParamDisplayName(int32_t p) {
	using enum l10n::String;
	auto lang = l10n::chosenLanguage;

	// These can basically be 13 chars long, or 14 if the last one is a dot.
	switch (p) {

	//Master Level, Pitch, Pan
	case Param::Local::VOLUME:
		return l10n::get(STRING_FOR_PARAM_LOCAL_VOLUME);

	case Param::Global::VOLUME_POST_FX:
		return l10n::get(STRING_FOR_PARAM_GLOBAL_VOLUME_POST_FX);

	case Param::Local::PAN:
		return l10n::get(STRING_FOR_PARAM_LOCAL_PAN);

	case Param::Local::PITCH_ADJUST:
		return l10n::get(STRING_FOR_PARAM_LOCAL_PITCH_ADJUST);

	//LPF Frequency, Resonance, Morph
	case Param::Local::LPF_FREQ:
		return l10n::get(STRING_FOR_PARAM_LOCAL_LPF_FREQ);

	case Param::Local::LPF_RESONANCE:
		return l10n::get(STRING_FOR_PARAM_LOCAL_LPF_RESONANCE);

	case Param::Local::LPF_MORPH:
		return l10n::get(STRING_FOR_PARAM_LOCAL_LPF_MORPH);

	//HPF Frequency, Resonance, Morph
	case Param::Local::HPF_FREQ:
		return l10n::get(STRING_FOR_PARAM_LOCAL_HPF_FREQ);

	case Param::Local::HPF_MORPH:
		return l10n::get(STRING_FOR_PARAM_LOCAL_HPF_MORPH);

	case Param::Local::HPF_RESONANCE:
		return l10n::get(STRING_FOR_PARAM_LOCAL_HPF_RESONANCE);

	//Reverb Amount
	case Param::Global::REVERB_AMOUNT:
		return l10n::get(STRING_FOR_PARAM_GLOBAL_REVERB_AMOUNT);

	//Delay Rate, Amount
	case Param::Global::DELAY_RATE:
		return l10n::get(STRING_FOR_PARAM_GLOBAL_DELAY_RATE);

	case Param::Global::DELAY_FEEDBACK:
		return l10n::get(STRING_FOR_PARAM_GLOBAL_DELAY_FEEDBACK);

	//Sidechain Level
	case Param::Global::VOLUME_POST_REVERB_SEND:
		return l10n::get(STRING_FOR_PARAM_GLOBAL_VOLUME_POST_REVERB_SEND);

	//Wavefolder
	case Param::Local::FOLD:
		return l10n::get(STRING_FOR_WAVEFOLDER);

	//OSC 1 Level, Pitch, Phase Width, Carrier Feedback, Wave Index
	case Param::Local::OSC_A_VOLUME:
		return l10n::get(STRING_FOR_PARAM_LOCAL_OSC_A_VOLUME);

	case Param::Local::OSC_A_PITCH_ADJUST:
		return l10n::get(STRING_FOR_PARAM_LOCAL_OSC_A_PITCH_ADJUST);

	case Param::Local::OSC_A_PHASE_WIDTH:
		return l10n::get(STRING_FOR_PARAM_LOCAL_OSC_A_PHASE_WIDTH);

	case Param::Local::CARRIER_0_FEEDBACK:
		return l10n::get(STRING_FOR_PARAM_LOCAL_CARRIER_0_FEEDBACK);

	case Param::Local::OSC_A_WAVE_INDEX:
		return l10n::get(STRING_FOR_PARAM_LOCAL_OSC_A_WAVE_INDEX);

	//OSC 2 Volume, Pitch, Phase Width, Carrier Feedback, Wave Index
	case Param::Local::OSC_B_VOLUME:
		return l10n::get(STRING_FOR_PARAM_LOCAL_OSC_B_VOLUME);

	case Param::Local::OSC_B_PITCH_ADJUST:
		return l10n::get(STRING_FOR_PARAM_LOCAL_OSC_B_PITCH_ADJUST);

	case Param::Local::OSC_B_PHASE_WIDTH:
		return l10n::get(STRING_FOR_PARAM_LOCAL_OSC_B_PHASE_WIDTH);

	case Param::Local::CARRIER_1_FEEDBACK:
		return l10n::get(STRING_FOR_PARAM_LOCAL_CARRIER_1_FEEDBACK);

	case Param::Local::OSC_B_WAVE_INDEX:
		return l10n::get(STRING_FOR_PARAM_LOCAL_OSC_B_WAVE_INDEX);

	//FM Mod 1 Volume, Pitch, Feedback
	case Param::Local::MODULATOR_0_VOLUME:
		return l10n::get(STRING_FOR_PARAM_LOCAL_MODULATOR_0_VOLUME);

	case Param::Local::MODULATOR_0_PITCH_ADJUST:
		return l10n::get(STRING_FOR_PARAM_LOCAL_MODULATOR_0_PITCH_ADJUST);

	case Param::Local::MODULATOR_0_FEEDBACK:
		return l10n::get(STRING_FOR_PARAM_LOCAL_MODULATOR_0_FEEDBACK);

	//FM Mod 2 Volume, Pitch, Feedback
	case Param::Local::MODULATOR_1_VOLUME:
		return l10n::get(STRING_FOR_PARAM_LOCAL_MODULATOR_1_VOLUME);

	case Param::Local::MODULATOR_1_PITCH_ADJUST:
		return l10n::get(STRING_FOR_PARAM_LOCAL_MODULATOR_1_PITCH_ADJUST);

	case Param::Local::MODULATOR_1_FEEDBACK:
		return l10n::get(STRING_FOR_PARAM_LOCAL_MODULATOR_1_FEEDBACK);

	//Env 1 ADSR
	case Param::Local::ENV_0_ATTACK:
		return l10n::get(STRING_FOR_PARAM_LOCAL_ENV_0_ATTACK);

	case Param::Local::ENV_0_DECAY:
		return l10n::get(STRING_FOR_PARAM_LOCAL_ENV_0_DECAY);

	case Param::Local::ENV_0_SUSTAIN:
		return l10n::get(STRING_FOR_PARAM_LOCAL_ENV_0_SUSTAIN);

	case Param::Local::ENV_0_RELEASE:
		return l10n::get(STRING_FOR_PARAM_LOCAL_ENV_0_RELEASE);

	//Env 2 ADSR
	case Param::Local::ENV_1_ATTACK:
		return l10n::get(STRING_FOR_PARAM_LOCAL_ENV_1_ATTACK);

	case Param::Local::ENV_1_DECAY:
		return l10n::get(STRING_FOR_PARAM_LOCAL_ENV_1_DECAY);

	case Param::Local::ENV_1_SUSTAIN:
		return l10n::get(STRING_FOR_PARAM_LOCAL_ENV_1_SUSTAIN);

	case Param::Local::ENV_1_RELEASE:
		return l10n::get(STRING_FOR_PARAM_LOCAL_ENV_1_RELEASE);

	//LFO 1 Freq
	case Param::Global::LFO_FREQ:
		return l10n::get(STRING_FOR_PARAM_GLOBAL_LFO_FREQ);

	//LFO 2 Freq
	case Param::Local::LFO_LOCAL_FREQ:
		return l10n::get(STRING_FOR_PARAM_LOCAL_LFO_LOCAL_FREQ);

	//Mod FX Depth, Rate
	case Param::Global::MOD_FX_DEPTH:
		return l10n::get(STRING_FOR_PARAM_GLOBAL_MOD_FX_DEPTH);

	case Param::Global::MOD_FX_RATE:
		return l10n::get(STRING_FOR_PARAM_GLOBAL_MOD_FX_RATE);

	//Arp Rate
	case Param::Global::ARP_RATE:
		return l10n::get(STRING_FOR_PARAM_GLOBAL_ARP_RATE);

	//Noise
	case Param::Local::NOISE_VOLUME:
		return l10n::get(STRING_FOR_PARAM_LOCAL_NOISE_VOLUME);

	default:
		return l10n::get(STRING_FOR_NONE);
	}
}

char const* getParamDisplayName(Kind kind, int32_t p) {
	using enum l10n::String;
	if (kind == Kind::PATCHED) {
		return getPatchedParamDisplayName(p);
	}

	if (kind == Kind::UNPATCHED_SOUND || kind == Kind::UNPATCHED_GLOBAL) {
		// These can basically be 13 chars long, or 14 if the last one is a dot.
		switch (p) {

		//Bass, Bass Freq
		case Param::Unpatched::BASS:
			return l10n::get(STRING_FOR_BASS);

		case Param::Unpatched::BASS_FREQ:
			return l10n::get(STRING_FOR_BASS_FREQUENCY);

		//Treble, Treble Freq
		case Param::Unpatched::TREBLE:
			return l10n::get(STRING_FOR_TREBLE);

		case Param::Unpatched::TREBLE_FREQ:
			return l10n::get(STRING_FOR_TREBLE_FREQUENCY);

		//Sidechain Shape
		case Param::Unpatched::COMPRESSOR_SHAPE:
			return l10n::get(STRING_FOR_SIDECHAIN_SHAPE);

		//Decimation, Bitcrush
		case Param::Unpatched::SAMPLE_RATE_REDUCTION:
			return l10n::get(STRING_FOR_DECIMATION);

		case Param::Unpatched::BITCRUSHING:
			return l10n::get(STRING_FOR_BITCRUSH);

		//Mod FX Offset, Feedback
		case Param::Unpatched::MOD_FX_OFFSET:
			return l10n::get(STRING_FOR_MODFX_OFFSET);

		case Param::Unpatched::MOD_FX_FEEDBACK:
			return l10n::get(STRING_FOR_MODFX_FEEDBACK);

		case Param::Unpatched::STUTTER_RATE:
			return l10n::get(STRING_FOR_STUTTER_RATE);
		}
	}

	if (kind == Kind::UNPATCHED_SOUND) {
		switch (p) {
		case Param::Unpatched::Sound::ARP_GATE:
			return l10n::get(STRING_FOR_ARP_GATE_MENU_TITLE);
		case Param::Unpatched::Sound::PORTAMENTO:
			return l10n::get(STRING_FOR_PORTAMENTO);
		}
	}

	if (kind == Kind::UNPATCHED_GLOBAL) {
		// These can basically be 13 chars long, or 14 if the last one is a dot.
		switch (p) {
		//Master Volume, Pitch, Pan
		case Param::Unpatched::GlobalEffectable::VOLUME:
			return l10n::get(STRING_FOR_MASTER_LEVEL);

		case Param::Unpatched::GlobalEffectable::PITCH_ADJUST:
			return l10n::get(STRING_FOR_MASTER_PITCH);

		case Param::Unpatched::GlobalEffectable::PAN:
			return l10n::get(STRING_FOR_MASTER_PAN);

		//LPF Cutoff, Resonance
		case Param::Unpatched::GlobalEffectable::LPF_FREQ:
			return l10n::get(STRING_FOR_LPF_FREQUENCY);

		case Param::Unpatched::GlobalEffectable::LPF_RES:
			return l10n::get(STRING_FOR_LPF_RESONANCE);

		//HPF Cutoff, Resonance
		case Param::Unpatched::GlobalEffectable::HPF_FREQ:
			return l10n::get(STRING_FOR_HPF_FREQUENCY);

		case Param::Unpatched::GlobalEffectable::HPF_RES:
			return l10n::get(STRING_FOR_HPF_RESONANCE);

		//Reverb Amount
		case Param::Unpatched::GlobalEffectable::REVERB_SEND_AMOUNT:
			return l10n::get(STRING_FOR_REVERB_AMOUNT);

		//Delay Rate, Amount
		case Param::Unpatched::GlobalEffectable::DELAY_RATE:
			return l10n::get(STRING_FOR_DELAY_RATE);

		case Param::Unpatched::GlobalEffectable::DELAY_AMOUNT:
			return l10n::get(STRING_FOR_DELAY_AMOUNT);

		//Sidechain Send
		case Param::Unpatched::GlobalEffectable::SIDECHAIN_VOLUME:
			return l10n::get(STRING_FOR_SIDECHAIN_LEVEL);

		//Mod FX Depth, Rate
		case Param::Unpatched::GlobalEffectable::MOD_FX_DEPTH:
			return l10n::get(STRING_FOR_MODFX_DEPTH);

		case Param::Unpatched::GlobalEffectable::MOD_FX_RATE:
			return l10n::get(STRING_FOR_MODFX_RATE);
		}
	}
	return l10n::get(STRING_FOR_NONE);
}

char const* paramNameForFile(Kind const kind, ParamType const param) {
	if (kind == UNPATCHED_SOUND) {
		// Unpatched params just for Sounds
		switch (param) {
		case Param::Unpatched::START + Param::Unpatched::Sound::ARP_GATE:
			return "arpGate";

		case Param::Unpatched::START + Param::Unpatched::Sound::PORTAMENTO:
			return "portamento";

		default:
		    // Fall through to the other param kind handling
		    ;
		}
	}
	else if (kind == UNPATCHED_GLOBAL) {
		// Params for GlobalEffectable
		switch (param) {
		case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::MOD_FX_RATE:
			return "modFXRate";

		case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::MOD_FX_DEPTH:
			return "modFXDepth";

		case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::DELAY_RATE:
			return "delayRate";

		case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::DELAY_AMOUNT:
			return "delayFeedback";

		case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::PAN:
			return "pan";

		case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::LPF_FREQ:
			return "lpfFrequency";

		case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::LPF_RES:
			return "lpfResonance";

		case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::HPF_FREQ:
			return "hpfFrequency";

		case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::HPF_RES:
			return "hpfResonance";

		case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::REVERB_SEND_AMOUNT:
			return "reverbAmount";

		case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::VOLUME:
			return "volume";

		case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::SIDECHAIN_VOLUME:
			return "sidechainCompressorVolume";

		case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::PITCH_ADJUST:
			return "pitchAdjust";

		default:
		    // Fall through to the other param kind handling
		    ;
		}
	}

	// Local params
	switch (param) {
	case Param::Local::OSC_A_VOLUME:
		return "oscAVolume";

	case Param::Local::OSC_B_VOLUME:
		return "oscBVolume";

	case Param::Local::VOLUME:
		return "volume";

	case Param::Local::NOISE_VOLUME:
		return "noiseVolume";

	case Param::Local::OSC_A_PHASE_WIDTH:
		return "oscAPhaseWidth";

	case Param::Local::OSC_B_PHASE_WIDTH:
		return "oscBPhaseWidth";

	case Param::Local::OSC_A_WAVE_INDEX:
		return "oscAWavetablePosition";

	case Param::Local::OSC_B_WAVE_INDEX:
		return "oscBWavetablePosition";

	case Param::Local::LPF_RESONANCE:
		return "lpfResonance";

	case Param::Local::HPF_RESONANCE:
		return "hpfResonance";

	case Param::Local::PAN:
		return "pan";

	case Param::Local::MODULATOR_0_VOLUME:
		return "modulator1Volume";

	case Param::Local::MODULATOR_1_VOLUME:
		return "modulator2Volume";

	case Param::Local::LPF_FREQ:
		return "lpfFrequency";

	case Param::Local::LPF_MORPH:
		return "lpfMorph";

	case Param::Local::HPF_MORPH:
		return "hpfMorph";

	case Param::Local::PITCH_ADJUST:
		return "pitch";

	case Param::Local::OSC_A_PITCH_ADJUST:
		return "oscAPitch";

	case Param::Local::OSC_B_PITCH_ADJUST:
		return "oscBPitch";

	case Param::Local::MODULATOR_0_PITCH_ADJUST:
		return "modulator1Pitch";

	case Param::Local::MODULATOR_1_PITCH_ADJUST:
		return "modulator2Pitch";

	case Param::Local::HPF_FREQ:
		return "hpfFrequency";

	case Param::Local::LFO_LOCAL_FREQ:
		return "lfo2Rate";

	case Param::Local::ENV_0_ATTACK:
		return "env1Attack";

	case Param::Local::ENV_0_DECAY:
		return "env1Decay";

	case Param::Local::ENV_0_SUSTAIN:
		return "env1Sustain";

	case Param::Local::ENV_0_RELEASE:
		return "env1Release";

	case Param::Local::ENV_1_ATTACK:
		return "env2Attack";

	case Param::Local::ENV_1_DECAY:
		return "env2Decay";

	case Param::Local::ENV_1_SUSTAIN:
		return "env2Sustain";

	case Param::Local::ENV_1_RELEASE:
		return "env2Release";

	case Param::Global::LFO_FREQ:
		return "lfo1Rate";

	case Param::Global::VOLUME_POST_FX:
		return "volumePostFX";

	case Param::Global::VOLUME_POST_REVERB_SEND:
		return "volumePostReverbSend";

	case Param::Global::DELAY_RATE:
		return "delayRate";

	case Param::Global::DELAY_FEEDBACK:
		return "delayFeedback";

	case Param::Global::REVERB_AMOUNT:
		return "reverbAmount";

	case Param::Global::MOD_FX_RATE:
		return "modFXRate";

	case Param::Global::MOD_FX_DEPTH:
		return "modFXDepth";

	case Param::Global::ARP_RATE:
		return "arpRate";

	case Param::Local::MODULATOR_0_FEEDBACK:
		return "modulator1Feedback";

	case Param::Local::MODULATOR_1_FEEDBACK:
		return "modulator2Feedback";

	case Param::Local::CARRIER_0_FEEDBACK:
		return "carrier1Feedback";

	case Param::Local::CARRIER_1_FEEDBACK:
		return "carrier2Feedback";

	case Param::Local::FOLD:
		return "waveFold";

		// Unpatched params
	case Param::Unpatched::START + Param::Unpatched::STUTTER_RATE:
		return "stutterRate";

	case Param::Unpatched::START + Param::Unpatched::BASS:
		return "bass";

	case Param::Unpatched::START + Param::Unpatched::TREBLE:
		return "treble";

	case Param::Unpatched::START + Param::Unpatched::BASS_FREQ:
		return "bassFreq";

	case Param::Unpatched::START + Param::Unpatched::TREBLE_FREQ:
		return "trebleFreq";

	case Param::Unpatched::START + Param::Unpatched::SAMPLE_RATE_REDUCTION:
		return "sampleRateReduction";

	case Param::Unpatched::START + Param::Unpatched::BITCRUSHING:
		return "bitcrushAmount";

	case Param::Unpatched::START + Param::Unpatched::MOD_FX_OFFSET:
		return "modFXOffset";

	case Param::Unpatched::START + Param::Unpatched::MOD_FX_FEEDBACK:
		return "modFXFeedback";

	case Param::Unpatched::START + Param::Unpatched::COMPRESSOR_SHAPE:
		return "compressorShape";

	default:
		return "none";
	}
}

uint8_t fileStringToParam(Kind kind, char const* string) {
	for (int32_t p = 0; p < kAbsoluteMaximumParam; ++p) {
		if (strcmp(string, paramNameForFile(kind, p)) == 0) {
			return p;
		}
	}

	if (strcmp(string, "range") == 0) {
		return Param::PLACEHOLDER_RANGE; // For compatibility reading files from before V3.2.0
	}

	return Param::Global::NONE;
}

} // namespace params
} // namespace modulation
} // namespace deluge
