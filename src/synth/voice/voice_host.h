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

#include "plugin-api/types/voice_priority.h"
#include "synth/voice/voice_clock.h"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>

/// @file
/// @brief What an instrument's voices need from the host, and what the host's cull asks of them back.
///
/// The host owns the memory and the cull policy (which sound loses a voice when the budget runs out); the instrument
/// owns its voices and answers for them. Any instrument implements this seam over its own voice storage.

namespace deluge::synth {

/// @brief How the host's cull takes the voice an instrument rates highest.
enum class CullKind : uint8_t {
	IMMEDIATE,    ///< Stopped within the block; may click.
	FAST_RELEASE, ///< Released at a given increment.
	SPEED_UP,     ///< Released, or its release sped up.
};

/// @brief What a note-on reads of its sound as the note arrives, beside its voices.
struct NoteFacts {
	bool note_tails_allowed{};               ///< Some active source sustains, so a released note keeps sounding
	std::array<bool, 2> source_active_now{}; ///< Each source sounds at the current settings
	bool fm{};                               ///< The sound's engine is FM
	bool operator==(const NoteFacts&) const = default;
};

/// @brief What a voice's kernel does for the pool: rate itself for the cull, report a cull fade, release on demand.
template <class K>
concept voice_kernel = requires(K& kernel, const K& rated, plugin::VoicePriority priority, size_t count,
                                uint32_t increment, VoiceClock clock) {
	{ rated.priority_rating(priority, count) } -> std::same_as<uint32_t>;
	{ rated.is_cull_fading() } -> std::same_as<bool>;
	{ kernel.do_immediate_release(clock) } -> std::same_as<bool>;
	{ kernel.do_fast_release(increment, clock) } -> std::same_as<bool>;
	{ kernel.speed_up_release(clock) } -> std::same_as<bool>;
};

/// @brief What the host gives an instrument's voices.
///
/// Slots are host memory, constructed by acquire(). The list is the host's type (a vector of slot pointers it reserves,
/// so it never moves while the render walks it). Exclusion keeps the render out while the list changes; it nests, as
/// the instrument takes it inside its own, and is free on the audio tier.
template <class H>
concept voice_host = requires(H& host, H::Slot& slot, uint32_t cost) {
	typename H::Slot;
	typename H::List;
	typename H::Exclusion;
	{ host.acquire() } -> std::same_as<typename H::Slot*>; ///< null when there is no memory
	{ host.unassign(slot) };                               ///< frees the voice's resources while it is still listed
	{ host.release(slot) } noexcept;                       ///< destroys the slot and returns its memory
	{ host.admit(cost) };                                  ///< the budget; may cull another sound's voice
	{ host.voice_starting() };                             ///< a voice is about to start
	{ host.voices_changed() };                             ///< after an edit, under the exclusion
	{ host.note_edit() };                                  ///< the structural-edit note
	{ host.clock() } -> std::same_as<VoiceClock>;
};

} // namespace deluge::synth
