// dx7::Voice against the reference voice: every workload driven through a test-local adapter that mirrors
// dx_voice_adapter.cpp (the patch's LFO, log2f pitch, retune on a patch change, the zeroed block) renders the same
// samples and lives as long as the reference voice with the device's modern arithmetic (run_reference_vector), which
// equals the recorded reference wherever the voice stays under MkI; and it draws its noise as many times.

#include "dsp-next/core/control.h"
#include "dsp-next/fixed/dx7/graph.h"
#include "dsp-next/fixed/dx7/lfo.h"
#include "dsp-next/fixed/dx7/patch.h"
#include "dsp-next/fixed/dx7/tables.h"
#include "dsp-next/fixed/dx7/voice.h"
#include "dx7_workload_records.h"
#include "reference_dx7.h"
#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>

namespace dx7 = deluge::dsp::fixed::dx7;
namespace dw = dx7_workloads;
using Voice = dx7::Voice<dw::SeededStream>;

namespace {

/// @brief What a run saw: the outputs and the alive flags, digested as dw::run digests them, and the noise drawn.
struct Run {
	uint64_t outputs;
	uint64_t alive;
	std::size_t draws;
};

const dx7::Tables& tables() {
	static const dx7::Tables t = [] {
		dx7::Tables g{};
		dx7::generate(g);
		return g;
	}();
	return t;
}

/// @brief DxPatch's engine choice: MkI when forced, or in auto for algorithms 4 and 6 with feedback.
dx7::Engine engine_of(uint8_t mode, const dw::Patch& patch) {
	const bool mki = mode == 2 || (mode == 0 && patch[135] > 0 && (patch[134] == 3 || patch[134] == 5));
	return mki ? dx7::Engine::MKI : dx7::Engine::MODERN;
}

/// @return @p w through dx7::Voice, as the voice adapter drives it: per block, the events due, the patch's LFO, then
/// the voice's block.
Run run_voice(const dw::Workload& w) {
	static dx7::Buses buses;
	dw::Patch patch = w.patch;
	uint8_t mode = w.engine;
	dx7::Engine engine = engine_of(mode, patch);
	deluge::dsp::Control<dx7::Lfo> lfo;
	dx7::PatchBytes snapshot{};
	const auto settings = [&] {
		snapshot = patch;
		dx7::sanitise(snapshot);
		return Voice::Settings{.patch = &snapshot,
		                       .midinote = w.midinote,
		                       .velocity = w.velocity,
		                       .random_detune = w.random_detune,
		                       .engine = engine,
		                       .tables = &tables()};
	};
	const auto note_on = [&](Voice& v) {
		dw::SeededStream stream;
		v.start(settings(), stream);
		if (Voice::key_sync_lfo(settings())) {
			lfo.on<&dx7::Lfo::key_sync>();
		}
	};

	dw::SeededStatic::reseed(w.seed);
	auto voice = std::make_unique<Voice>();
	note_on(*voice);

	dx7_reference::Digest outputs;
	dx7_reference::Digest alive;
	bool patch_changed = false;
	for (std::size_t b = 0; b < w.blocks.size(); ++b) {
		for (const dw::Event& e : w.events) {
			if (e.block != b) {
				continue;
			}
			switch (e.kind) {
			case dw::Event::KEYUP:
				voice->release(settings());
				break;
			case dw::Event::PATCH_EDIT:
				patch = e.patch;
				engine = engine_of(mode, patch);
				patch_changed = true;
				break;
			case dw::Event::ENGINE_SWITCH:
				mode = e.engine;
				engine = engine_of(mode, patch);
				break;
			case dw::Event::RETRIGGER:
				note_on(*voice);
				break;
			}
		}
		const std::size_t n = w.blocks[b];
		lfo.configure({patch[137], patch[142], &tables()});
		const int32_t lfo_value = lfo.advance(n);

		// dx_voice_adapter.cpp's render
		alignas(16) std::array<int32_t, dx7::kMaxBlock> buf{};
		const Voice::BlockInputs in{.base_pitch = dx7_reference::adapter_pitch(w.phase_increment),
		                            .lfo_value = lfo_value,
		                            .eg_mod = 127,
		                            .amp_mod = w.amp_mod_param >> 13};
		if (patch_changed) {
			voice->retune(settings());
			patch_changed = false;
		}
		const bool live = voice->render_block(settings(), {buf.data(), n}, in, buses);
		for (std::size_t i = 0; i < n; ++i) {
			outputs.add(buf[i]);
		}
		alive.add(live);
	}
	return {.outputs = outputs.value, .alive = alive.value, .draws = dw::SeededStatic::draws()};
}

} // namespace

