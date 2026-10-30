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

/// @file
/// @brief The DX7 workloads: every algorithm under both engines with and without feedback, the MkI feedback stacks,
/// engine and patch changes mid-note, every LFO wave, the LFO delay and key syncs, random detune, fixed frequencies,
/// release tails and a retrigger. Each runs through the reference voice block by block as the voice adapter drives it,
/// and folds into three digests: the output words, the voice and patch state after every block, and whether the
/// voice is still alive.

#pragma once

#include "dsp-next/core/mono.h"
#include "dsp-next/fixed/dx7/modern_vector_operator.h"
#include "dsp-next/fixed/dx7/tables.h"
#include "dsp-next/fixed/noise/noise.h"
#include "reference_dx7.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace dx7_workloads {

using Digest = dx7_reference::Digest;

/// @brief Every workload, in all_workloads() order: 128 algorithm workloads (32 algorithms × 2 engines × 2 feedbacks),
/// then the named ones.
enum class WorkloadId : uint16_t {
	ALG_FIRST = 0,
	ALG_LAST = 127,
	AUTO_FB2,
	AUTO_FB3,
	MKI_ALG32_FB,
	MODERN_ALG4_FB,
	MODERN_ALG6_FB,
	ENGINE_SWITCH_MID_NOTE,
	PATCH_EDIT_MID_NOTE,
	LFO_WAVE_0,
	LFO_WAVE_1,
	LFO_WAVE_2,
	LFO_WAVE_3,
	LFO_WAVE_4,
	LFO_WAVE_5,
	LFO_DELAY_KEY_SYNC,
	OSC_KEY_SYNC_OFF,
	RANDOM_DETUNE,
	FIXED_FREQ,
	RELEASE_TAIL_MODERN,
	RELEASE_TAIL_MKI,
	RETRIGGER_KEEPS_FEEDBACK,
	kCount,
};
inline constexpr std::size_t kWorkloadCount = static_cast<std::size_t>(WorkloadId::kCount);

/// @return The algorithm workload's id for algorithm index @p a, engine MKI or MODERN, feedback 7 or 0.
[[nodiscard]] constexpr WorkloadId alg_id(int a, bool mki, bool fb) {
	return static_cast<WorkloadId>((a * 4) + (mki ? 2 : 0) + (fb ? 1 : 0));
}

/// @return @p id's name: the enumerator's spelling, an algorithm workload as ALG_<a>_<ENGINE>_FB<f>.
inline const char* name(WorkloadId id) {
	static const std::array<std::string, kWorkloadCount> names = [] {
		std::array<std::string, kWorkloadCount> n{};
		for (int a = 0; a < 32; ++a) {
			for (const bool mki : {false, true}) {
				for (const bool fb : {false, true}) {
					n[static_cast<std::size_t>(alg_id(a, mki, fb))] =
					    "ALG_" + std::to_string(a) + (mki ? "_MKI" : "_MODERN") + (fb ? "_FB7" : "_FB0");
				}
			}
		}
		const char* named[] = {"AUTO_FB2",
		                       "AUTO_FB3",
		                       "MKI_ALG32_FB",
		                       "MODERN_ALG4_FB",
		                       "MODERN_ALG6_FB",
		                       "ENGINE_SWITCH_MID_NOTE",
		                       "PATCH_EDIT_MID_NOTE",
		                       "LFO_WAVE_0",
		                       "LFO_WAVE_1",
		                       "LFO_WAVE_2",
		                       "LFO_WAVE_3",
		                       "LFO_WAVE_4",
		                       "LFO_WAVE_5",
		                       "LFO_DELAY_KEY_SYNC",
		                       "OSC_KEY_SYNC_OFF",
		                       "RANDOM_DETUNE",
		                       "FIXED_FREQ",
		                       "RELEASE_TAIL_MODERN",
		                       "RELEASE_TAIL_MKI",
		                       "RETRIGGER_KEEPS_FEEDBACK"};
		for (std::size_t i = 0; i < std::size(named); ++i) {
			n[static_cast<std::size_t>(WorkloadId::AUTO_FB2) + i] = named[i];
		}
		return n;
	}();
	return names.at(static_cast<std::size_t>(id)).c_str();
}

