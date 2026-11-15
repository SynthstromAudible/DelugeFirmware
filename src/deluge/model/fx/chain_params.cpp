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

#include "model/fx/chain_params.h"
#include "model/fx/song_stutter.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"

namespace deluge::model {

namespace params = deluge::modulation::params;

void initSharedParams(ParamManager* paramManager, params::Kind defaults) {
	UnpatchedParamSet& unpatchedParams = *paramManager->getUnpatchedParamSet();
	setParamsToDefaults(
	    unpatchedParams, defaults,
	    {params::UNPATCHED_BASS, params::UNPATCHED_TREBLE, params::UNPATCHED_BASS_FREQ, params::UNPATCHED_TREBLE_FREQ,
	     params::UNPATCHED_ARP_GATE, params::UNPATCHED_NOTE_PROBABILITY, params::UNPATCHED_ARP_BASS_PROBABILITY,
	     params::UNPATCHED_ARP_SWAP_PROBABILITY, params::UNPATCHED_ARP_GLIDE_PROBABILITY,
	     params::UNPATCHED_REVERSE_PROBABILITY, params::UNPATCHED_ARP_CHORD_PROBABILITY,
	     params::UNPATCHED_ARP_RATCHET_PROBABILITY, params::UNPATCHED_ARP_RATCHET_AMOUNT,
	     params::UNPATCHED_ARP_SEQUENCE_LENGTH, params::UNPATCHED_ARP_CHORD_POLYPHONY, params::UNPATCHED_ARP_RHYTHM,
	     params::UNPATCHED_SPREAD_VELOCITY, params::UNPATCHED_ARP_SPREAD_GATE, params::UNPATCHED_ARP_SPREAD_OCTAVE});
	initStutterParams(paramManager, defaults);
	setParamsToDefaults(unpatchedParams, defaults,
	                    {params::UNPATCHED_MOD_FX_OFFSET, params::UNPATCHED_MOD_FX_FEEDBACK,
	                     params::UNPATCHED_SAMPLE_RATE_REDUCTION, params::UNPATCHED_BITCRUSHING,
	                     params::UNPATCHED_SIDECHAIN_SHAPE, params::UNPATCHED_COMPRESSOR_THRESHOLD});
}

void initGlobalParams(ParamManager* paramManager) {
	initSharedParams(paramManager, params::Kind::UNPATCHED_GLOBAL);

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	unpatchedParams->kind = params::Kind::UNPATCHED_GLOBAL;
	setParamsToDefaults(*unpatchedParams, params::Kind::UNPATCHED_GLOBAL,
	                    {params::UNPATCHED_ARP_RATE, params::UNPATCHED_MOD_FX_RATE, params::UNPATCHED_MOD_FX_DEPTH,
	                     params::UNPATCHED_DELAY_RATE, params::UNPATCHED_PAN, params::UNPATCHED_DELAY_AMOUNT,
	                     params::UNPATCHED_REVERB_SEND_AMOUNT, params::UNPATCHED_VOLUME,
	                     params::UNPATCHED_SIDECHAIN_VOLUME, params::UNPATCHED_PITCH_ADJUST, params::UNPATCHED_LPF_RES,
	                     params::UNPATCHED_LPF_FREQ, params::UNPATCHED_HPF_RES, params::UNPATCHED_HPF_FREQ,
	                     params::UNPATCHED_LPF_MORPH, params::UNPATCHED_HPF_MORPH});
}

void initAudioClipParams(ParamManagerForTimeline* paramManager) {
	initGlobalParams(paramManager);
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	unpatchedParams->params[params::UNPATCHED_VOLUME].setBase(*unpatchedParams->docForParam(params::UNPATCHED_VOLUME),
	                                                          -536870912);
}

} // namespace deluge::model