// clang-format off
describe dx7_voice_spec("dx7::Voice", ${
	for (const dw::Workload& workload : dw::all_workloads()) {
		it(dw::name(workload.id), _ {
			const Run run = run_voice(workload);

			dw::SeededStatic::reseed(workload.seed);
			const dw::WorkloadRecord expected = dw::run<dw::VectorCores>(workload).record;
			const std::size_t reference_draws = dw::SeededStatic::draws();

			expect(run.outputs).to_equal(expected.outputs);
			expect(run.alive).to_equal(expected.alive);
			expect(run.draws).to_equal(reference_draws);
			if (dw::renders_mki_throughout(workload)) {
				const dw::WorkloadRecord& recorded = dw::kRecordedReference.at(static_cast<std::size_t>(workload.id));
				expect(run.outputs).to_equal(recorded.outputs);
				expect(run.alive).to_equal(recorded.alive);
			}
		});
	}

	it("leaves every workload's patch as it is when it sanitises it", _ {
		for (const dw::Workload& w : dw::all_workloads()) {
			dx7::PatchBytes p = w.patch;
			dx7::sanitise(p);
			expect(p == w.patch).to_be_true();
		}
	});

	it("renders a patch with every guarded byte at 255 as the same patch at the DX7's limits", _ {
		dw::Workload wild = dw::all_workloads().front();
		for (std::size_t op = 0; op < 6; ++op) {
			wild.patch[(op * 21) + 17] = 1; // fixed frequency
			wild.patch[(op * 21) + 18] = 3; // the top decade, where an unclamped fine overflows
		}
		dw::Workload limits = wild;
		for (std::size_t op = 0; op < 6; ++op) {
			wild.patch[(op * 21) + 19] = 255;
			limits.patch[(op * 21) + 19] = 99;
		}
		for (std::size_t i = 126; i < 134; ++i) {
			wild.patch[i] = 255;
			limits.patch[i] = 99;
		}
		wild.patch[134] = 255;
		limits.patch[134] = 31;
		wild.patch[135] = 255;
		limits.patch[135] = 7;
		wild.patch[138] = 255;
		limits.patch[138] = 99;
		const Run a = run_voice(wild);
		const Run b = run_voice(limits);
		expect(a.outputs).to_equal(b.outputs);
		expect(a.alive).to_equal(b.alive);
	});

	it("takes a mid-note edit of every envelope's and the pitch envelope's rates and levels as the reference does", _ {
		std::size_t edited = 0;
		for (dw::Workload w : dw::all_workloads()) {
			if (w.id != dw::WorkloadId::PATCH_EDIT_MID_NOTE) {
				continue;
			}
			for (dw::Event& e : w.events) {
				if (e.kind != dw::Event::PATCH_EDIT) {
					continue;
				}
				for (std::size_t op = 0; op < 6; ++op) {
					for (std::size_t i = 0; i < 4; ++i) {
						e.patch[(op * 21) + i] = static_cast<uint8_t>(20 + (op * 9) + (i * 7));     // rates
						e.patch[(op * 21) + 4 + i] = static_cast<uint8_t>(99 - (op * 5) - (i * 11)); // levels
					}
				}
				for (std::size_t i = 0; i < 8; ++i) {
					e.patch[126 + i] = static_cast<uint8_t>(30 + (i * 9)); // the pitch envelope's rates and levels
				}
				e.block = 1; // during the attack, so the stages entered after it read the edit
				++edited;
			}
			// and a release, which reads them again
			w.events.push_back({.block = 60, .kind = dw::Event::KEYUP, .engine = 0, .patch = {}});
			const Run run = run_voice(w);
			dw::SeededStatic::reseed(w.seed);
			const dw::WorkloadRecord expected = dw::run<dw::VectorCores>(w).record;
			expect(run.outputs).to_equal(expected.outputs);
			expect(run.alive).to_equal(expected.alive);
		}
		expect(edited).to_equal(std::size_t{1});
	});

	it("adds the amplitude-modulation parameter as the reference does, at every sensitivity", _ {
		std::size_t modulated = 0;
		for (dw::Workload w : dw::all_workloads()) {
			w.amp_mod_param = 0x2468ace0; // ampmod = 0x2468ace0 >> 13, through each op's sensitivity
			for (std::size_t op = 0; op < 6; ++op) {
				w.patch[(op * 21) + 14] = static_cast<uint8_t>(op % 4); // AMS 0 to 3
			}
			const Run run = run_voice(w);
			dw::SeededStatic::reseed(w.seed);
			const dw::WorkloadRecord expected = dw::run<dw::VectorCores>(w).record;
			expect(run.outputs).to_equal(expected.outputs);
			expect(run.alive).to_equal(expected.alive);
			++modulated;
		}
		expect(modulated).to_equal(dw::all_workloads().size());
	});
});
// clang-format on

CPPSPEC_MAIN(dx7_voice_spec);