/// @brief SeededNoise behind a static policy: the reference voice draws its detunes and phases from it.
struct SeededStatic {
	inline static deluge::dsp::fixed::SeededNoise noise{1};
	inline static std::size_t count = 0;
	static void reseed(uint32_t seed) {
		noise = deluge::dsp::fixed::SeededNoise{seed};
		count = 0;
	}
	static int32_t render() {
		++count;
		return noise.render();
	}
	static std::size_t draws() { return count; }
};

using Patch = std::array<uint8_t, 156>;

/// @brief Something that happens to the voice before a block renders.
struct Event {
	std::size_t block;
	enum Kind : uint8_t { KEYUP, PATCH_EDIT, ENGINE_SWITCH, RETRIGGER } kind;
	uint8_t engine;
	Patch patch;
};

struct Workload {
	WorkloadId id;
	Patch patch;
	uint8_t engine;
	int32_t random_detune;
	int midinote;
	int velocity;
	uint32_t phase_increment;
	int32_t amp_mod_param;
	std::vector<uint16_t> blocks;
	std::vector<Event> events;
	uint32_t seed;
};

/// @return Every block length from 1 to 132 once, in a fixed order that mixes short and long: 8,778 samples.
inline std::span<const uint16_t> block_lengths_1_to_132() {
	static const std::vector<uint16_t> lengths = [] {
		std::vector<uint16_t> l;
		for (uint16_t k = 0; k < 132; ++k) {
			l.push_back(static_cast<uint16_t>(((k * 89U) % 132U) + 1U)); // 89 is coprime to 132
		}
		return l;
	}();
	return lengths;
}

// The patch bytes this file sets, per op (21 bytes each) and global (from 126).
inline constexpr std::size_t kOpBytes = 21;
inline constexpr std::size_t kOutputLevel = 16;
inline constexpr std::size_t kOscMode = 17;
inline constexpr std::size_t kCoarse = 18;
inline constexpr std::size_t kDetune = 20;
inline constexpr std::size_t kKeyVelSens = 15;
inline constexpr std::size_t kAmpModSens = 14;
inline constexpr std::size_t kAlgorithm = 134;
inline constexpr std::size_t kFeedback = 135;
inline constexpr std::size_t kOscKeySync = 136;
inline constexpr std::size_t kLfoRate = 137;
inline constexpr std::size_t kLfoDelay = 138;
inline constexpr std::size_t kLfoPmd = 139;
inline constexpr std::size_t kLfoAmd = 140;
inline constexpr std::size_t kLfoKeySync = 141;
inline constexpr std::size_t kLfoWave = 142;
inline constexpr std::size_t kLfoPms = 143;
inline constexpr std::size_t kOpSwitch = 155;

/// @return The init voice with every op sounding: output level 99, rates 99/50/40/30, levels 99/80/60/0, coarse 1 to
/// 6 across the ops, detune 7, key-velocity sensitivity 3, every op switched on.
inline Patch all_ops_sounding() {
	Patch p = deluge::instrument::kDx7InitVoice;
	for (std::size_t op = 0; op < 6; ++op) {
		uint8_t* o = &p[op * kOpBytes];
		o[0] = 99;
		o[1] = 50;
		o[2] = 40;
		o[3] = 30;
		o[4] = 99;
		o[5] = 80;
		o[6] = 60;
		o[7] = 0;
		o[kOutputLevel] = 99;
		o[kCoarse] = static_cast<uint8_t>(op + 1);
		o[kDetune] = 7;
		o[kKeyVelSens] = 3;
	}
	p[kOpSwitch] = 63;
	return p;
}

/// @brief Middle C as the voice source's phase increment: 261.63 Hz at 44.1 kHz in a 32-bit phase.
inline constexpr uint32_t kMiddleC = 25480439;

