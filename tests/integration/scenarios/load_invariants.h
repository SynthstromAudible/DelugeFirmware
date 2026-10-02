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

/// @brief The two invariants an audio-file load must preserve, in one place every load scenario can
///        reach (audio_load_ab, load_policy), so a change to what "half-built" means lands in all of
///        them at once.

#include "model/sample/sample.h"
#include "storage/audio/audio_file_manager.h"

#include <cstdint>
#include <cstdio>

// `src/lib/printf.h` soft-aliases `printf` to the embedded `printf_`, which emits through
// `putchar_` -- a symbol the host harnesses do not provide. Whether the alias is in scope here depends
// on nothing more than which OTHER header a consumer happened to include first, and with it this
// inline fails to LINK. Pin the standard function, since what this prints is gate output that has to
// reach real stdout either way.
#ifdef printf
#undef printf
#endif

/// @brief Assert the two invariants an async load must preserve (design §4b, §6): the manager holds
///        no half-built object (adopted, reasoned, but with zero geometry), and no load leaked a
///        reason.
///
/// Both hold trivially on the synchronous path — pinning them makes a later async break loud.
///
/// The geometry half only applies to Samples (a WaveTable has no byteDepth/numChannels concept of
/// its own in this sense). The leaked-reason half applies to every AudioFile: neither caller ever
/// calls addReason() itself, and buildAudioFileFromCard's own protect-during-setup reason is always
/// dropped before getAudioFileFromFilename returns (removeReason("E399") — "Setup done ... the
/// caller re-leases"), so every object reachable here should sit at leaseCount() == 0 between items.
///
/// @param label Printed with any failure, to say which item broke it. Callers pass a
///              caller-qualified string (e.g. "audioLoadAB/both2") so a FAIL line names its gate.
/// @return true if both invariants hold across every resident AudioFile.
inline bool checkLoadInvariants(const char* label) {
	bool ok = true;
	for (int32_t i = 0; i < static_cast<int32_t>(audioFileManager.audioFiles.size()); i++) {
		AudioFile* f = audioFileManager.audioFiles[i];

		if (f->type == AudioFileType::SAMPLE) {
			auto* s = static_cast<Sample*>(f);
			if (s->byteDepth == 0 || s->numChannels == 0) {
				printf("FAIL [%s]: zero-geometry Sample in cache: %s\n", label, s->filePath.c_str());
				ok = false;
			}
		}

		if (f->leaseCount() != 0) {
			printf("FAIL [%s]: leaked reason(s) on %s (leaseCount=%u)\n", label, f->filePath.c_str(), f->leaseCount());
			ok = false;
		}
	}
	return ok;
}
