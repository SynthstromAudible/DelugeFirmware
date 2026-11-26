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

#include "synth/engines/dx7_engine.h"
#include "dsp/core/fixed_point.h"
#include "synth/sources/source.h"

namespace deluge::synth {

// The order of a DX7 slot's note-on as the subtractive engine ran it: the voice's phase draws came first; per part,
// the DX7's note-on (its port draws its detunes and phases), then the carrier feedback's reset on a new voice.
bool Dx7Engine::note_on(const EngineNote& note) {
	if (!mark_active(note, /*refuse_silent=*/false)) {
		return false;
	}
	calculate_phase_increments(note);
	// Slot 1 plays nothing here, whatever its setup says, so it never holds the voice open
	for (int32_t u = 0; u < note.config->voicing.unison.count; u++) {
		note.parts[u].slots[1].active = false;
	}
	const SourceSetup& dx7 = note.setup->sources[0];
	if (!dx7.ever_active) [[unlikely]] {
		return true;
	}
	const SourceNote source_note{
	    .samples_late = note.samples_late,
	    .note_code = note.note_code,
	    .velocity = note.velocity,
	    .priority_rating = note.priority_rating,
	    .dx_patch = dx7.dx_patch,
	    .osc_type = dx7.osc_type,
	};
	for (int32_t u = 0; u < note.config->voicing.unison.count; u++) {
		SourceSlot& slot = note.parts[u].slots[0];
		if (!slot.active) {
			continue;
		}
		if (!dx7_source_of(note.parts[u]).note_on(source_note)) [[unlikely]] {
			return false;
		}
		if (note.reset) {
			slot.carrier_feedback = dsp::Q<31>{};
		}
	}
	return true;
}

EngineRenderResult Dx7Engine::render(const EngineBlock& block) {
	return render_with(block, SourcePick::DX7);
}

void Dx7Engine::release(std::span<UnisonPart> parts, const ReleaseInfo& info, int32_t /*priority_rating*/) {
	for (int32_t u = 0; u < info.num_unison; u++) {
		(void)dx7_source_of(parts[u]).release({});
	}
}

} // namespace deluge::synth