/// @return A workload over @p patch at @p engine, the standard blocks and note, seeded by its id.
inline Workload make(WorkloadId id, const Patch& patch, uint8_t engine) {
	const std::span<const uint16_t> lengths = block_lengths_1_to_132();
	return {.id = id,
	        .patch = patch,
	        .engine = engine,
	        .random_detune = 0,
	        .midinote = 60,
	        .velocity = 100,
	        .phase_increment = kMiddleC,
	        .amp_mod_param = 0,
	        .blocks = std::vector<uint16_t>(lengths.begin(), lengths.end()),
	        .events = {},
	        .seed = static_cast<uint32_t>(id) + 1U};
}

/// @return @p patch at algorithm index @p a and feedback @p fb.
inline Patch with_algorithm(Patch patch, int a, int fb) {
	patch[kAlgorithm] = static_cast<uint8_t>(a);
	patch[kFeedback] = static_cast<uint8_t>(fb);
	return patch;
}

/// @brief Every workload, in WorkloadId order.
inline const std::vector<Workload>& all_workloads() {
	static const std::vector<Workload> all = [] {
		std::vector<Workload> w;
		const Patch base = all_ops_sounding();

		for (int a = 0; a < 32; ++a) {
			for (const bool mki : {false, true}) {
				for (const bool fb : {false, true}) {
					Patch p = with_algorithm(base, a, fb ? 7 : 0);
					p[2 * kOpBytes + kOutputLevel] = 0; // one op gated in every algorithm
					Workload wl = make(alg_id(a, mki, fb), p, mki ? 2 : 1);
					wl.events.push_back({.block = 60, .kind = Event::KEYUP, .engine = 0, .patch = {}});
					w.push_back(wl);
				}
			}
		}

		w.push_back(make(WorkloadId::AUTO_FB2, with_algorithm(base, 5, 7), 0));
		w.push_back(make(WorkloadId::AUTO_FB3, with_algorithm(base, 3, 7), 0));
		w.push_back(make(WorkloadId::MKI_ALG32_FB, with_algorithm(base, 31, 5), 2));
		w.push_back(make(WorkloadId::MODERN_ALG4_FB, with_algorithm(base, 3, 6), 1));
		w.push_back(make(WorkloadId::MODERN_ALG6_FB, with_algorithm(base, 5, 6), 1));

		{
			Workload wl = make(WorkloadId::ENGINE_SWITCH_MID_NOTE, with_algorithm(base, 3, 7), 1);
			wl.events.push_back({.block = 40, .kind = Event::ENGINE_SWITCH, .engine = 2, .patch = {}});
			wl.events.push_back({.block = 80, .kind = Event::ENGINE_SWITCH, .engine = 1, .patch = {}});
			w.push_back(wl);
		}
		{
			Workload wl = make(WorkloadId::PATCH_EDIT_MID_NOTE, with_algorithm(base, 0, 3), 1);
			Patch edited = with_algorithm(base, 17, 6);
			edited[0 * kOpBytes + kOutputLevel] = 70;
			edited[4 * kOpBytes + kOutputLevel] = 85;
			edited[2 * kOpBytes + kCoarse] = 9;
			wl.events.push_back({.block = 30, .kind = Event::PATCH_EDIT, .engine = 0, .patch = edited});
			w.push_back(wl);
		}
		for (int wave = 0; wave < 6; ++wave) {
			Patch p = with_algorithm(base, 4, 0);
			p[kLfoRate] = 35;
			p[kLfoPmd] = 50;
			p[kLfoAmd] = 99;
			p[kLfoPms] = 7;
			p[kLfoWave] = static_cast<uint8_t>(wave);
			p[kLfoDelay] = 0;
			for (std::size_t op = 0; op < 6; ++op) {
				p[op * kOpBytes + kAmpModSens] = 3;
			}
			w.push_back(make(static_cast<WorkloadId>(static_cast<int>(WorkloadId::LFO_WAVE_0) + wave), p, 1));
		}
		{
			Patch p = with_algorithm(base, 4, 0);
			p[kLfoRate] = 35;
			p[kLfoPmd] = 50;
			p[kLfoAmd] = 99;
			p[kLfoPms] = 5;
			p[kLfoWave] = 4;
			p[kLfoDelay] = 60;
			p[kLfoKeySync] = 1;
			for (std::size_t op = 0; op < 6; ++op) {
				p[op * kOpBytes + kAmpModSens] = 2;
			}
			w.push_back(make(WorkloadId::LFO_DELAY_KEY_SYNC, p, 1));
		}
		{
			Patch p = with_algorithm(base, 4, 0);
			p[kOscKeySync] = 0;
			w.push_back(make(WorkloadId::OSC_KEY_SYNC_OFF, p, 1));
		}
		{
			Workload wl = make(WorkloadId::RANDOM_DETUNE, with_algorithm(base, 4, 0), 1);
			wl.random_detune = 40;
			w.push_back(wl);
		}
		{
			Patch p = with_algorithm(base, 4, 0);
			p[0 * kOpBytes + kOscMode] = 1;
			p[3 * kOpBytes + kOscMode] = 1;
			w.push_back(make(WorkloadId::FIXED_FREQ, p, 1));
		}
		for (const bool mki : {false, true}) {
			// A release rate of 80, so the tail decays within the workload (the base patch's 30 takes longer).
			Patch p = with_algorithm(base, 0, 7);
			for (std::size_t op = 0; op < 6; ++op) {
				p[op * kOpBytes + 3] = 80;
			}
			Workload wl = make(mki ? WorkloadId::RELEASE_TAIL_MKI : WorkloadId::RELEASE_TAIL_MODERN, p, mki ? 2 : 1);
			wl.blocks = std::vector<uint16_t>(4010, 128);
			wl.events.push_back({.block = 10, .kind = Event::KEYUP, .engine = 0, .patch = {}});
			w.push_back(wl);
		}
		{
			Workload wl = make(WorkloadId::RETRIGGER_KEEPS_FEEDBACK, with_algorithm(base, 0, 7), 1);
			wl.events.push_back({.block = 50, .kind = Event::RETRIGGER, .engine = 0, .patch = {}});
			w.push_back(wl);
		}
		return w;
	}();
	return all;
}

