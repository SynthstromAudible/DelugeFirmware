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
#pragma once

#include "synth/source/dx_voice_port.h" // synth::DxVoicePort
#include <cstdint>
#include <span>

class DxPatch;
class DxVoice;

namespace deluge::dsp {

/// @brief The firmware adapter behind @ref deluge::synth::DxVoicePort: a pooled MSFA @ref DxVoice for one
/// voice-source. Holds the source's `DxVoice` + cached `DxPatch`, soliciting/releasing from the global
/// `getDxEngine()` pool and running the per-block log-pitch / control derivation + FM compute. This is the
/// app-coupled half the library-resident `DX7Source` no longer carries (the FM math is self-contained — no
/// AudioEngine/storage — but the pool + the MSFA types stay app-side). Ownership of the pooled voice is
/// **explicit** (freed only in @ref unassign, never in a destructor) — the per-source objects are pooled and
/// reused in place, so an owning dtor would risk a double-free.
class DxVoiceAdapter final : public synth::DxVoicePort {
	DxVoice* voice_ = nullptr;
	DxPatch* patch_ = nullptr;

public:
	bool noteOn(void* patch, int midinote, int velocity) override;
	void keyup() override;
	void unassign() override;
	std::span<const int32_t> render(int32_t numSamples, uint32_t phaseIncrement, int32_t ampModParam, bool patchChanged,
	                                int midinote) override;
};

} // namespace deluge::dsp
