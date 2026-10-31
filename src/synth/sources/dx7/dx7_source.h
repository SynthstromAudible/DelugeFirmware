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

#include "synth/sources/dx7/dx_voice_port.h" // DxVoicePort
#include "synth/sources/source.h"            // synth::Source, SourceBlock, SourceRenderResult
#include <cstdint>

namespace deluge::synth {

/// @brief The DX7 (MSFA FM) source for one voice-source — a @ref synth::Source over an injected
/// @ref DxVoicePort.
///
/// App-free: it holds only the port (the firmware's pooled `dx7::Voice` adapter lives behind it) and contributes the
/// bespoke per-unison amplitude/stereo sum in @ref render; the FM compute + the pooled-voice lifecycle are
/// the port's job. The preset patch flows through @ref note_on as an opaque handle (never dereferenced here). See
/// `docs/dev/synth_source_ports.md`.
class DX7Source : public Source {
	DxVoicePort* dx_; ///< the injected FM-voice port (a firmware adapter); not owned

public:
	explicit DX7Source(DxVoicePort* dx) : dx_{dx} {}
	/// Plays through @p dx, which must outlive this source.
	void bind_port(DxVoicePort* dx) { dx_ = dx; }

	/// Solicits and starts the FM voice for this note with the note's patch (an opaque preset handle). @return false
	/// if the pool is empty.
	bool note_on(const SourceNote& note) override { return dx_->note_on(note.dx_patch, note.note_code, note.velocity); }

	/// Returns the pooled FM voice (no-op if none held).
	void unassign(bool /*deleting_song*/) override {
		if (dx_ != nullptr) {
			dx_->unassign();
		}
	}

	/// Note-off: begins the release of all operators.
	bool release(const SourceRelease& /*release*/) override {
		dx_->keyup();
		return true;
	}

	/// Renders this DX7 source's contribution into @c block.osc_buffer (the per-unison amplitude/stereo sum over the
	/// port's FM block), reporting UNASSIGN once the FM voice has ended.
	SourceRenderResult render(const SourceBlock& block) override;
};

} // namespace deluge::synth
