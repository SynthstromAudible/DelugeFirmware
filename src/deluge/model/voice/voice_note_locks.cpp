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

#include "model/voice/voice_note_locks.h"
#include "model/model_stack.h"
#include "model/note/note_lock_playback.h"
#include "model/voice/voice_note_locks_inline.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "processing/sound/sound.h"
#include <optional>

namespace params = deluge::modulation::params;
namespace note_locks = deluge::note_locks;

namespace {
using deluge::voice_note_locks_detail::kVolumeOff;
/// Whether `locks` turn this volume param up from -inf: nullopt if it isn't locked
std::optional<bool> lockedOn(const note_locks::LockSet& locks, int32_t param) {
	std::optional<int32_t> value = locks.get(param);
	if (!value) [[likely]] {
		return std::nullopt;
	}
	return *value != kVolumeOff;
}

bool sourceHasSampleIfNeeded(Sound& sound, int32_t s) {
	return sound.getSynthMode() == SynthMode::FM || sound.sources[s].oscType != OscType::SAMPLE
	       || sound.sources[s].hasAtLeastOneAudioFileLoaded();
}
} // namespace

bool VoiceNoteLocks::takePending() {
	// Empty outside the sequencer's note-ons - and for a legato / mono retrigger by an unlocked note, which must drop
	// the previous note's locks
	const note_locks::Pending& pending = note_locks::pending;
	liveEditTag = pending.ref.liveTag;
	if (pending.locks == nullptr && empty()) [[likely]] {
		return false;
	}
	LockSet::operator=(pending.locks != nullptr ? *pending.locks : LockSet{});
	return true;
}

bool VoiceNoteLocks::pendingTurnsOnASource(Sound& sound) {
	const note_locks::LockSet* locks = note_locks::pending.locks;
	if (locks == nullptr) [[likely]] {
		return false;
	}
	for (int32_t s = 0; s < kNumSources; s++) {
		if (lockedOn(*locks, params::LOCAL_OSC_A_VOLUME + s).value_or(false) && sourceHasSampleIfNeeded(sound, s)) {
			return true;
		}
	}
	return lockedOn(*locks, params::LOCAL_NOISE_VOLUME).value_or(false);
}

bool VoiceNoteLocks::giveSustainLocked(Sound& sound, ParamManagerForTimeline* paramManager) const {
	return sound.envelopeHasSustainEver(0, paramManager, this);
}

bool VoiceNoteLocks::sourceEverActiveLocked(ModelStackWithSoundFlags* modelStack, int32_t s) const {
	Sound& sound = *static_cast<Sound*>(modelStack->modControllable);
	bool on = lockedOn(*this, params::LOCAL_OSC_A_VOLUME + s).value_or(false);
	// Osc sync makes osc A run for osc B (Sound::renderingOscillatorSyncEver)
	if (sound.oscillatorSync && sound.getSynthMode() != SynthMode::FM) {
		on = on || lockedOn(*this, params::LOCAL_OSC_B_VOLUME).value_or(false);
	}
	return on && sourceHasSampleIfNeeded(sound, s);
}

bool VoiceNoteLocks::sourceActiveCurrentlyLocked(Sound& sound, int32_t s) const {
	bool locked = lockedOn(*this, params::LOCAL_OSC_A_VOLUME + s).value_or(false);
	return (sound.getSynthMode() == SynthMode::RINGMOD || locked) && sourceHasSampleIfNeeded(sound, s);
}

bool VoiceNoteLocks::oscSyncCurrentlyLocked(Sound& sound) const {
	bool locked = lockedOn(*this, params::LOCAL_OSC_B_VOLUME).value_or(false);
	return sound.oscillatorSync && sound.getSynthMode() != SynthMode::FM
	       && (locked || sound.getSynthMode() == SynthMode::RINGMOD);
}

bool VoiceNoteLocks::modulatorActiveLocked(Sound&, int32_t m) const {
	return lockedOn(*this, params::LOCAL_MODULATOR_0_VOLUME + m).value_or(false);
}

bool VoiceNoteLocks::noiseEverActiveLocked() const {
	return lockedOn(*this, params::LOCAL_NOISE_VOLUME).value_or(false);
}
