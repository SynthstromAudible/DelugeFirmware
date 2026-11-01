// The plate kernel (dsp-next/float/reverb/plate.h) against the reverb reference over every Mutable and Digital
// workload, and each loop's behaviour.
#include "dsp-next/core/mono.h"
#include "dsp-next/float/reverb/plate.h"
#include "reference_reverb.h"
#include "reverb_kernel_views.h"
#include "reverb_workload_records.h"
#include "reverb_workloads.h"
#include <cppspec.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ranges>
#include <span>
#include <vector>

namespace {
namespace rw = reverb_workloads;
using deluge::dsp::StereoSample;
using deluge::dsp::fp::Plate;
using deluge::dsp::fp::PlateLfo;

/// @brief Mono<Plate> over a ring it owns, as effects::Reverb holds it: a model change zeroes the ring, resets the
/// State and starts the LFO; the knobs carry across a model change.
struct PlateRunner {
	/// Model changes asked for that this runner cannot render, across every runner; the specs assert it stays 0.
	static inline std::size_t foreign_models = 0;
	std::vector<float> memory = std::vector<float>(Plate::kMemoryFloats);
	deluge::dsp::Mono<Plate> mono;
	Plate::Settings settings{};

	void set_model(rw::Model m) {
		if (m == rw::Model::FREEVERB) {
			++foreign_models;
			return;
		}
		std::ranges::fill(memory, 0.f);
		mono.reset();
		settings.mode = m == rw::Model::DIGITAL ? Plate::Mode::DIGITAL : Plate::Mode::MUTABLE;
		mono.configure(settings);
		mono.on<&Plate::start>();
		mono.on<&Plate::set_memory>(memory.data());
	}
	void set_knobs(const rw::Knobs& k) {
		settings.room_size = k.room_size;
		settings.damping = k.damping;
		settings.width = k.width;
		settings.hpf = k.hpf;
		settings.lpf = k.lpf;
		mono.configure(settings);
	}
	void render(std::span<int32_t> send, std::span<StereoSample> out, int32_t l, int32_t r) {
		mono.on<&Plate::set_send>(send.data(), l, r);
		mono.render_block(out, out);
	}
	void digest_state(rw::Digest& d) const { rw::digest_plate(d, deluge::dsp::test::plate_view(mono.state(), memory)); }
};

[[nodiscard]] bool is_mutable(const rw::Workload& w) {
	return w.model == rw::Model::MUTABLE;
}
[[nodiscard]] bool is_digital(const rw::Workload& w) {
	return w.model == rw::Model::DIGITAL;
}

constexpr std::size_t kBlock = deluge::dsp::test::kImpulseBlock;

/// @brief A plate with its knobs set once, both return levels at 2^30: the drive the behaviour cases measure through.
struct PlateModel {
	PlateRunner runner;
	PlateModel(rw::Model model, float room_size, float damping) {
		runner.set_model(model);
		runner.set_knobs({.room_size = room_size, .damping = damping, .width = 0.5f, .hpf = 0.f, .lpf = 1.f});
	}
	void process(std::span<int32_t> input, std::span<StereoSample> output) {
		runner.render(input, output, 1 << 30, 1 << 30);
	}
	/// @return A callable rendering one block through this model, for the shared impulse measurements.
	[[nodiscard]] auto render() {
		return [this](std::span<int32_t> input, std::span<StereoSample> output) { process(input, output); };
	}
};

/// The ring alone is 128 KB, so the models live on the heap.
std::unique_ptr<PlateModel> make_model(rw::Model model, float room_size, float damping) {
	return std::make_unique<PlateModel>(model, room_size, damping);
}

/// @return The settled tail's mean sample-to-sample change over its mean level: falls as damping darkens it.
double tail_brightness(PlateModel& model, double seconds) {
	std::vector<int32_t> input(kBlock, 0);
	std::vector<StereoSample> output(kBlock);
	input[0] = 1 << 28;
	const auto blocks = static_cast<std::size_t>(seconds * 44100.0 / kBlock);
	const std::size_t settled = blocks / 8; // the early reflections pass first
	double difference = 0.0;
	double level = 0.0;
	double previous = 0.0;
	for (std::size_t block = 0; block < blocks; ++block) {
		std::ranges::fill(output, StereoSample{.l = 0, .r = 0});
		model.process(input, output);
		std::ranges::fill(input, 0);
		for (const StereoSample& sample : output) {
			const auto current = static_cast<double>(sample.l);
			if (block >= settled) {
				difference += std::abs(current - previous);
				level += std::abs(current);
			}
			previous = current;
		}
	}
	return level > 0.0 ? difference / level : 0.0;
}

/// @return The same sine rendered through a fresh model at 2^@p input_shift, after the tank fills.
std::vector<int32_t> render_at(rw::Model m, int input_shift, double seconds) {
	auto model = make_model(m, 0.5f, 0.5f);
	std::vector<int32_t> input(kBlock);
	std::vector<StereoSample> output(kBlock);
	std::vector<int32_t> captured;
	const auto blocks = static_cast<std::size_t>(seconds * 44100.0 / kBlock);
	for (std::size_t block = 0; block < blocks; ++block) {
		for (std::size_t i = 0; i < kBlock; ++i) {
			const auto t = static_cast<double>((block * kBlock) + i);
			input[i] = static_cast<int32_t>(std::ldexp(1.0, input_shift) * std::sin(0.05 * t));
		}
		std::ranges::fill(output, StereoSample{.l = 0, .r = 0});
		model->process(input, output);
		if (block >= blocks / 4) {
			for (const StereoSample& sample : output) {
				captured.push_back(sample.l);
			}
		}
	}
	return captured;
}

/// @return The percentage of samples where an overdriven render disagrees in sign with a clean one. The loop is
/// linear up to the output conversion, so saturation keeps the sign and only an unchecked narrowing flips it.
double overdrive_sign_disagreement(rw::Model m) {
	const std::vector<int32_t> hot = render_at(m, 30, 0.6);
	const std::vector<int32_t> quiet = render_at(m, 18, 0.6);
	long long checked = 0;
	long long disagreed = 0;
	for (std::size_t i = 0; i < hot.size(); ++i) {
		if (std::abs(quiet[i]) < 10000) {
			continue; // too close to zero to have a meaningful sign
		}
		++checked;
		disagreed += ((hot[i] < 0) != (quiet[i] < 0)) ? 1 : 0;
	}
	return checked > 0 ? 100.0 * static_cast<double>(disagreed) / static_cast<double>(checked) : 0.0;
}

/// @return How many LFO lane values fall outside the plate's kLfoMin to kLfoMax over @p frames, the LFO stepped as the
/// plate steps it.
std::size_t lfo_lanes_out_of_range(std::size_t frames) {
	const PlateLfo::Coefficients c = PlateLfo::coefficients({});
	PlateLfo::State s{};
	PlateLfo::start(c, s);
	int32_t write = 0;
	std::size_t outside = 0;
	for (std::size_t frame = 0; frame < frames; ++frame) {
		if (--write < 0) {
			write += static_cast<int32_t>(Plate::kMemoryFloats);
		}
		if ((write & 31) == 0) {
			PlateLfo::advance(c, s, PlateLfo::kStride);
		}
		std::array<float, 2> lanes{};
		PlateLfo::value(s).StoreTo(lanes.data());
		for (const float lane : lanes) {
			outside += (lane < Plate::kLfoMin || lane > Plate::kLfoMax) ? 1 : 0;
		}
	}
	return outside;
}
} // namespace

