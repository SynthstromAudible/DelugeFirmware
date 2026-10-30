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

// The reverb component (effects/reverb.h) against the reverb reference: every workload, the model hand-over,
// settings publication, and which model renders.
#include "effects/reverb.h"

#include "dsp-next/core/stereo_sample.h"
#include "reference_reverb.h"
#include "reverb_kernel_views.h"
#include "reverb_workload_records.h"
#include "reverb_workloads.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace {
namespace rw = reverb_workloads;
using deluge::dsp::StereoSample;
using deluge::dsp::test::ReferenceRunner;
using deluge::effects::Reverb;
using deluge::effects::ReverbModel;

/// @return @p k as the component's settings.
Reverb::Settings settings_of(const rw::Knobs& k) {
	return {.room_size = k.room_size, .damping = k.damping, .width = k.width, .hpf = k.hpf, .lpf = k.lpf};
}

/// @brief The component behind the runner shape, in the call order the engine uses: a model change is begin_change,
/// prepare, commit; knobs are published. It lives on the heap: the buffer is 128 KB.
struct ComponentRunner {
	std::unique_ptr<Reverb> reverb = std::make_unique<Reverb>();

	void set_model(rw::Model m) {
		reverb->begin_change();
		reverb->prepare(static_cast<ReverbModel>(m));
		reverb->commit();
	}
	void set_knobs(const rw::Knobs& k) { reverb->publish(settings_of(k)); }
	void render(std::span<int32_t> send, std::span<StereoSample> out, int32_t l, int32_t r) {
		reverb->render(out, send, l, r);
	}
	void digest_state(rw::Digest& d) const {
		if (reverb->model() == ReverbModel::FREEVERB) {
			rw::digest_freeverb(d,
			                    deluge::dsp::test::freeverb_view(reverb->freeverb_state(), reverb->freeverb_memory()));
		}
		else {
			rw::digest_plate(d, deluge::dsp::test::plate_view(reverb->plate_state(), reverb->plate_memory()));
		}
	}
};
static_assert(rw::ReverbRunner<ComponentRunner>);

/// @brief One block of 128 frames of tone, numbered from 0: its send and the dry frames it is rendered over.
struct Block {
	std::vector<int32_t> send;
	std::vector<StereoSample> out;
	explicit Block(std::size_t b)
	    : send(rw::send(128, rw::Send::TONE, b, b * 128)), out(rw::dry(128, rw::Send::TONE, b, b * 128)) {}
};

/// @return Whether two blocks hold the same frames.
bool same(std::span<const StereoSample> a, std::span<const StereoSample> b) {
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](StereoSample x, StereoSample y) {
		       return x.l == y.l && x.r == y.r;
	       });
}

/// @return Block @p b rendered by @p runner at centred levels.
template <class R>
std::vector<StereoSample> render_block(R& runner, std::size_t b) {
	Block block(b);
	runner.render(block.send, block.out, rw::kCentred, rw::kCentred);
	return block.out;
}

constexpr std::array kModels{rw::Model::FREEVERB, rw::Model::MUTABLE, rw::Model::DIGITAL};

} // namespace

