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

class DxPatch;
class DxVoice;

namespace deluge::dsp {

/// @brief The DX7 (MSFA-derived FM) source engine for one voice-source.
///
/// Unlike the oscillator engines, DX7 is a *foreign* synthesis engine: no phasor, a pooled @ref DxVoice owning
/// the six-operator FM state, its log-pitch + control derived from a @ref DxPatch, a voice-death signal, and a
/// bespoke amplitude sum (kept in Voice). So it is a standalone engine, not a @ref synth::Source — it
/// defines the shape a foreign engine takes; the uniform L1 interface (and the note-on registry that deletes the
/// renderBasicSource dispatch) is a later step.
///
/// It encapsulates the DX7-specific logic that was smeared across Voice / VoiceUnisonPartSource: the pooled
/// DxVoice lifecycle (solicit / reuse-on-restart / free), note-off keyup, and the per-block pitch/control
/// derivation + render. Ownership of the pooled voice is **explicit** (freed only in @ref unassign, never in a
/// destructor) — Voice's per-source objects are pooled and reused in place, so an owning dtor would risk a
/// double-free.
class DX7Engine {
	DxVoice* voice_ = nullptr;
	/// The patch this engine renders from, injected at note-on (the adapter domain). Stable for a Source's
	/// lifetime (`Source::ensureDxPatch` creates it once, never replaces), so caching it here keeps the render
	/// path free of any `Sound&`/`Source` reach. Contents may be UI-edited mid-note — `render`'s `patchChanged`
	/// arg signals the re-derive.
	DxPatch* patch_ = nullptr;

public:
	/// Solicit a pooled DxVoice if we don't already hold one (reused across a restart), then initialise it for the
	/// note, caching @p patch for subsequent renders. @return false if the pool is empty (the voice should fail to
	/// start).
	bool noteOn(DxPatch& patch, int midinote, int velocity);

	/// Return the pooled DxVoice to the pool (no-op if none held).
	void unassign();

	/// Note-off: begin the release of all operators (no-op if no voice is held).
	void keyup();

	/// Render @p numSamples of the FM voice, *adding* into @p buf (DxVoice::compute accumulates). Derives the
	/// log-pitch from @p phaseIncrement and the amplitude-mod control from @p ampModParam, re-deriving the patch
	/// state first when @p patchChanged. Renders from the patch cached at note-on. @return whether the voice is
	/// still active (false ⇒ the note has ended).
	bool render(int32_t* buf, int32_t numSamples, uint32_t phaseIncrement, int32_t ampModParam, bool patchChanged,
	            int midinote);
};

} // namespace deluge::dsp
