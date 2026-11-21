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

/// @brief What a note-on or note-off reads of its sound as the note arrives, beside its voices.
struct NoteFacts {
	bool note_tails_allowed{};               ///< Some active source sustains, so a released note keeps sounding
	std::array<bool, 2> source_active_now{}; ///< Each source sounds at the current settings
	bool fm{};                               ///< The sound's engine is FM
	/// As note_tails_allowed, but a one-shot sample with a loop end counts as sustaining
	bool note_tails_counting_loops{};
	bool arpeggiating{}; ///< The sound's arpeggiator is on
	/// A released note falls back to one still held: LEGATO or MONO, not a drum, and note_tails_counting_loops
	bool falls_back{};
	bool operator==(const NoteFacts&) const = default; ///< Field by field
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
/// the instrument takes it inside its own, and is free on the audio tier. Context is what the host needs to free a
/// voice's resources (its value-initialised form when the caller has none), handed through the pool's free paths.
template <class H>
concept voice_host = requires(H& host, H::Slot& slot, H::Context context, uint32_t cost) {
	typename H::List;
	typename H::Exclusion;
	requires std::copyable<typename H::Context> && std::default_initializable<typename H::Context>;
	{ host.acquire() } -> std::same_as<typename H::Slot*>; ///< null when there is no memory
	{ host.unassign(slot, context) };                      ///< frees the voice's resources while it is still listed
	{ host.release(slot) } noexcept;                       ///< destroys the slot and returns its memory
	{ host.admit(cost) };                                  ///< the budget; may cull another sound's voice
	{ host.voice_starting() };                             ///< a voice is about to start
	{ host.voices_changed(context) };                      ///< after an edit, under the exclusion
	{ host.note_edit() };                                  ///< the structural-edit note
	{ host.clock() } -> std::same_as<VoiceClock>;
};

/// @brief What a host gives the pool's note path besides voice_host: the note's setup, the voice order, the cost
/// clock and the note-on's own bracket. Config, Event and Noise are the host's types, handed to the kernel.
template <class H>
concept note_host = voice_host<H>
                    && requires(H& host, H::Slot& slot, H::Context context, const H::Event& event, uint32_t length,
                                int32_t late, int32_t note) {
	                       typename H::Config;
	                       typename H::Noise;
	                       {
		                       host.prepare(slot, context, event, length, late)
	                       }; ///< a note-on's setup; binds the note's files to the part
	                       { host.prepare_change(slot, context, note) }; ///< a note change's setup, the files kept
	                       { host.renew(slot) } noexcept;                ///< rebuilds a voice in place for a steal
	                       { host.next_voice_order() } -> std::same_as<uint32_t>;
	                       {
		                       host.cycles()
	                       } -> std::same_as<uint32_t>; ///< the cycle counter the admit cost is read from
	                       { host.note_started(slot) }; ///< after the kernel's note-on (the parameter dump)
	                       { host.noise() } -> std::same_as<typename H::Noise&>;
	                       { host.steal_increment() } -> std::same_as<uint32_t>; ///< the voice-limit steal's fade
	                       {
		                       host.timed([] { return true; })
	                       } -> std::same_as<bool>; ///< runs a note-on inside the host's bench bracket
                       };

} // namespace deluge::synth
