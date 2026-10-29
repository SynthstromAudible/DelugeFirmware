// Based on Dexed's dx7note (Copyright 2016-2017 Pascal Gauthier, Copyright 2012 Google Inc., Apache License 2.0).

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

#include "dsp-next/core/control.h"
#include "dsp-next/core/types.h"
#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/dx7/algorithms.h"
#include "dsp-next/fixed/dx7/envelope.h"
#include "dsp-next/fixed/dx7/graph.h"
#include "dsp-next/fixed/dx7/lfo_delay.h"
#include "dsp-next/fixed/dx7/lookup.h"
#include "dsp-next/fixed/dx7/pitch_envelope.h"
#include "dsp-next/fixed/dx7/tables.h"
#include "dsp-next/fixed/dx7/voice_scaling.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <math.h> // NOLINT(*-deprecated-headers): the amplitude curve's exp resolves as the engine's always did
#include <tuple>

/// @file
/// @brief A DX7 voice: one note of a patch, its envelopes, pitches and LFO delay around the six-operator graph.
namespace deluge::dsp::fixed::dx7 {

/// @brief A DX7 patch's 156 bytes, in the patch's layout: six ops of 21 bytes (op 6 first), then the globals.
using PatchBytes = std::array<uint8_t, 156>;

/// @brief One note of a DX7 patch.
/// @details A component, not a kernel. Its verbs are @ref start, @ref release, @ref retune and @ref renderBlock, each
/// given the patch as it stands (@ref Settings, a snapshot the owner takes per block); the voice keeps only what the
/// note carries between blocks: the graph's operators, the envelopes, the base pitches, the random detunes, the output
/// levels and rate scalings, and the note-on velocity. The patch's own LFO is the owner's: it keys it at note-on when
/// @ref key_sync_lfo says so, and passes its value in each block.
/// @tparam Noise The noise policy: `Noise::render()` gives one draw. A note-on draws six detunes (op order), then
/// six phases unless the oscillators are key-synced; a block draws nothing.
template <class Noise>
class Voice {
public:
	/// @brief The patch and the note, as they stand when the owner calls.
	struct Settings {
		PatchBytes patch{};             ///< A snapshot of the patch's bytes.
		int32_t midinote = 60;          ///< The note.
		int32_t velocity = 100;         ///< The note-on velocity, 0 to 127.
		int32_t random_detune = 0;      ///< The patch's random-detune amount.
		Engine engine = Engine::MODERN; ///< The engine the patch renders in.
		const Tables* tables = nullptr; ///< The engine's tables.
	};

	/// @brief What a block takes from outside the patch.
	struct BlockInputs {
		int32_t base_pitch = 0; ///< The note's pitch, Q24 octaves, relative to the oscillators' base.
		int32_t lfo_value = 0;  ///< The patch's LFO, Q24.
		int32_t eg_mod = 127;   ///< The envelope's amplitude-modulation floor, 0 to 127.
		int32_t amp_mod = 0;    ///< Added to each modulated op's level, scaled by its sensitivity.
		int32_t vel_mod = 0;    ///< Added to each op's level, scaled by its velocity sensitivity.
		int32_t rate_mod = 0;   ///< Added to each envelope's stage rates.
	};

	/// @brief Note-on: the envelopes start, the base pitches are drawn (six detune draws, op order), the pitch
	/// envelope starts, the operators restart at zero phase (oscillator key sync) or at six drawn phases, and the
	/// LFO delay starts. The feedback history carries over from the voice's last note.
	void start(const Settings& s) {
		const PatchBytes& p = s.patch;
		velocity_ = static_cast<uint8_t>(s.velocity);
		const int32_t log_freq = note_to_log_freq(s.midinote);
		for (std::size_t op = 0; op < 6; ++op) {
			const std::size_t off = op * kOpBytes;
			scale_op(s, op, s.velocity);
			env_[op].configure(env_settings(s, op, 0));
			env_[op].template on<&Envelope::start>();
			detune_[op] = static_cast<int16_t>(Noise::render() >> 16);
			base_pitch_[op] =
			    osc_freq(log_freq, p[off + 17], p[off + 18], p[off + 19], p[off + 20], detune_[op], s.random_detune);
		}
		pitch_env_.configure({pitch_params(s)});
		pitch_env_.template on<&PitchEnvelope::start>();

		std::array<q31, 6> phases{};
		if (p[kOscSync] == 0) {
			for (q31& phase : phases) {
				phase = Noise::render();
			}
		}
		graph_.restart(phases);

		lfo_delay_.configure({p[kLfoDelay]});
		lfo_delay_.reset();
	}

