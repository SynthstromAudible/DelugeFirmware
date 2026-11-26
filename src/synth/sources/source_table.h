/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"
#include "dsp/core/phasor.h"
#include "dsp/fixed/noise/cong.h"
#include "synth/engines/engine_table.h"
#include "synth/sources/dx7/dx7_source.h"
#include "synth/sources/input/input_source.h"
#include "synth/sources/sample/sample_source.h"
#include "synth/sources/supersaw/supersaw_source.h"
#include "synth/sources/va/va_shapes.h"
#include "synth/sources/va/virtual_analog_source.h"
#include "synth/sources/wavetable/wavetable_source.h"
#include "synth/table_rows.h"
#include <tuple>
#include <type_traits>
#include <variant>

/// @file
/// @brief The kinds of source an oscillator slot plays, one row each: the slot's note-on, build, lookup, binding and
/// release, and the engines' own-rate test, are generated from these rows. Adding a source is adding a row.
///
/// A ported row's source is bound to a firmware port for the voice's life (sample, DX7, live input); a held row's
/// source is built per note into the slot's held oscillator (the waves). Either sounds only where the engine table's
/// row for the mode holds the oscillators (holds_oscillators).

namespace deluge::synth {

/// @brief The firmware ports a slot's port-backed sources play through; they outlive the slot.
struct SourcePorts {
	SamplePlaybackPort* sample{};    ///< the sample reader and its pool
	DxVoicePort* dx{};               ///< the pooled DX7 voice
	AudioInputPort* input{};         ///< the engine's live input
	const LiveInputs* live_inputs{}; ///< the oscillator's live histories
};

/// @brief What a held source's build reads.
struct HeldBuild {
	OscType type;            ///< the slot's type as set
	dsp::UQ<32> seed;        ///< the wave's zero crossing plus the retrigger phase
	bool retrigger;          ///< a retrigger phase is set (the oscillator is not free-running)
	bool reset_everything;   ///< a new voice
	dsp::UQ<32> osc_pos;     ///< the free-running phase randomize_osc_phases left
	dsp::fixed::Cong* noise; ///< the synth's noise, for a free-running supersaw's side saws
};

/// @brief The sample row: a file played through its port, at its own rate; its note-on is the slot's whole note-on.
struct SampleKind {
	using Source = SampleSource;
	static constexpr bool kHeld = false;
	static constexpr bool kOwnsNote = true;
	static constexpr bool kOwnRate = true;
	/// @brief Whether this row plays @p type.
	[[nodiscard]] static constexpr bool plays(OscType type) noexcept { return type == OscType::SAMPLE; }
	/// @brief Binds @p source to its port in @p ports.
	static void bind(Source& source, const SourcePorts& ports) { source.bind_port(ports.sample); }
	/// @brief Starts @p note on @p source.
	/// @return False when the note cannot play.
	[[nodiscard]] static bool note_on(Source& source, const SourceNote& note) { return source.note_on(note); }
	/// @brief The note-off for @p source, while the slot is @p active.
	/// @return False when the release ends the note at once and the slot unassigns.
	[[nodiscard]] static bool release(Source& source, const SourceRelease& release, bool active) {
		return !active || source.release(release);
	}
};

/// @brief The DX7 row: a pooled DX7 voice played through its port.
struct Dx7Kind {
	using Source = DX7Source;
	static constexpr bool kHeld = false;
	static constexpr bool kOwnsNote = false;
	static constexpr bool kOwnRate = false;
	/// @brief Whether this row plays @p type.
	[[nodiscard]] static constexpr bool plays(OscType type) noexcept { return type == OscType::DX7; }
	/// @brief Binds @p source to its port in @p ports.
	static void bind(Source& source, const SourcePorts& ports) { source.bind_port(ports.dx); }
	/// @brief Starts @p note on @p source.
	/// @return False when the note cannot play.
	[[nodiscard]] static bool note_on(Source& source, const SourceNote& note) { return source.note_on(note); }
	/// @brief The note-off for @p source.
	/// @return True: the DX7 releases on its own envelopes, whatever the slot's state, and never ends the note here.
	[[nodiscard]] static bool release(Source& source, const SourceRelease& /*release*/, bool /*active*/) {
		(void)source.release({});
		return true;
	}
};

/// @brief The live-input row: the audio input played through its port, at its own rate.
struct InputKind {
	using Source = InputSource;
	static constexpr bool kHeld = false;
	static constexpr bool kOwnsNote = false;
	static constexpr bool kOwnRate = true;
	/// @brief Whether this row plays @p type.
	[[nodiscard]] static constexpr bool plays(OscType type) noexcept {
		return type == OscType::INPUT_L || type == OscType::INPUT_R || type == OscType::INPUT_STEREO;
	}
	/// @brief Binds @p source to its port in @p ports.
	static void bind(Source& source, const SourcePorts& ports) { source.bind_port(ports.input, ports.live_inputs); }
	/// @brief Starts @p note on @p source, latching its input channel.
	/// @return True: the note-on only latches the input channel.
	[[nodiscard]] static bool note_on(Source& source, const SourceNote& note) {
		(void)source.note_on(note);
		return true;
	}
	/// @brief The note-off for a live input.
	/// @return True: a live input has nothing to release.
	[[nodiscard]] static bool release(Source& /*source*/, const SourceRelease& /*release*/, bool /*active*/) {
		return true;
	}
};

/// @brief The virtual-analog waves.
struct VirtualAnalogKind {
	using Source = VirtualAnalogSource;
	static constexpr bool kHeld = true;
	static constexpr bool kOwnRate = false;
	/// @brief Whether this row plays @p type: the virtual-analog waves.
	[[nodiscard]] static bool plays(OscType type) { return va_shape::is_virtual_analog(type); }
	/// @brief A retrigger seeds the wave at its zero crossing; a free-running wave keeps a held one's phase and wave,
	/// and a new voice starts it at the random phase.
	/// @param held The slot's held oscillator.
	/// @param build What the note's build reads.
	template <class Holder>
	static void build(Holder& held, const HeldBuild& build) {
		const va_shape::Wave wave = va_shape::wave_for(build.type);
		if (build.retrigger) {
			held.template emplace<Source>(wave, dsp::Phasor{build.seed, dsp::UQ<32>{}});
		}
		else if (build.reset_everything || !std::holds_alternative<Source>(held)) {
			held.template emplace<Source>(wave, dsp::Phasor{build.osc_pos, dsp::UQ<32>{}});
		}
	}
};

/// @brief The wavetable.
struct WavetableKind {
	using Source = WavetableSource;
	static constexpr bool kHeld = true;
	static constexpr bool kOwnRate = false;
	/// @brief Whether this row plays @p type.
	[[nodiscard]] static constexpr bool plays(OscType type) noexcept { return type == OscType::WAVETABLE; }
	/// @brief As the virtual-analog waves, with no wave to choose.
	/// @param held The slot's held oscillator.
	/// @param build What the note's build reads.
	template <class Holder>
	static void build(Holder& held, const HeldBuild& build) {
		if (build.retrigger) {
			held.template emplace<Source>(dsp::Phasor{build.seed, dsp::UQ<32>{}});
		}
		else if (build.reset_everything || !std::holds_alternative<Source>(held)) {
			held.template emplace<Source>(dsp::Phasor{build.osc_pos, dsp::UQ<32>{}});
		}
	}
};

/// @brief The supersaw.
struct SupersawKind {
	using Source = SupersawSource;
	static constexpr bool kHeld = true;
	static constexpr bool kOwnRate = false;
	/// @brief Whether this row plays @p type.
	[[nodiscard]] static constexpr bool plays(OscType type) noexcept { return type == OscType::SUPERSAW; }
	/// @brief A retrigger starts the side saws at fixed offsets from the centre one, so every note starts the same way;
	/// a free-running one on a new voice draws random side saws like the centre one, else every note's side saws would
	/// start from the same place and sweep the same way.
	/// @param held The slot's held oscillator.
	/// @param build What the note's build reads.
	template <class Holder>
	static void build(Holder& held, const HeldBuild& build) {
		using dsp::fixed::Supersaw;
		if (build.retrigger) {
			held.template emplace<Source>(build.seed, Supersaw::side_phases_from(build.seed.raw()));
		}
		else if (build.reset_everything || !std::holds_alternative<Source>(held)) {
			Supersaw::SidePhases side_phases;
			for (uint32_t& phase : side_phases) {
				phase = build.noise->next();
			}
			held.template emplace<Source>(build.osc_pos, side_phases);
		}
	}
};

/// @brief The slot's source kinds: the ported ones first, in the order bind and unassign walk them, then the held.
using SourceKinds = std::tuple<SampleKind, Dx7Kind, InputKind, VirtualAnalogKind, WavetableKind, SupersawKind>;

namespace detail {

template <bool Held, class Kinds>
struct SourcesOf;

template <bool Held>
struct SourcesOf<Held, std::tuple<>> {
	using type = std::tuple<>;
};

template <bool Held, class Kind, class... Rest>
struct SourcesOf<Held, std::tuple<Kind, Rest...>> {
	using rest = SourcesOf<Held, std::tuple<Rest...>>::type;
	using type = std::conditional_t<
	    Kind::kHeld == Held,
	    decltype(std::tuple_cat(std::declval<std::tuple<typename Kind::Source>>(), std::declval<rest>())), rest>;
};

template <class Tuple>
struct HeldVariantOf;

template <class... Sources>
struct HeldVariantOf<std::tuple<Sources...>> {
	using type = std::variant<std::monostate, Sources...>;
};

template <class Kinds>
struct PortedKindsOf;

template <class... Kinds>
struct PortedKindsOf<std::tuple<Kinds...>> {
	using type =
	    decltype(std::tuple_cat(std::declval<std::conditional_t<Kinds::kHeld, std::tuple<>, std::tuple<Kinds>>>()...));
};

template <class Kinds>
struct HeldKindsOf;

template <class... Kinds>
struct HeldKindsOf<std::tuple<Kinds...>> {
	using type =
	    decltype(std::tuple_cat(std::declval<std::conditional_t<Kinds::kHeld, std::tuple<Kinds>, std::tuple<>>>()...));
};

} // namespace detail

/// @brief The held oscillator: none, or one of the held rows' sources, in the table's order.
using HeldOscillator = detail::HeldVariantOf<detail::SourcesOf<true, SourceKinds>::type>::type;
/// @brief The ported sources a slot keeps bound, one per ported row, in the table's order.
using PortedSources = detail::SourcesOf<false, SourceKinds>::type;
/// @brief The ported rows, in the table's order.
using PortedKinds = detail::PortedKindsOf<SourceKinds>::type;
/// @brief The held rows, in the table's order.
using HeldKinds = detail::HeldKindsOf<SourceKinds>::type;

/// @brief Whether a source of @p type plays at its own rate in @p mode, so its neutral increment is the root note's: a
/// ported row with kOwnRate, where the mode's engine holds the oscillators.
/// @return True for a sample or a live input outside FM.
[[nodiscard]] inline bool plays_at_own_rate(OscType type, SynthMode mode) {
	return holds_oscillators(mode)
	       && find_row<PortedKinds>([&]<class Kind>(Kind) { return Kind::kOwnRate && Kind::plays(type); });
}

} // namespace deluge::synth
