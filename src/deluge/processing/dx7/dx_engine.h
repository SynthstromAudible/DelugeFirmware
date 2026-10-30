/*
 * Copyright © 2015-2024 Synthstrom Audible Limited
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

#pragma once

#include "definitions_cxx.hpp"
#include "dsp-next/fixed/dx7/graph.h"
#include "dsp-next/fixed/dx7/tables.h"
#include "dsp-next/fixed/dx7/voice.h"
#include "dsp-next/fixed/noise/cong_noise.h"
#include "processing/dx7/dx_patch.h"
#include <gsl/gsl>

/// @brief The voice the pool holds: a DX7 voice drawing from the synth's shared noise stream.
using DxPoolVoice = deluge::dsp::fixed::dx7::Voice<>;

class DxEngine {
public:
	DxEngine() = default;
	deluge::dsp::fixed::dx7::Tables tables;

	/// @brief The intermediate buses, lent to each voice's graph as it renders: voices render one at a time.
	[[nodiscard]] deluge::dsp::fixed::dx7::Buses& buses() { return buses_; }

	static gsl::owner<DxPoolVoice*> solicitDxVoice();
	static void dxVoiceUnassigned(gsl::owner<DxPoolVoice*> dxVoice);
	DxPatch* newPatch();

private:
	deluge::dsp::fixed::dx7::Buses buses_;
};

extern DxEngine* dxEngine;

DxEngine* getDxEngine();