// clang-format off
describe plate_kernel("the plate kernel", ${
	it("renders each Mutable workload's output and state as the reference recorded", _ {
		PlateRunner::foreign_models = 0;
		for (const rw::Workload& w : rw::all_workloads() | std::views::filter(is_mutable)) {
			const rw::WorkloadRecord got = rw::record<PlateRunner>(w);
			const rw::WorkloadRecord& want = rw::kRecordedReference.at(static_cast<std::size_t>(w.id));
			expect(got.outputs).to_equal(want.outputs);
			expect(got.state).to_equal(want.state);
			expect(got.send).to_equal(rw::record_send_untouched(w)); // the plates never write the send back
		}
		expect(PlateRunner::foreign_models).to_equal(std::size_t{0});
	});

	it("computes the reference's coefficients", _ {
		auto reference = std::make_unique<deluge::dsp::test::reverb_ref::ReferenceMutable>();
		for (int32_t k = 0; k <= 50; ++k) {
			const float knob = static_cast<float>(k) / 50.f;
			reference->setRoomSize(knob);
			reference->setDamping(knob);
			reference->setWidth(knob);
			reference->setHPF(knob);
			reference->setLPF(knob);
			const Plate::Coefficients c = Plate::coefficients(
			    {.mode = Plate::Mode::MUTABLE, .room_size = knob, .damping = knob, .width = knob, .hpf = knob, .lpf = knob});
			expect(c.reverb_time).to_equal(reference->reverb_time());
			expect(c.diffusion).to_equal(reference->diffusion());
			expect(c.lp).to_equal(reference->lp());
			expect(c.hp_cutoff).to_equal(reference->hp_cutoff());
			expect(c.lp_cutoff).to_equal(reference->lp_cutoff());
		}
	});

	it("keeps both LFO lanes within the reach the read bounds allow", _ {
		// The lanes settle at their widest within 40 million frames.
		expect(lfo_lanes_out_of_range(40'000'000)).to_equal(std::size_t{0});
	});

	context("the Mutable loop", _ {
		it("does not run away at the longest room size", _ {
			// Max size with no damping is close to a freeze by design: the tail need only be falling.
			auto model = make_model(rw::Model::MUTABLE, 1.0f, 0.0f);
			const std::vector<double> envelope = deluge::dsp::test::impulse_envelope(model->render(), 30.0);
			const std::size_t eighth = envelope.size() / 8;
			const double early = *std::max_element(envelope.begin() + eighth, envelope.begin() + (2 * eighth));
			const double late = *std::max_element(envelope.end() - eighth, envelope.end());
			expect(late).to_be_less_than(early);
		});

		it("darkens the tail as damping rises", _ {
			auto open = make_model(rw::Model::MUTABLE, 0.5f, 0.0f);
			auto damped = make_model(rw::Model::MUTABLE, 0.5f, 1.0f);
			expect(tail_brightness(*damped, 4.0)).to_be_less_than(tail_brightness(*open, 4.0));
		});

		it("lengthens the tail as the room grows", _ {
			auto small = make_model(rw::Model::MUTABLE, 0.2f, 0.0f);
			auto large = make_model(rw::Model::MUTABLE, 0.8f, 0.0f);
			expect(deluge::dsp::test::rt60(large->render(), 20.0)).to_be_greater_than(deluge::dsp::test::rt60(small->render(), 20.0));
		});

		it("clips the tail instead of inverting it", _ {
			expect(overdrive_sign_disagreement(rw::Model::MUTABLE)).to_be_less_than(0.5);
		});
	});

	it("renders each Digital workload's output and state as the reference recorded", _ {
		PlateRunner::foreign_models = 0;
		for (const rw::Workload& w : rw::all_workloads() | std::views::filter(is_digital)) {
			const rw::WorkloadRecord got = rw::record<PlateRunner>(w);
			const rw::WorkloadRecord& want = rw::kRecordedReference.at(static_cast<std::size_t>(w.id));
			expect(got.outputs).to_equal(want.outputs);
			expect(got.state).to_equal(want.state);
			expect(got.send).to_equal(rw::record_send_untouched(w));
		}
		expect(PlateRunner::foreign_models).to_equal(std::size_t{0});
	});

	it("computes the reference's Digital coefficients", _ {
		auto reference = std::make_unique<deluge::dsp::test::reverb_ref::ReferenceDigital>();
		for (int32_t k = 0; k <= 50; ++k) {
			const float knob = static_cast<float>(k) / 50.f;
			reference->setRoomSize(knob);
			reference->setDamping(knob);
			reference->setWidth(knob);
			reference->setHPF(knob);
			reference->setLPF(knob);
			const Plate::Coefficients c = Plate::coefficients(
			    {.mode = Plate::Mode::DIGITAL, .room_size = knob, .damping = knob, .width = knob, .hpf = knob, .lpf = knob});
			expect(c.reverb_time).to_equal(reference->reverb_time());
			// decay_diffusion_2 is pinned through the Digital workloads' records, which read it every frame.
			expect(c.lp).to_equal(reference->lp());
			expect(c.hp_cutoff).to_equal(reference->hp_cutoff());
			expect(c.lp_cutoff).to_equal(reference->lp_cutoff());
		}
	});

	it("lays out every Digital line as the reference topology truncates it", _ {
		auto reference = std::make_unique<deluge::dsp::test::reverb_ref::ReferenceDigital>();
		const auto lines = reference->digital_topology();
		const Plate::DigitalLines& d = Plate::kDigital;
		const std::array<Plate::Line, 12> mine{d.ap1,   d.ap2,   d.ap3,   d.ap4,   d.dap1a, d.del1a,
		                                       d.dap1b, d.del1b, d.dap2a, d.del2a, d.dap2b, d.del2b};
		for (std::size_t i = 0; i < mine.size(); ++i) {
			expect(mine[i].base).to_equal(lines[i].base);
			expect(mine[i].length).to_equal(lines[i].length);
		}
	});

	context("the Digital loop", _ {
		it("holds energy the way a unity-gain allpass tank should", _ {
			// The tank loses only decay^4 a round trip; a bare delay standing in for a modulated allpass would cost
			// another ~0.49 a lap and drag the tail far short of what the room knob promises.
			auto model = make_model(rw::Model::DIGITAL, 1.0f, 0.0f);
			expect(deluge::dsp::test::rt60(model->render(), 20.0)).to_be_between(4.0, 9.0);
		});

		it("lengthens the tail as the room grows", _ {
			auto small = make_model(rw::Model::DIGITAL, 0.2f, 0.0f);
			auto large = make_model(rw::Model::DIGITAL, 0.8f, 0.0f);
			expect(deluge::dsp::test::rt60(large->render(), 20.0)).to_be_greater_than(deluge::dsp::test::rt60(small->render(), 20.0));
		});

		it("settles to silence even at the longest decay", _ {
			auto model = make_model(rw::Model::DIGITAL, 1.0f, 0.0f);
			expect(deluge::dsp::test::rt60(model->render(), 30.0)).to_be_less_than(25.0);
		});

		it("darkens the tail as damping rises", _ {
			auto open = make_model(rw::Model::DIGITAL, 0.8f, 0.0f);
			auto damped = make_model(rw::Model::DIGITAL, 0.8f, 1.0f);
			expect(tail_brightness(*damped, 4.0)).to_be_less_than(tail_brightness(*open, 4.0));
		});

		it("clips the Digital tail instead of inverting it", _ {
			expect(overdrive_sign_disagreement(rw::Model::DIGITAL)).to_be_less_than(0.5);
		});
	});

	it("carries the State across a mode change with no reset", _ {
		// The component resets on every model change; the kernel itself, reconfigured to another mode, resets nothing.
		auto runner = std::make_unique<PlateRunner>();
		runner->set_model(rw::Model::MUTABLE);
		runner->set_knobs(rw::kTurned);
		std::vector<int32_t> send(128);
		std::vector<StereoSample> out(128);
		uint32_t seed = 1;
		for (int32_t block = 0; block < 20; ++block) {
			for (int32_t& v : send) {
				seed = (seed * 1664525U) + 1013904223U;
				v = static_cast<int32_t>(seed) >> 2;
			}
			runner->render(send, out, rw::kCentred, rw::kCentred);
		}
		const Plate::State before = runner->mono.state();
		const auto nonzero = [&runner] { return std::ranges::count_if(runner->memory, [](float f) { return f != 0.f; }); };
		const auto nonzero_before = nonzero();
		const auto lanes = [](const auto& v) {
			std::array<float, 2> l{};
			v.StoreTo(l.data());
			return std::array<uint32_t, 2>{std::bit_cast<uint32_t>(l[0]), std::bit_cast<uint32_t>(l[1])};
		};

		runner->settings.mode = Plate::Mode::DIGITAL;
		runner->mono.configure(runner->settings);
		const Plate::State& after = runner->mono.state();
		expect(nonzero()).to_equal(nonzero_before);
		expect(after.write).to_equal(before.write);
		expect(lanes(after.lfo.y_0)).to_equal(lanes(before.lfo.y_0));
		expect(lanes(after.lfo.y_1)).to_equal(lanes(before.lfo.y_1));
		expect(after.lp_decay_1).to_equal(before.lp_decay_1);
		expect(after.lp_decay_2).to_equal(before.lp_decay_2);
		expect(after.hp_l).to_equal(before.hp_l);
		expect(after.hp_r).to_equal(before.hp_r);
		expect(after.lp_l).to_equal(before.lp_l);
		expect(after.lp_r).to_equal(before.lp_r);

		runner->render(send, out, rw::kCentred, rw::kCentred);
		expect(runner->mono.state().write).to_equal((before.write - 128 + 32768) % 32768);
	});
});
// clang-format on

CPPSPEC_MAIN(plate_kernel);
