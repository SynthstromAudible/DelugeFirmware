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

#include "model/note/note_locks.h"
#include "modulation/params/param.h"
#include <cstdint>

class ModelStackWithSoundFlags;
class ParamManager;
class ParamManagerForTimeline;
class Sound;

/// The parameter locks of the note a Voice is playing: its own copy, taken at note-on (pool entries can move while the
/// voice sounds). The Voice's patcher reads them in place of the locked params' knob values.
///
/// Sound's checks of whether a source / modulator / noise / osc sync / filter is on, or whether envelope 1 sustains,
/// look at the knob values. A lock replaces the knob value for its note, so a Voice asks the versions here instead.
/// Each is the Sound's own answer unless a lock is involved.
class VoiceNoteLocks final : public deluge::note_locks::LockSet {
public:
	/// note_locks::LiveEdit::tag of the held pad whose uncommitted lock edits this voice was started with, else 0
	uint16_t liveEditTag{0};

	/// Takes over the locks - or lack of them - of the note being started (note_locks::pending). True if this voice
	/// had or now has any, i.e. unless nothing changed.
	bool takePending();
	/// The note about to start (note_locks::pending) locks a source's or the noise's volume up from -inf, so it
	/// sounds even if the Sound's knob values have everything off
	[[nodiscard]] static bool pendingTurnsOnASource(Sound& sound);

	// The queries below are inline (definitions in voice_note_locks_inline.h, which needs Sound complete): with no
	// locks, the common case, each is a test of the lock mask and then exactly the call the Sound makes without locks.
	// The lock-dependent logic is in the out-of-line ...Locked functions.

	/// The locks give envelope 1 a sustain (they lock one of its sustain / decay / release, and with those values it
	/// sustains). Lets a note-off through on a sound whose own values have no tails.
	[[nodiscard]] bool giveSustain(Sound& sound, ParamManagerForTimeline* paramManager) const {
		if (!hasAnyOf(kEnvelope0Tail)) [[likely]] {
			return false;
		}
		return giveSustainLocked(sound, paramManager);
	}
	/// Sound::isSourceActiveEver (through the model stack's cache), or a lock turns the source's volume up. "Ever"
	/// stays true for a source the sound has on and a lock turns off: that only skips its rendering.
	[[nodiscard]] inline bool sourceEverActive(ModelStackWithSoundFlags* modelStack, int32_t s) const;
	/// Sound::isSourceActiveCurrently, with a lock on the source's volume in place of its knob value
	[[nodiscard]] inline bool sourceActiveCurrently(Sound& sound, int32_t s,
	                                                ParamManagerForTimeline* paramManager) const;
	/// Sound::renderingOscillatorSyncCurrently, with a lock on osc B's volume in place of its knob value
	[[nodiscard]] inline bool oscSyncCurrently(Sound& sound, ParamManagerForTimeline* paramManager) const;
	/// The FM modulator's volume is above -inf, from a lock if there is one, else its knob value
	[[nodiscard]] inline bool modulatorActive(Sound& sound, int32_t m, ParamManagerForTimeline* paramManager) const;
	/// The noise volume is ever above -inf, from its knob / automation or a lock
	[[nodiscard]] inline bool noiseEverActive(ParamManager* paramManager) const;

	// Some per-voice params are also gated at Sound level on the knob value (is the filter on, is pan applied per
	// voice): locks on these have to open the gate for their voice (Sound::render).
	static constexpr uint64_t kPan = deluge::note_locks::paramMask(deluge::modulation::params::LOCAL_PAN);
	/// Sound::renderingVoicesInStereo only looks for stereo samples in the sources the knob values have on
	static constexpr uint64_t kStereo = kPan
	                                    | deluge::note_locks::paramMask(deluge::modulation::params::LOCAL_OSC_A_VOLUME,
	                                                                    deluge::modulation::params::LOCAL_OSC_B_VOLUME);
	static constexpr uint64_t kLPF = deluge::note_locks::paramMask(deluge::modulation::params::LOCAL_LPF_FREQ,
	                                                               deluge::modulation::params::LOCAL_LPF_MORPH);
	static constexpr uint64_t kHPF = deluge::note_locks::paramMask(deluge::modulation::params::LOCAL_HPF_FREQ,
	                                                               deluge::modulation::params::LOCAL_HPF_MORPH);

private:
	/// has(), for a volume param: they're all below 32, so this only reads the low word of the mask and skips a 64-bit
	/// variable shift - it's checked per voice per render
	[[nodiscard]] bool volumeLocked(int32_t param) const {
		static_assert(deluge::modulation::params::FIRST_LOCAL_NON_VOLUME <= 32);
		return ((static_cast<uint32_t>(mask()) >> param) & 1) != 0;
	}

	static constexpr uint64_t kEnvelope0Tail = deluge::note_locks::paramMask(
	    deluge::modulation::params::LOCAL_ENV_0_SUSTAIN, deluge::modulation::params::LOCAL_ENV_0_DECAY,
	    deluge::modulation::params::LOCAL_ENV_0_RELEASE);
	static constexpr uint64_t kOscBVolume =
	    deluge::note_locks::paramMask(deluge::modulation::params::LOCAL_OSC_B_VOLUME);
	static constexpr uint64_t kSourceVolumes = deluge::note_locks::paramMask(
	    deluge::modulation::params::LOCAL_OSC_A_VOLUME, deluge::modulation::params::LOCAL_OSC_B_VOLUME);
	static constexpr uint64_t kNoiseVolume =
	    deluge::note_locks::paramMask(deluge::modulation::params::LOCAL_NOISE_VOLUME);

	[[gnu::cold, gnu::noinline]] bool giveSustainLocked(Sound& sound, ParamManagerForTimeline* paramManager) const;
	[[gnu::cold, gnu::noinline]] bool sourceEverActiveLocked(ModelStackWithSoundFlags* modelStack,
	                                                         int32_t s) const; // some source volume is locked
	[[gnu::cold, gnu::noinline]] bool sourceActiveCurrentlyLocked(Sound& sound, int32_t s) const;
	[[gnu::cold, gnu::noinline]] bool oscSyncCurrentlyLocked(Sound& sound) const;
	[[gnu::cold, gnu::noinline]] bool modulatorActiveLocked(Sound& sound, int32_t m) const;
	// Only called with a lock on the param asked about
	[[gnu::cold, gnu::noinline]] bool noiseEverActiveLocked() const;
};
