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

#include "synth/source/dx_voice_port.h" // DxVoicePort
#include "synth/source/source.h"        // synth::Source, SourceRenderContext, SourceRenderResult
#include <cstdint>

namespace deluge::synth {

/// @brief The DX7 (MSFA FM) source for one voice-source — a @ref synth::Source over an injected
/// @ref DxVoicePort.
///
/// App-free: it holds only the port (the firmware's pooled `DxVoice` adapter lives behind it) and contributes the
/// bespoke per-unison amplitude/stereo sum in @ref renderSource; the FM compute + the pooled-voice lifecycle are
/// the port's job. The preset patch flows through @ref noteOn as an opaque handle (never dereferenced here). See
/// `docs/dev/synth_source_ports.md`.
class DX7Source : public Source {
	DxVoicePort* dx_; ///< the injected FM-voice port (a firmware adapter); not owned

public:
	explicit DX7Source(DxVoicePort* dx) : dx_{dx} {}

	/// Solicit + init the FM voice for this note, caching @p patch (opaque preset handle). @return false if the
	/// pool is empty.
	bool noteOn(void* patch, int midinote, int velocity) { return dx_->noteOn(patch, midinote, velocity); }

	/// Return the pooled FM voice (no-op if none held).
	void unassign() { dx_->unassign(); }

	/// Note-off: begin the release of all operators.
	void keyup() { dx_->keyup(); }

	/// Render this DX7 source's contribution into @c ctx.oscBuffer (the bespoke per-unison amplitude/stereo sum
	/// over the port's FM block), reporting UNASSIGN once the FM voice has ended.
	SourceRenderResult renderSource(const SourceRenderContext& ctx) override;
};

} // namespace deluge::synth