struct WorkloadRecord {
	uint64_t outputs;
	uint64_t state;
	uint64_t alive;
	bool operator==(const WorkloadRecord&) const = default;
};

struct TableRecord {
	uint64_t exp2, sin, freq, sin_log, sin_exp;
	bool operator==(const TableRecord&) const = default;
};

/// @return Digests of each table of @p tables, by member name: the reference's (exp2tab, sintab, freq_lut,
/// sinLogTable, sinExpTable) or the component's (exp2, sine, frequency, mki_sin_log, mki_sin_exp).
template <class T>
TableRecord table_record(const T& tables) {
	const auto fold = [](const auto& table) {
		Digest d;
		for (const auto value : table) {
			d.add(value);
		}
		return d.value;
	};
	if constexpr (requires { tables.exp2tab; }) {
		return {fold(tables.exp2tab), fold(tables.sintab), fold(tables.freq_lut), fold(tables.sinLogTable),
		        fold(tables.sinExpTable)};
	}
	else {
		return {fold(tables.exp2), fold(tables.sine), fold(tables.frequency), fold(tables.mki_sin_log),
		        fold(tables.mki_sin_exp)};
	}
}

/// @return The tables, built once.
inline const dx7_reference::Tables& reference_tables() {
	static const dx7_reference::Tables& tables = [] -> const dx7_reference::Tables& {
		static dx7_reference::Tables t{};
		dx7_reference::init_tables(t);
		return t;
	}();
	return tables;
}

/// @brief What run() saw: the three digests, and the first block whose compute returned false.
struct Run {
	WorkloadRecord record;
	bool ended;
	std::size_t end_block;
};

