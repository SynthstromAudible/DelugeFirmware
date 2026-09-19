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

#include "storage/startup_song_policy.h"

namespace deluge::storage {

StartupStep nextStartupPhase(StartupPhase current, StartupPresence answer, bool usingTemplate) {
	switch (current) {
	case StartupPhase::CheckCanary:
		switch (answer) {
		case StartupPresence::Confirmed:
			// A canary from a previous boot: that boot died mid-load. Skip the song and KEEP the
			// canary, so a boot that dies again is still recognised.
			return {StartupPhase::Done, usingTemplate};
		case StartupPresence::Missing:
			return {usingTemplate ? StartupPhase::CheckTemplate : StartupPhase::WriteCanary, usingTemplate};
		case StartupPresence::Unusable:
			// Cannot tell whether a canary is there. Treating that as "no canary" would resume the
			// crash loop this whole mechanism exists to break.
			return {StartupPhase::Done, usingTemplate};
		}
		return {StartupPhase::Done, usingTemplate};

	case StartupPhase::CheckTemplate:
		switch (answer) {
		case StartupPresence::Confirmed:
			return {StartupPhase::WriteCanary, usingTemplate};
		case StartupPresence::Missing:
			// The one Missing branch that WRITES -- and the write is its own phase, because it is card
			// I/O: done inline here it blocked whatever tier the routine was ticking on.
			return {StartupPhase::WriteTemplate, usingTemplate};
		case StartupPresence::Unusable:
			// The one ruling that would put bytes on the card: never write a template over a card
			// that merely failed to answer.
			return {StartupPhase::Done, usingTemplate};
		}
		return {StartupPhase::Done, usingTemplate};

	case StartupPhase::CheckSong:
		switch (answer) {
		case StartupPresence::Confirmed:
			return {StartupPhase::BeginLoad, usingTemplate};
		case StartupPresence::Missing:
			return {StartupPhase::CheckFallbackTemplate, usingTemplate};
		case StartupPresence::Unusable:
			// Nothing was loaded, so this was not a crash: remove the canary rather than leave one
			// that would make the NEXT boot skip a song for no reason.
			return {StartupPhase::UnlinkCanary, usingTemplate};
		}
		return {StartupPhase::UnlinkCanary, usingTemplate};

	case StartupPhase::CheckFallbackTemplate:
		switch (answer) {
		case StartupPresence::Confirmed:
			// Mode flips: the completion wipes the name of a template-loaded song.
			return {StartupPhase::BeginLoad, true};
		case StartupPresence::Missing:
			return {StartupPhase::UnlinkCanary, usingTemplate};
		case StartupPresence::Unusable:
			return {StartupPhase::UnlinkCanary, usingTemplate};
		}
		return {StartupPhase::UnlinkCanary, usingTemplate};

	case StartupPhase::Idle:
	case StartupPhase::WriteTemplate:
		// However the write turned out. A template that could not be written is not a reason to skip the
		// fail-safe -- the inline version carried on too, and the canary is what protects the load.
		return {StartupPhase::WriteCanary, usingTemplate};

	case StartupPhase::WriteCanary:
	case StartupPhase::BeginLoad:
	case StartupPhase::UnlinkCanary:
	case StartupPhase::Done:
		// Not existence-driven: their own outcomes advance them, in `startupSongRoutine()`. Reaching
		// here means a caller asked the wrong question, and `Done` is the answer that changes nothing.
		return {StartupPhase::Done, usingTemplate};
	}
	// No `default:` in any switch above, deliberately, so -Wswitch catches a new enumerator.
	return {StartupPhase::Done, usingTemplate};
}

} // namespace deluge::storage