// clang-format off
describe reverb("the reverb component", ${
	it("renders every workload as the reference recorded", _ {
		for (const rw::Workload& w : rw::all_workloads()) {
			const rw::WorkloadRecord got = rw::record<ComponentRunner>(w);
			const rw::WorkloadRecord& want = rw::kRecordedReference.at(static_cast<std::size_t>(w.id));
			expect(got.outputs).to_equal(want.outputs);
			expect(got.state).to_equal(want.state);
			expect(got.send).to_equal(rw::record_send_untouched(w)); // the component never writes the send back
		}
	});

	it("renders nothing while a model change is under way", _ {
		for (const rw::Model m : kModels) {
			ComponentRunner runner;
			runner.set_model(rw::Model::FREEVERB);
			runner.set_knobs(rw::kTurned);
			std::ignore = render_block(runner, 0);

			Block dry(1);
			runner.reverb->begin_change();
			std::vector<StereoSample> out = render_block(runner, 1);
			expect(same(out, dry.out)).to_be_true();
			runner.reverb->prepare(static_cast<ReverbModel>(m));
			out = render_block(runner, 1);
			expect(same(out, dry.out)).to_be_true();
			runner.reverb->commit();

			ReferenceRunner fresh;
			fresh.set_model(m);
			fresh.set_knobs(rw::kTurned);
			expect(same(render_block(runner, 0), render_block(fresh, 0))).to_be_true();
		}
	});

	it("zeroes its memory and resets the model at every change, the same model included", _ {
		for (const rw::Model m : kModels) {
			ComponentRunner runner;
			runner.set_model(m);
			runner.set_knobs(rw::kTurned);
			for (std::size_t b = 0; b < 100; ++b) {
				std::ignore = render_block(runner, b);
			}
			runner.set_model(m);

			ReferenceRunner fresh;
			fresh.set_model(m);
			fresh.set_knobs(rw::kTurned);
			expect(same(render_block(runner, 0), render_block(fresh, 0))).to_be_true();
		}
	});

	it("applies published settings from the next block", _ {
		// Past the lines' first wrap (the shortest is Freeverb's 1,116-frame comb), so the knobs reach the output.
		constexpr std::size_t kWarm = 40;
		for (const rw::Model m : kModels) {
			ComponentRunner runner;
			ReferenceRunner switched;
			ReferenceRunner kept;
			runner.set_model(m);
			switched.set_model(m);
			kept.set_model(m);
			runner.set_knobs(rw::kTurned);
			switched.set_knobs(rw::kTurned);
			kept.set_knobs(rw::kTurned);
			std::size_t mismatches = 0;
			for (std::size_t b = 0; b < kWarm; ++b) {
				mismatches += same(render_block(runner, b), render_block(switched, b)) ? 0 : 1;
				std::ignore = render_block(kept, b);
			}
			expect(mismatches).to_equal(std::size_t{0});

			runner.set_knobs(rw::kMidpointB);
			switched.set_knobs(rw::kMidpointB);
			const std::vector<StereoSample> got = render_block(runner, kWarm);
			expect(same(got, render_block(switched, kWarm))).to_be_true();
			expect(same(got, render_block(kept, kWarm))).to_be_false(); // B landed at this block, not later
		}
	});

	it("starts at known settings", _ {
		const auto reverb = std::make_unique<Reverb>();
		expect(reverb->model() == ReverbModel::FREEVERB).to_be_true();
		expect(Reverb::Settings{} == Reverb::Settings{.room_size = 0.5f, .damping = 0.5f, .width = 1.f, .hpf = 0.f, .lpf = 0.f}).to_be_true();

		// Nothing published: the model takes the component's default settings, as the reference takes the knobs it
		// caches at construction. Both leave the low-pass at 0, so a plate's block is the dry block in each; no knob
		// reaches a model from uninitialised memory.
		reverb->begin_change();
		reverb->prepare(ReverbModel::MUTABLE);
		reverb->commit();
		Block mine(0);
		reverb->render(mine.out, mine.send, rw::kCentred, rw::kCentred);

		auto reference = std::make_unique<deluge::dsp::test::reverb_ref::ReferenceReverb>();
		reference->setModel(deluge::dsp::test::reverb_ref::ReferenceReverb::Model::MUTABLE);
		reference->setPanLevels(rw::kCentred, rw::kCentred);
		Block theirs(0);
		reference->process(theirs.send, theirs.out);
		expect(same(mine.out, theirs.out)).to_be_true();
	});

	it("keeps the published settings across a model change", _ {
		ComponentRunner runner;
		ReferenceRunner reference;
		runner.set_model(rw::Model::MUTABLE);
		reference.set_model(rw::Model::MUTABLE);
		runner.set_knobs(rw::kTurned);
		reference.set_knobs(rw::kTurned);
		runner.set_model(rw::Model::DIGITAL);
		reference.set_model(rw::Model::DIGITAL);
		std::size_t mismatches = 0;
		for (std::size_t b = 0; b < 40; ++b) {
			mismatches += same(render_block(runner, b), render_block(reference, b)) ? 0 : 1;
		}
		expect(mismatches).to_equal(std::size_t{0});
	});

	it("renders through whichever model is selected", _ {
		// Each model, opened up (room 0.7, low-pass 1), equals the reference at that model block for block, and rings.
		constexpr rw::Knobs kOpen{.room_size = 0.7f, .damping = 0.5f, .width = 1.f, .hpf = 0.f, .lpf = 1.f};
		for (const rw::Model m : kModels) {
			ComponentRunner runner;
			ReferenceRunner reference;
			runner.set_model(m);
			reference.set_model(m);
			runner.set_knobs(kOpen);
			reference.set_knobs(kOpen);
			std::size_t mismatches = 0;
			for (std::size_t b = 0; b < 40; ++b) {
				mismatches += same(render_block(runner, b), render_block(reference, b)) ? 0 : 1;
			}
			expect(mismatches).to_equal(std::size_t{0});

			ComponentRunner ringing;
			ringing.set_model(m);
			ringing.set_knobs(kOpen);
			const double seconds = deluge::dsp::test::rt60(
			    [&ringing](std::span<int32_t> in, std::span<StereoSample> out) { ringing.render(in, out, 1 << 30, 1 << 30); },
			    5.0);
			expect(seconds).to_be_greater_than(0.05);
		}
	});

	it("commits the model the render asked for during a change, not the one being prepared", _ {
		// The interaction tier prepares Mutable; the render, in a song swap at a launch, asks for Digital meanwhile.
		ComponentRunner runner;
		runner.set_knobs(rw::kTurned);
		Reverb& reverb = *runner.reverb;
		reverb.begin_change();
		reverb.prepare(ReverbModel::MUTABLE);
		expect(reverb.changing()).to_be_true();
		reverb.request(ReverbModel::DIGITAL);
		const std::optional<ReverbModel> requested = reverb.take_request();
		expect(requested.has_value() && *requested == ReverbModel::DIGITAL).to_be_true();
		reverb.prepare(*requested);
		expect(reverb.take_request().has_value()).to_be_false(); // taken once
		reverb.commit();
		expect(reverb.model() == ReverbModel::DIGITAL).to_be_true();

		ReferenceRunner fresh;
		fresh.set_model(rw::Model::DIGITAL);
		fresh.set_knobs(rw::kTurned);
		std::size_t mismatches = 0;
		for (std::size_t b = 0; b < 40; ++b) {
			mismatches += same(render_block(runner, b), render_block(fresh, b)) ? 0 : 1;
		}
		expect(mismatches).to_equal(std::size_t{0});
	});

	it("forgets a request left over from an earlier change", _ {
		const auto reverb = std::make_unique<Reverb>();
		reverb->request(ReverbModel::DIGITAL);
		reverb->begin_change();
		expect(reverb->take_request().has_value()).to_be_false();
		reverb->prepare(ReverbModel::FREEVERB);
		reverb->commit();
	});

	it("reports the model last prepared", _ {
		const auto reverb = std::make_unique<Reverb>();
		for (const ReverbModel m : {ReverbModel::DIGITAL, ReverbModel::MUTABLE, ReverbModel::FREEVERB}) {
			reverb->begin_change();
			reverb->prepare(m);
			expect(reverb->model() == m).to_be_true();
			reverb->commit();
			expect(reverb->model() == m).to_be_true();
		}
	});

	it("ducks its return only at a non-zero depth, as a sidechain of the same rates does", _ {
		using deluge::dsp::fixed::DuckScale;
		using deluge::dsp::fixed::duck_gain;
		using deluge::effects::ReturnDuck;
		using deluge::effects::Sidechain;
		using deluge::effects::SidechainState;
		const SidechainState rates{.attack = 37640, .release = 3680};
		constexpr uint32_t kInverse = 6492772;
		constexpr int32_t kHit = 0x60000000;
		constexpr int32_t kShape = -601295438;
		auto reverb = std::make_unique<Reverb>();
		Sidechain alongside;

		// At depth 0 the return is unducked, and a hit neither registers nor advances the envelope.
		expect(reverb->return_ducks()).to_be_false();
		expect(reverb->return_level(128, kHit, rates, kInverse)).to_equal(1 << 29);

		reverb->set_return_duck(ReturnDuck{.depth = 0x20000000, .shape = kShape});
		expect(reverb->return_ducks()).to_be_true();
		for (int block = 0; block < 40; block++) {
			const int32_t hit = block == 2 || block == 25 ? kHit : 0;
			if (hit != 0) {
				alongside.registerHit(hit);
			}
			const int32_t duck = alongside.render(128, kShape, rates, kInverse);
			expect(reverb->return_level(128, hit, rates, kInverse))
			    .to_equal(duck_gain<DuckScale::REVERB_RETURN>(duck, 0x20000000));
		}
	});

	it("holds its return's duck still while the depth is 0, a hit then ignored, and resumes when it returns", _ {
		using deluge::dsp::fixed::DuckScale;
		using deluge::dsp::fixed::duck_gain;
		using deluge::effects::ReturnDuck;
		using deluge::effects::Sidechain;
		using deluge::effects::SidechainState;
		const SidechainState rates{.attack = 37640, .release = 3680};
		constexpr uint32_t kInverse = 6492772;
		constexpr int32_t kHit = 0x60000000;
		constexpr int32_t kShape = -601295438;
		auto reverb = std::make_unique<Reverb>();
		// The engine's rule, by hand: at a non-zero depth the hit registers and the duck advances; at 0, neither.
		Sidechain by_hand;
		for (int block = 0; block < 36; block++) {
			const int32_t depth = block < 10 ? 0x20000000 : (block < 20 ? 0 : 0x30000000);
			const int32_t hit = block == 2 || block == 12 || block == 25 ? kHit : 0;
			reverb->set_return_duck(ReturnDuck{.depth = depth, .shape = kShape});
			int32_t duck = 0;
			if (depth != 0) {
				if (hit != 0) {
					by_hand.registerHit(hit);
				}
				duck = by_hand.render(128, kShape, rates, kInverse);
			}
			expect(reverb->return_level(128, hit, rates, kInverse))
			    .to_equal(duck_gain<DuckScale::REVERB_RETURN>(duck, depth));
		}
	});
});
// clang-format on

CPPSPEC_SPEC(reverb);
