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

#include "model/consequence/consequence_arp_settings_change.h"
#include "model/clip/arp_freeze.h"
#include "model/clip/instrument_clip.h"
#include <utility>

ConsequenceArpSettingsChange::ConsequenceArpSettingsChange(InstrumentClip* newClip)
    : clip(newClip), preset(newClip->arpSettings.preset), mode(newClip->arpSettings.mode),
      noteProbability(newClip->arpSettings.noteProbability), spreadVelocity(newClip->arpSettings.spreadVelocity),
      reverseProbability(newClip->arpSettings.reverseProbability) {
}

Error ConsequenceArpSettingsChange::revert(TimeType /*time*/, ModelStack* /*modelStack*/) {
	arp_freeze::stopNotesForArpChange(clip);

	std::swap(preset, clip->arpSettings.preset);
	std::swap(mode, clip->arpSettings.mode);
	std::swap(noteProbability, clip->arpSettings.noteProbability);
	std::swap(spreadVelocity, clip->arpSettings.spreadVelocity);
	std::swap(reverseProbability, clip->arpSettings.reverseProbability);
	clip->arpSettings.flagForceArpRestart = true;

	// This is reverted last when redoing, after the frozen notes are back
	arp_freeze::resumePlayback(clip);

	return Error::NONE;
}
