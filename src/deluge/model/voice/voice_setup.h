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

#include "model/voice/voice_inputs.h"

class ModelStackWithSoundFlags;
class ParamManagerForTimeline;
class Sound;
class VoiceSlot;

/// @file What the voice kernel's lifecycle reads from a Sound, built where the Sound is: a note-on's or note change's
/// NoteSetup and a note-off's ReleaseInfo.

/// @brief The setup for @p voice's next note on @p sound.
/// @details A note-on (@p forNoteChange false) first finds each SAMPLE or WAVETABLE source's range for the note and
/// takes its file into the voice's playback guide, with a time-stretched sample's sync and a sample's playback bounds.
/// A note change keeps the files the note started with.
deluge::synth::NoteSetup note_setup_for(VoiceSlot& voice, Sound& sound, ParamManagerForTimeline* paramManager,
                                        ModelStackWithSoundFlags& flags, int32_t noteAfterArpeggiation,
                                        uint32_t sampleSyncLength, int32_t ticksLate, bool forNoteChange);

/// @brief What a note-off on @p sound needs.
deluge::synth::ReleaseInfo release_info_for(Sound& sound, ParamManagerForTimeline* paramManager, bool noteTailsAllowed);
