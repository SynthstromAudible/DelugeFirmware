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

#include <cstdint>
#include <span>

namespace deluge::synth {

/// @brief The port through which a DX7 source drives one pooled DX/MSFA FM voice — the dependency-inversion seam
/// that lets `DX7Source` live in the library without naming the firmware's `DxVoice`/`DxPatch`/`getDxEngine()`.
///
/// The firmware supplies the adapter (`deluge::dsp::DxVoiceAdapter`, wrapping the MSFA pool + voice); a host/test
/// could supply a fake. The preset patch is an **opaque handle** (`void*`) the library never dereferences — it
/// hands the firmware's `DxPatch*` straight back through. See `docs/dev/synth_source_ports.md`.
class DxVoicePort {
public:
	virtual ~DxVoicePort() = default;

	/// Solicit + initialise a pooled voice for this note, caching @p patch for subsequent renders. @return false
	/// if the pool is empty (the source should fail to start).
	virtual bool noteOn(void* patch, int midinote, int velocity) = 0;

	/// Note-off: begin the release of all operators (no-op if no voice is held).
	virtual void keyup() = 0;

	/// Return the pooled voice (no-op if none held).
	virtual void unassign() = 0;

	/// Render @p numSamples of FM. @return the rendered samples (size @p numSamples), or an **empty span** if the
	/// voice has ended (⇒ the source should unassign). Derives log-pitch from @p phaseIncrement and the
	/// amplitude-mod control from @p ampModParam, re-deriving the cached patch first when @p patchChanged.
	virtual std::span<const int32_t> render(int32_t numSamples, uint32_t phaseIncrement, int32_t ampModParam,
	                                        bool patchChanged, int midinote) = 0;
};

} // namespace deluge::synth