	/// @brief Key-up: every envelope and the pitch envelope release.
	void release(const Settings& s) {
		for (std::size_t op = 0; op < 6; ++op) {
			env_[op].configure(env_settings(s, op, 0));
			env_[op].template on<&Envelope::release>();
		}
		pitch_env_.configure({pitch_params(s)});
		pitch_env_.template on<&PitchEnvelope::release>();
	}

	/// @brief The patch changed mid-note: the base pitches (with the note's detune draws), the output levels at the
	/// note-on velocity, and each envelope's retune. The pitch envelope is left as it is.
	void retune(const Settings& s) {
		const PatchBytes& p = s.patch;
		const int32_t log_freq = note_to_log_freq(s.midinote);
		for (std::size_t op = 0; op < 6; ++op) {
			const std::size_t off = op * kOpBytes;
			base_pitch_[op] =
			    osc_freq(log_freq, p[off + 17], p[off + 18], p[off + 19], p[off + 20], detune_[op], s.random_detune);
			scale_op(s, op, velocity_);
			env_[op].configure(env_settings(s, op, 0));
			env_[op].template on<&Envelope::retune>();
		}
	}

	/// @brief One block into @p out, using @p buses (lent by the owner, shared by every voice) for the graph's two
	/// intermediate buses. Carriers add into @p out, so the caller zeroes it first.
	/// @return Whether the voice sounds on: the key is down, or an op's gain is at or above kGainLevelThresh. That
	/// threshold is the modern engine's; a MkI op's gain is an attenuation, so a MkI voice's ops always pass it, as the
	/// engine has always counted them.
	[[nodiscard]] bool renderBlock(const Settings& s, Buffer out, const BlockInputs& in, Buses& buses) {
		const PatchBytes& p = s.patch;
		const std::size_t n = out.size();
		const int32_t lfo_delay = lfo_delay_.advance(n);
		int32_t lfo_val = in.lfo_value;

		// pitch: the LFO's depth after its delay, at the patch's sensitivity, plus the pitch envelope
		const int32_t pitch_depth = (p[kPitchModDepth] * 165) >> 6;
		const int32_t pitch_sens = kPitchModSens[p[kPitchModSensitivity] & 7];
		const uint32_t pmd = static_cast<uint32_t>(pitch_depth) * static_cast<uint32_t>(lfo_delay); // Q32
		const int32_t senslfo = pitch_sens * (lfo_val - (1 << 23));
		// |pmd · senslfo| < 2^32 · 2^31, so the shifted product is under 2^24 and abs never meets INT32_MIN
		const int32_t pmod = std::abs(static_cast<int32_t>((static_cast<int64_t>(pmd) * senslfo) >> 39));
		pitch_env_.configure({pitch_params(s)});
		int32_t pitch_mod = pitch_env_.advance(n) + (pmod * (senslfo < 0 ? -1 : 1));
		pitch_mod += in.base_pitch;

		// amplitude: the LFO's depth after its delay, inverted, no shallower than the envelope's floor
		lfo_val = (1 << 24) - lfo_val;
		const int32_t amp_depth = (p[kAmpModDepth] * 165) >> 6;
		uint32_t amd_mod = static_cast<uint32_t>((static_cast<int64_t>(amp_depth) * lfo_delay) >> 8); // Q24
		amd_mod = static_cast<uint32_t>((static_cast<int64_t>(amd_mod) * lfo_val) >> 24);
		const uint32_t eg_floor = static_cast<uint32_t>(in.eg_mod + 1) << 17;
		amd_mod = std::max((1U << 24) - eg_floor, amd_mod);

		Graph::Settings graph{
		    .engine = s.engine, .algorithm = p[kAlgorithm], .feedback = p[kFeedback], .tables = s.tables};
		for (std::size_t op = 0; op < 6; ++op) {
			const std::size_t off = op * kOpBytes;
			if (((p[kOpSwitch] >> op) & 1) == 0) {
				// a switched-off op's envelope still runs
				env_[op].configure(env_settings(s, op, 0));
				std::ignore = env_[op].advance(n);
				continue;
			}
			graph.freq[op] = freq_lookup(*s.tables, p[off + 17] != 0 ? base_pitch_[op] : base_pitch_[op] + pitch_mod);
			env_[op].configure(env_settings(s, op, in.rate_mod));
			int32_t level = env_[op].advance(n);
			const auto ams = static_cast<int32_t>(kAmpModSens[p[off + 14] & 3]);
			if (ams != 0) {
				const auto sensamp = static_cast<uint32_t>((static_cast<uint64_t>(amd_mod) * ams) >> 24);
				const auto pt = static_cast<uint32_t>(exp(((float)sensamp) / 262144 * 0.07 + 12.2));
				const auto ldiff =
				    static_cast<uint32_t>((static_cast<uint64_t>(level) * (static_cast<uint64_t>(pt) << 4)) >> 28);
				level = wrapping_sub(level, static_cast<int32_t>(ldiff));
				level = wrapping_add(level, (ams >> 16) * in.amp_mod);
			}
			graph.level_in[op] = wrapping_add(level, p[off + 15] * in.vel_mod);
		}
		graph_.renderBlock(graph, out, buses);

		bool any_active_op = false;
		for (std::size_t op = 0; op < 6; ++op) {
			any_active_op = any_active_op || graph_.gain_out(op) >= kGainLevelThresh;
		}
		return pitch_env_.state().down || any_active_op;
	}

