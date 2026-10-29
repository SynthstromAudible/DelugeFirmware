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
/// @brief The reverb's workloads: a model and its knobs set as a song load sets them, then blocks of a length, a send,
/// pan levels and, between blocks, a model change or new knobs; and what a reverb renders from them.
///
/// A run is generic over the reverb: the reference (reference_reverb.h) or a kernel's runner, which take the same
/// calls (ReverbRunner). A block of the same index gets the same send and dry frames in every run.

#pragma once

#include "dsp-next/core/stereo_sample.h"
#include "reverb_views.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <ostream>
#include <span>
#include <vector>

namespace reverb_workloads {

using deluge::dsp::StereoSample;
using q31 = int32_t;

/// @brief A model as a song file saves it.
enum class Model : uint8_t { FREEVERB, MUTABLE, DIGITAL };

/// @brief The knobs as Song::project.reverb holds them, each a fraction 0 to 1.
struct Knobs {
	float room_size, damping, width, hpf, lpf;
};

/// @brief The boot song's knobs (Song::Song).
inline constexpr Knobs kBootKnobs{30.f / 50, 36.f / 50, 1.f, 0.f, 50.f / 50};
/// @brief Knobs away from them, width at half (Freeverb's cross-mix, wet2 > 0).
inline constexpr Knobs kTurned{0.9f, 0.2f, 0.5f, 0.6f, 0.4f};
/// @brief Every knob at an end: room and damping at 1, width at its lowest step above 0 (1/50), high-pass 1,
/// low-pass 0. Width 0 itself is left out: Freeverb's wet2 there is 2^31, out of int32's range, which the targets
/// convert differently; the kernel's coefficient spec covers it on each target.
inline constexpr Knobs kExtremes{1.f, 1.f, 1.f / 50, 1.f, 0.f};
/// @brief Full-scale input through a plate that passes it: room at 1, no damping, both filters open (high-pass 0,
/// low-pass 1). At kExtremes the plates' low-pass closes and they return nothing, so these knobs are what drive a
/// plate's output to full scale.
inline constexpr Knobs kPlateOpen{1.f, 0.f, 1.f, 0.f, 1.f};
/// @brief Two knob sets between those, for the knob steps: every knob at half, and one away from it each way.
inline constexpr Knobs kMidpointA{0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
inline constexpr Knobs kMidpointB{0.3f, 0.7f, 0.8f, 0.1f, 0.9f};

/// @brief The send's signal: a sum of tones, LCG noise, full-scale runs of INT32_MAX then INT32_MIN (512 frames
/// each), a constant offset (the DC blocker's input), or silence.
enum class Send : uint8_t { TONE, NOISE, FULL_SCALE, DC, SILENCE };

/// @brief The return levels renderReverb computes with no duck: (0x20000000 >> 15) * (0x20000000 >> 14).
inline constexpr q31 kCentred = 1 << 29;

/// @brief The DC send's constant offset.
inline constexpr q31 kDcOffset = 0x30000000;

/// @brief One block: its length, its send, its pan levels, and a model change or knobs applied before it.
struct Step {
	uint16_t frames;
	Send send;
	q31 pan_l = kCentred;
	q31 pan_r = kCentred;
	std::optional<Model> model{};
	std::optional<Knobs> knobs{};
};

/// @brief Every workload, in all_workloads() order.
enum class WorkloadId : uint8_t {
	FREEVERB_BOOT,
	FREEVERB_TURNED,
	FREEVERB_BLOCKS_1_TO_9,
	FREEVERB_FULL_SCALE,
	FREEVERB_DC,
	FREEVERB_KNOB_STEPS,
	MUTABLE_BOOT,
	MUTABLE_TURNED,
	MUTABLE_BLOCKS_1_TO_9,
	MUTABLE_FULL_SCALE,
	MUTABLE_LONG,
	MUTABLE_KNOB_STEPS,
	DIGITAL_BOOT,
	DIGITAL_TURNED,
	DIGITAL_BLOCKS_1_TO_9,
	DIGITAL_FULL_SCALE,
	DIGITAL_LONG,
	DIGITAL_KNOB_STEPS,
	MODEL_CHANGES,
	MUTABLE_FULL_SCALE_OPEN,
	DIGITAL_FULL_SCALE_OPEN,
	kCount,
};
inline constexpr std::size_t kWorkloadCount = static_cast<std::size_t>(WorkloadId::kCount);

/// @brief A workload: a model held throughout (its first step sets it), or none for MODEL_CHANGES.
struct Workload {
	WorkloadId id;
	const char* name;
	std::optional<Model> model;
	std::size_t blocks;
	std::function<Step(std::size_t)> step;
};

/// @return A triangle at @p phase, ±2^30.
[[nodiscard]] inline q31 triangle(uint32_t phase) {
	const uint32_t folded = (phase & 0x80000000U) != 0 ? ~phase : phase;
	return static_cast<q31>(folded) - 0x40000000;
}

/// @return Full scale for frame @p frame of a full-scale run: INT32_MAX for 512 frames, then INT32_MIN for 512.
[[nodiscard]] inline q31 full_scale(std::size_t frame) {
	return (frame / 512) % 2 == 0 ? INT32_MAX : INT32_MIN;
}

/// @return @p n words of block @p block's send, whose first frame is the workload's @p first_frame: the tones run on
/// from frame to frame across blocks, the noise is seeded per block.
[[nodiscard]] inline std::vector<q31> send(std::size_t n, Send kind, std::size_t block, std::size_t first_frame) {
	std::vector<q31> v(n);
	switch (kind) {
	case Send::TONE:
		// Two triangles near 220 Hz and 1 kHz, each ±2^28.
		for (std::size_t i = 0; i < n; ++i) {
			const auto frame = static_cast<uint32_t>(first_frame + i);
			v[i] = (triangle(frame * 21'426'900U) >> 2) + (triangle(frame * 99'334'000U) >> 2);
		}
		break;
	case Send::NOISE: {
		uint32_t x = (0xc2b2ae35U * static_cast<uint32_t>(block + 1)) + 7U;
		for (q31& word : v) {
			x = (x * 1664525U) + 1013904223U;
			word = static_cast<q31>(x) >> 2;
		}
		break;
	}
	case Send::FULL_SCALE:
		for (std::size_t i = 0; i < n; ++i) {
			v[i] = full_scale(first_frame + i);
		}
		break;
	case Send::DC:
		std::ranges::fill(v, kDcOffset);
		break;
	case Send::SILENCE:
		break;
	}
	return v;
}

/// @return @p n dry frames of block @p block, whose first frame is the workload's @p first_frame: LCG noise at ±2^29
/// per channel, seeded per block; or, beside a full-scale send, full scale on both channels, so the reverb's add to
/// the output wraps.
[[nodiscard]] inline std::vector<StereoSample> dry(std::size_t n, Send kind, std::size_t block,
                                                   std::size_t first_frame) {
	std::vector<StereoSample> v(n);
	if (kind == Send::FULL_SCALE) {
		for (std::size_t i = 0; i < n; ++i) {
			const q31 extreme = full_scale(first_frame + i);
			v[i] = {extreme, extreme};
		}
		return v;
	}
	uint32_t l = 0x9e3779b9U * static_cast<uint32_t>(block + 1);
	uint32_t r = (0x85ebca6bU * static_cast<uint32_t>(block + 1)) + 1;
	for (StereoSample& frame : v) {
		l = (l * 1664525U) + 1013904223U;
		r = (r * 22695477U) + 1U;
		frame = {static_cast<q31>(l) >> 2, static_cast<q31>(r) >> 2};
	}
	return v;
}

/// @brief Every workload.
[[nodiscard]] inline const std::vector<Workload>& all_workloads() {
	static const std::vector<Workload> all = [] {
		std::vector<Workload> w;
		// @p blocks blocks of 128 frames of @p kind, the model and @p knobs set before the first.
		const auto steady = [](Model m, Knobs knobs, Send kind) {
			return [m, knobs, kind](std::size_t b) {
				Step s{.frames = 128, .send = kind};
				if (b == 0) {
					s.model = m;
					s.knobs = knobs;
				}
				return s;
			};
		};
		// Noise at kTurned, the pan alternating every 8 blocks: centred, unequal, then left silent.
		const auto turned = [](Model m) {
			return [m](std::size_t b) {
				Step s{.frames = 128, .send = Send::NOISE};
				switch ((b / 8) % 3) {
				case 1:
					s.pan_l = 0x15000000;
					s.pan_r = 0x2A000000;
					break;
				case 2:
					s.pan_l = 0;
					s.pan_r = kCentred;
					break;
				default:
					break;
				}
				if (b == 0) {
					s.model = m;
					s.knobs = kTurned;
				}
				return s;
			};
		};
		// Blocks of 1 to 9 frames of tone at kTurned: past 32 frames of LFO stride many times.
		const auto blocks_1_to_9 = [](Model m) {
			return [m](std::size_t b) {
				Step s{.frames = static_cast<uint16_t>((b % 9) + 1), .send = Send::TONE};
				if (b == 0) {
					s.model = m;
					s.knobs = kTurned;
				}
				return s;
			};
		};
		// Tone, the knobs stepping every 10 blocks through five sets, so knob changes land between blocks.
		const auto knob_steps = [](Model m) {
			return [m](std::size_t b) {
				constexpr std::array<Knobs, 5> sets{kBootKnobs, kTurned, kExtremes, kMidpointA, kMidpointB};
				Step s{.frames = 128, .send = Send::TONE};
				if (b % 10 == 0) {
					s.knobs = sets[(b / 10) % sets.size()];
				}
				if (b == 0) {
					s.model = m;
				}
				return s;
			};
		};

		const auto add_model = [&](Model m, WorkloadId boot, const char* boot_name, WorkloadId turned_id,
		                           const char* turned_name, WorkloadId blocks_id, const char* blocks_name,
		                           WorkloadId full_id, const char* full_name) {
			w.push_back({boot, boot_name, m, 60, steady(m, kBootKnobs, Send::TONE)});
			w.push_back({turned_id, turned_name, m, 60, turned(m)});
			w.push_back({blocks_id, blocks_name, m, 2400, blocks_1_to_9(m)});
			w.push_back({full_id, full_name, m, 80, steady(m, kExtremes, Send::FULL_SCALE)});
		};

		add_model(Model::FREEVERB, WorkloadId::FREEVERB_BOOT, "FREEVERB_BOOT", WorkloadId::FREEVERB_TURNED,
		          "FREEVERB_TURNED", WorkloadId::FREEVERB_BLOCKS_1_TO_9, "FREEVERB_BLOCKS_1_TO_9",
		          WorkloadId::FREEVERB_FULL_SCALE, "FREEVERB_FULL_SCALE");
		// A constant offset at the boot knobs: the DC blocker's memory settles toward it.
		w.push_back({WorkloadId::FREEVERB_DC, "FREEVERB_DC", Model::FREEVERB, 400,
		             steady(Model::FREEVERB, kBootKnobs, Send::DC)});
		w.push_back({WorkloadId::FREEVERB_KNOB_STEPS, "FREEVERB_KNOB_STEPS", Model::FREEVERB, 160,
		             knob_steps(Model::FREEVERB)});

		// The plates' LONG workloads run past one 32768-float ring lap.
		add_model(Model::MUTABLE, WorkloadId::MUTABLE_BOOT, "MUTABLE_BOOT", WorkloadId::MUTABLE_TURNED,
		          "MUTABLE_TURNED", WorkloadId::MUTABLE_BLOCKS_1_TO_9, "MUTABLE_BLOCKS_1_TO_9",
		          WorkloadId::MUTABLE_FULL_SCALE, "MUTABLE_FULL_SCALE");
		w.push_back({WorkloadId::MUTABLE_LONG, "MUTABLE_LONG", Model::MUTABLE, 420, turned(Model::MUTABLE)});
		w.push_back(
		    {WorkloadId::MUTABLE_KNOB_STEPS, "MUTABLE_KNOB_STEPS", Model::MUTABLE, 160, knob_steps(Model::MUTABLE)});

		add_model(Model::DIGITAL, WorkloadId::DIGITAL_BOOT, "DIGITAL_BOOT", WorkloadId::DIGITAL_TURNED,
		          "DIGITAL_TURNED", WorkloadId::DIGITAL_BLOCKS_1_TO_9, "DIGITAL_BLOCKS_1_TO_9",
		          WorkloadId::DIGITAL_FULL_SCALE, "DIGITAL_FULL_SCALE");
		w.push_back({WorkloadId::DIGITAL_LONG, "DIGITAL_LONG", Model::DIGITAL, 420, turned(Model::DIGITAL)});
		w.push_back(
		    {WorkloadId::DIGITAL_KNOB_STEPS, "DIGITAL_KNOB_STEPS", Model::DIGITAL, 160, knob_steps(Model::DIGITAL)});

		// Every model in turn: Mutable with no knobs (the cached knobs carry across), Freeverb re-entered, Digital
		// re-entered and then set again (the same model, which still rebuilds), and knobs alone at the end.
		w.push_back({WorkloadId::MODEL_CHANGES, "MODEL_CHANGES", std::nullopt, 200, [](std::size_t b) {
			             Step s{.frames = 128, .send = Send::TONE};
			             switch (b) {
			             case 0:
				             s.model = Model::FREEVERB;
				             s.knobs = kTurned;
				             break;
			             case 40:
				             s.model = Model::MUTABLE;
				             break;
			             case 80:
				             s.model = Model::DIGITAL;
				             s.knobs = kBootKnobs;
				             break;
			             case 120:
				             s.model = Model::FREEVERB;
				             break;
			             case 150:
			             case 170:
				             s.model = Model::DIGITAL;
				             break;
			             case 185:
				             s.knobs = kExtremes;
				             break;
			             default:
				             break;
			             }
			             return s;
		             }});
		w.push_back({WorkloadId::MUTABLE_FULL_SCALE_OPEN, "MUTABLE_FULL_SCALE_OPEN", Model::MUTABLE, 80,
		             steady(Model::MUTABLE, kPlateOpen, Send::FULL_SCALE)});
		w.push_back({WorkloadId::DIGITAL_FULL_SCALE_OPEN, "DIGITAL_FULL_SCALE_OPEN", Model::DIGITAL, 80,
		             steady(Model::DIGITAL, kPlateOpen, Send::FULL_SCALE)});
		return w;
	}();
	return all;
}

/// @brief A 64-bit FNV-1a digest of 32-bit words; floats by their bits.
struct Digest {
	uint64_t value = 14695981039346656037ULL;

	void add(uint32_t w) { value = (value ^ w) * 1099511628211ULL; }
	void add(int32_t w) { add(static_cast<uint32_t>(w)); }
	void add(float f) { add(std::bit_cast<uint32_t>(f)); }
};

/// @brief What a workload rendered: every block's output frames, the model's whole state at the end (in the view's
/// canonical order), and every block's send buffer after the render (the write-back, where a model makes one).
struct WorkloadRecord {
	uint64_t outputs = 0;
	uint64_t state = 0;
	uint64_t send = 0;

	bool operator==(const WorkloadRecord&) const = default;
};

inline std::ostream& operator<<(std::ostream& out, const WorkloadRecord& r) {
	return out << std::hex << "{.outputs = 0x" << r.outputs << "ULL, .state = 0x" << r.state << "ULL, .send = 0x"
	           << r.send << "ULL}" << std::dec;
}

/// @brief A reverb a workload can drive: a model change, the knobs, one block in place, and its state's digest.
template <class R>
concept ReverbRunner = requires(R r, const R cr, Model m, const Knobs& k, std::span<q31> send,
                                std::span<StereoSample> out, q31 pan, Digest& d) {
	r.set_model(m);
	r.set_knobs(k);
	r.render(send, out, pan, pan);
	cr.digest_state(d);
};

/// @brief Folds a Freeverb's state into @p d: its lines, indices, stores and DC memory.
inline void digest_freeverb(Digest& d, const deluge::dsp::test::FreeverbView& v) {
	for (const std::span<const int32_t> line : v.lines) {
		for (const int32_t word : line) {
			d.add(word);
		}
	}
	for (const int32_t index : v.index) {
		d.add(index);
	}
	for (const int32_t store : v.store) {
		d.add(store);
	}
	d.add(v.dc);
}

/// @brief Folds a plate's state into @p d: its ring, write index, LFO lanes and filter memories.
inline void digest_plate(Digest& d, const deluge::dsp::test::PlateView& v) {
	for (const float f : v.ring) {
		d.add(f);
	}
	d.add(v.write);
	for (const float f : v.lfo_y_0) {
		d.add(f);
	}
	for (const float f : v.lfo_y_1) {
		d.add(f);
	}
	d.add(v.lp_decay_1);
	d.add(v.lp_decay_2);
	d.add(v.lp_band);
	d.add(v.hp_l);
	d.add(v.hp_r);
	d.add(v.lp_l);
	d.add(v.lp_r);
}

/// @brief Plays @p w through @p runner, calling @p each with every block's index, send and output.
///
/// Each block's send and dry frames are built, the step's model change and then its knobs are applied, and the block
/// is rendered in place.
template <ReverbRunner R, class Each>
void play(const Workload& w, R& runner, Each&& each) {
	std::size_t frame = 0;
	for (std::size_t b = 0; b < w.blocks; ++b) {
		const Step step = w.step(b);
		std::vector<q31> send_block = send(step.frames, step.send, b, frame);
		std::vector<StereoSample> out = dry(step.frames, step.send, b, frame);
		frame += step.frames;
		if (step.model) {
			runner.set_model(*step.model);
		}
		if (step.knobs) {
			runner.set_knobs(*step.knobs);
		}
		runner.render(std::span<q31>{send_block}, std::span<StereoSample>{out}, step.pan_l, step.pan_r);
		each(b, std::span<const q31>{send_block}, std::span<const StereoSample>{out});
	}
}

/// @brief Folds one block's send into @p d; an empty send counts as @p frames words of 0.
inline void add_send(Digest& d, std::span<const q31> block, std::size_t frames) {
	if (block.empty()) {
		for (std::size_t i = 0; i < frames; ++i) {
			d.add(q31{0});
		}
		return;
	}
	for (const q31 word : block) {
		d.add(word);
	}
}

/// @return @p w's record, rendered by a default-constructed @p R.
template <ReverbRunner R>
[[nodiscard]] WorkloadRecord record(const Workload& w) {
	R runner{};
	Digest outputs;
	Digest sends;
	play(w, runner, [&](std::size_t, std::span<const q31> send_block, std::span<const StereoSample> out) {
		for (const StereoSample s : out) {
			outputs.add(s.l);
			outputs.add(s.r);
		}
		add_send(sends, send_block, out.size());
	});
	Digest state;
	runner.digest_state(state);
	return {.outputs = outputs.value, .state = state.value, .send = sends.value};
}

/// @return The send digest @ref record gives for @p w from a runner that leaves the send untouched: the send blocks
/// play builds, digested as record digests them, with nothing rendered.
[[nodiscard]] inline uint64_t record_send_untouched(const Workload& w) {
	Digest sends;
	std::size_t frame = 0;
	for (std::size_t b = 0; b < w.blocks; ++b) {
		const Step step = w.step(b);
		const std::vector<q31> send_block = send(step.frames, step.send, b, frame);
		frame += step.frames;
		add_send(sends, send_block, step.frames);
	}
	return sends.value;
}

} // namespace reverb_workloads