/// @return @p w run through the reference voice, as the voice adapter drives it: per block, the events due, the
/// patch's LFO, then render's pitch, control and compute. @p Cores are the cores the voice renders through.
template <class Cores = dx7_reference::ReferenceCores>
Run run(const Workload& w) {
	const dx7_reference::Tables& tables = reference_tables();
	dx7_reference::Patch patch;
	std::copy(w.patch.begin(), w.patch.end(), patch.params);
	patch.random_detune = w.random_detune;
	patch.setEngineMode(w.engine);
	SeededStatic::reseed(w.seed);
	auto voice = std::make_unique<dx7_reference::Voice<SeededStatic, Cores>>();
	voice->init(tables, patch, w.midinote, w.velocity);

	Digest outputs;
	Digest state;
	Digest alive;
	Run result{.record = {}, .ended = false, .end_block = 0};
	bool patch_changed = false;
	for (std::size_t b = 0; b < w.blocks.size(); ++b) {
		for (const Event& e : w.events) {
			if (e.block != b) {
				continue;
			}
			switch (e.kind) {
			case Event::KEYUP:
				voice->keyup();
				break;
			case Event::PATCH_EDIT:
				std::copy(e.patch.begin(), e.patch.end(), patch.params);
				patch.updateEngineMode();
				patch_changed = true;
				break;
			case Event::ENGINE_SWITCH:
				patch.setEngineMode(e.engine);
				break;
			case Event::RETRIGGER:
				voice->init(tables, patch, w.midinote, w.velocity);
				break;
			}
		}
		const int n = w.blocks[b];
		patch.computeLfo(tables, n);

		// dx_voice_adapter.cpp's render
		alignas(16) std::array<int32_t, dx7_reference::DX_MAX_N> buf{};
		const int32_t adjpitch = dx7_reference::adapter_pitch(w.phase_increment);
		dx7_reference::VoiceCtrl ctrl{};
		ctrl.ampmod = w.amp_mod_param >> 13;
		if (patch_changed) {
			voice->update(patch, w.midinote);
			patch_changed = false;
		}
		const bool live = voice->compute(buf.data(), n, adjpitch, &patch, &ctrl);

		for (int i = 0; i < n; ++i) {
			outputs.add(buf[static_cast<std::size_t>(i)]);
		}
		voice->digest(state);
		patch.digest(state);
		alive.add(live);
		if (!live && !result.ended) {
			result.ended = true;
			result.end_block = b;
		}
	}
	result.record = {.outputs = outputs.value, .state = state.value, .alive = alive.value};
	return result;
}

/// @return @p w's record through the reference.
inline WorkloadRecord run_reference(const Workload& w) {
	return run(w).record;
}

/// @brief The modern operator's tables as the app builds them, for VectorCore.
inline const deluge::dsp::fixed::dx7::Tables& vector_tables() {
	static const deluge::dsp::fixed::dx7::Tables t = [] {
		deluge::dsp::fixed::dx7::Tables g{};
		deluge::dsp::fixed::dx7::generate(g);
		return g;
	}();
	return t;
}

/// @brief The reference's modern core with its no-input and bus-input ops rendered by the vector operator, the
/// device's arithmetic, and everything else (the gains, the gate, the feedback op, the routing) as Core renders it.
struct VectorCore {
	dx7_reference::AlignedBuf<int32_t, dx7_reference::DX_MAX_N> buf_[2]{}; // NOLINT(*-avoid-c-arrays): as Core

	DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC void render(const dx7_reference::Tables& t, int32_t* output, int n,
	                                                   dx7_reference::FmOpParams* params, int algorithm,
	                                                   int32_t* fb_buf, int32_t feedback_shift) {
		const dx7_reference::FmAlgorithm alg = dx7_reference::algorithms[algorithm];
		const int inv_n = (1 << 30) / n;
		bool has_contents[3] = {true, false, false}; // NOLINT(*-avoid-c-arrays): as Core
		for (int op = 0; op < 6; op++) {
			int flags = alg.ops[op];
			bool add = (flags & dx7_reference::OUT_BUS_ADD) != 0;
			dx7_reference::FmOpParams& param = params[op];
			int inbus = (flags >> 4) & 3;
			int outbus = flags & 3;
			int32_t* outptr = (outbus == 0) ? output : buf_[outbus - 1].get();
			int32_t gain1 = param.gain_out;
			int32_t gain2 = dx7_reference::exp2_lookup(t, param.level_in - (14 * (1 << 24)));
			param.gain_out = gain2;
			int32_t dgain = (int)((((int64_t)(gain2 - gain1 + (n >> 1))) * (int64_t)(inv_n)) >> 30);

			if (gain1 >= dx7_reference::kGainLevelThresh || gain2 >= dx7_reference::kGainLevelThresh) {
				if (!has_contents[outbus]) {
					add = false;
				}
				if (inbus == 0 || !has_contents[inbus]) {
					if ((flags & 0xc0) == 0xc0 && feedback_shift < 16) {
						dx7_reference::compute_fb(t, outptr, n, param.phase, param.freq, gain1, gain2, dgain, fb_buf,
						                          feedback_shift, add);
					}
					else {
						vector(outptr, n, outptr, param, gain1, deluge::dsp::fixed::dx7::Input::NONE, add);
					}
				}
				else {
					vector(outptr, n, buf_[inbus - 1].get(), param, gain1, deluge::dsp::fixed::dx7::Input::BUS, add);
				}
				has_contents[outbus] = true;
			}
			else if (!add) {
				has_contents[outbus] = false;
			}
			param.phase += param.freq * n;
		}
	}

	/// @brief One op through the vector operator from its held phase and gain; Core moves the held state itself.
	static void vector(int32_t* out, int n, const int32_t* modulation, const dx7_reference::FmOpParams& param,
	                   int32_t gain1, deluge::dsp::fixed::dx7::Input input, bool add) {
		namespace dx7 = deluge::dsp::fixed::dx7;
		deluge::dsp::Mono<dx7::ModernVectorOperator> op{{.input = input,
		                                                 .output = add ? dx7::Output::ADD : dx7::Output::SET,
		                                                 .level_in = param.level_in,
		                                                 .freq = param.freq,
		                                                 .tables = &vector_tables()}};
		op.on<&dx7::ModernVectorOperator::resume>(dx7::OperatorState{.phase = param.phase, .gain_out = gain1});
		const auto len = static_cast<std::size_t>(n);
		op.renderBlock({modulation, len}, {out, len}, {out, len});
	}
};

/// @brief The cores a voice renders through with the device's modern arithmetic.
struct VectorCores {
	VectorCore modern;
	dx7_reference::CoreMkI mki;
};

/// @return @p w's record through the reference voice rendering modern ops as the device does.
inline WorkloadRecord run_reference_vector(const Workload& w) {
	return run<VectorCores>(w).record;
}

/// @return Whether @p w renders under MkI from its first block to its last: no modern op ever renders, so its record
/// is the same through either modern core.
inline bool renders_mki_throughout(const Workload& w) {
	const auto resolves_mki = [](uint8_t engine, const Patch& patch) {
		return engine == 2 || (engine == 0 && patch[135] > 0 && (patch[134] == 3 || patch[134] == 5));
	};
	if (!resolves_mki(w.engine, w.patch)) {
		return false;
	}
	return std::ranges::none_of(
	    w.events, [](const Event& e) { return e.kind == Event::PATCH_EDIT || e.kind == Event::ENGINE_SWITCH; });
}

/// @return @p w's workload.
inline const Workload& workload(WorkloadId id) {
	return all_workloads().at(static_cast<std::size_t>(id));
}

struct Ending {
	bool ended;
	std::size_t block;
};

/// @return Whether @p id's voice ended, and at which block.
inline Ending ending(WorkloadId id) {
	const Run r = run(workload(id));
	return {.ended = r.ended, .block = r.end_block};
}

} // namespace dx7_workloads