	/// @brief Whether the patch keys its LFO at note-on (byte 141).
	[[nodiscard]] static bool key_sync_lfo(const Settings& s) { return s.patch[kLfoSync] != 0; }

private:
	static constexpr std::size_t kOpBytes = 21;              ///< Each op's bytes.
	static constexpr std::size_t kPitchEnv = 126;            ///< The pitch envelope's rates, then levels.
	static constexpr std::size_t kAlgorithm = 134;           ///< 0 to 31.
	static constexpr std::size_t kFeedback = 135;            ///< 0 to 7.
	static constexpr std::size_t kOscSync = 136;             ///< Oscillator key sync.
	static constexpr std::size_t kLfoDelay = 138;            ///< 0 to 99.
	static constexpr std::size_t kPitchModDepth = 139;       ///< 0 to 99.
	static constexpr std::size_t kAmpModDepth = 140;         ///< 0 to 99.
	static constexpr std::size_t kLfoSync = 141;             ///< LFO key sync.
	static constexpr std::size_t kPitchModSensitivity = 143; ///< 0 to 7.
	static constexpr std::size_t kOpSwitch = 155;            ///< One bit per op: on.

	/// @brief Op @p op's output level and rate scaling for the note, at @p velocity.
	void scale_op(const Settings& s, std::size_t op, int32_t velocity) {
		const PatchBytes& p = s.patch;
		const std::size_t off = op * kOpBytes;
		int32_t level = Envelope::scale_output_level(p[off + 16]);
		level += scale_level(s.midinote, p[off + 8], p[off + 9], p[off + 10], p[off + 11], p[off + 12]);
		level = std::min(127, level) << 5;
		level += scale_velocity(velocity, p[off + 15]);
		output_level_[op] = std::max(0, level);
		rate_scaling_[op] = scale_rate(s.midinote, p[off + 13]);
	}

	/// @brief Op @p op's envelope settings: its patch bytes, its output level and rate scaling, and @p extra_rate.
	[[nodiscard]] Envelope::Settings env_settings(const Settings& s, std::size_t op, int32_t extra_rate) const {
		const uint8_t* e = &s.patch[op * kOpBytes];
		return {.params = {{e[0], e[1], e[2], e[3]}, {e[4], e[5], e[6], e[7]}},
		        .output_level = output_level_[op],
		        .rate_scaling = rate_scaling_[op],
		        .extra_rate = extra_rate};
	}

	/// @brief The pitch envelope's bytes.
	[[nodiscard]] static EnvelopeParams pitch_params(const Settings& s) {
		const uint8_t* e = &s.patch[kPitchEnv];
		return {{e[0], e[1], e[2], e[3]}, {e[4], e[5], e[6], e[7]}};
	}

	Graph graph_{};                          ///< The six operators and the feedback history.
	std::array<Control<Envelope>, 6> env_{}; ///< Each op's envelope.
	Control<PitchEnvelope> pitch_env_{};     ///< The pitch envelope.
	Control<LfoDelay> lfo_delay_{};          ///< The LFO's delay, configured at note-on only.
	std::array<int32_t, 6> base_pitch_{};    ///< Each op's pitch offset, Q24 octaves.
	std::array<int16_t, 6> detune_{};        ///< Each op's random detune draw.
	std::array<int32_t, 6> output_level_{};  ///< Each op's output level for the note, in microsteps.
	std::array<int32_t, 6> rate_scaling_{};  ///< Each op's rate scaling for the note, in qRate units.
	uint8_t velocity_ = 0;                   ///< The note-on velocity.
};

} // namespace deluge::dsp::fixed::dx7
