#include "dsp/reverb/digital.hpp"
#include "dsp/reverb/mutable.hpp"
#include "dsp/reverb/reverb.hpp"

#include "cppspec.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#ifndef DELUGE_DSP_BOUNDS_CHECK
#error "reverb_spec needs DELUGE_DSP_BOUNDS_CHECK; see tests/spec/CMakeLists.txt"
#endif

using deluge::dsp::Reverb;
namespace rv = deluge::dsp::reverb;

namespace {

constexpr size_t kBlock = 64;

/// The LFOs are configured for a cycle every 2-4 seconds, so the trace has to be long
/// enough to count a useful number of them.
constexpr double kTraceSeconds = 40.0;

struct LfoTrace {
	double hz_1;
	double hz_2;
	/// Largest |LFO_1 - LFO_2| over the trace. Stays near zero if the two lanes have
	/// collapsed onto a single coefficient.
	float divergence;
};

/// Drives an FxEngine the way the reverbs do -- Advance() once per frame, then one LFO
/// read per modulated delay line -- and measures what the modulators actually do.
LfoTrace traceLfo() {
	std::vector<float> buffer(1024);
	rv::FxEngine engine{buffer, {0.5f / kSampleRate, 0.3f / kSampleRate}};

	LfoTrace trace{0.0, 0.0, 0.0f};
	size_t crossings_1 = 0;
	size_t crossings_2 = 0;
	float previous_1 = 0.f;
	float previous_2 = 0.f;

	const auto frames = static_cast<size_t>(kTraceSeconds * kSampleRate);
	for (size_t frame = 0; frame < frames; ++frame) {
		engine.Advance();
		// Read order matches the reverbs: the branch driven by LFO_2 comes first.
		const float lfo_2 = engine.LFO(rv::LFO_2);
		const float lfo_1 = engine.LFO(rv::LFO_1);

		trace.divergence = std::max(trace.divergence, std::abs(lfo_1 - lfo_2));
		if (frame > 0) {
			// The oscillators swing between 0 and 1, so count upward midpoint crossings.
			if (previous_1 <= 0.5f && lfo_1 > 0.5f) {
				++crossings_1;
			}
			if (previous_2 <= 0.5f && lfo_2 > 0.5f) {
				++crossings_2;
			}
		}
		previous_1 = lfo_1;
		previous_2 = lfo_2;
	}

	trace.hz_1 = static_cast<double>(crossings_1) / kTraceSeconds;
	trace.hz_2 = static_cast<double>(crossings_2) / kTraceSeconds;
	return trace;
}

/// The buffer alone is 128 KB, so these always live on the heap.
template <typename Model>
std::unique_ptr<Model> makeModel(float room_size, float damping) {
	auto model = std::make_unique<Model>();
	model->setRoomSize(room_size);
	model->setDamping(damping);
	model->setWidth(0.5f);
	model->setHPF(0.f);
	model->setLPF(1.f);
	model->setPanLevels(1 << 30, 1 << 30);
	return model;
}

/// Feeds one impulse and returns the per-block peak envelope of the left channel.
template <typename Model>
std::vector<double> impulseEnvelope(Model& model, double seconds) {
	std::vector<int32_t> input(kBlock, 0);
	std::vector<StereoSample> output(kBlock);
	input[0] = 1 << 28;

	const auto blocks = static_cast<size_t>(seconds * kSampleRate / kBlock);
	std::vector<double> envelope;
	envelope.reserve(blocks);

	for (size_t block = 0; block < blocks; ++block) {
		std::fill(output.begin(), output.end(), StereoSample{.l = 0, .r = 0});
		model.process(input, output);
		std::fill(input.begin(), input.end(), 0);

		double peak = 0.0;
		for (const StereoSample& sample : output) {
			peak = std::max(peak, std::abs(static_cast<double>(sample.l)));
		}
		envelope.push_back(peak);
	}
	return envelope;
}

/// Seconds for the impulse response to fall 60 dB below its peak, or `seconds` if it
/// never gets there.
template <typename Model>
double rt60(Model& model, double seconds) {
	const std::vector<double> envelope = impulseEnvelope(model, seconds);
	const double peak = *std::max_element(envelope.begin(), envelope.end());
	const double decayed = peak / 1000.0;

	for (size_t block = envelope.size(); block-- > 0;) {
		if (envelope[block] > decayed) {
			return static_cast<double>(block + 1) * kBlock / kSampleRate;
		}
	}
	return seconds;
}

/// A crude spectral tilt for the settled part of the tail: mean sample-to-sample change over
/// mean level. Damping rolls the top end off the tank, so this falls as damping rises.
template <typename Model>
double tailBrightness(Model& model, double seconds) {
	std::vector<int32_t> input(kBlock, 0);
	std::vector<StereoSample> output(kBlock);
	input[0] = 1 << 28;

	const auto blocks = static_cast<size_t>(seconds * kSampleRate / kBlock);
	const size_t settled = blocks / 8; // let the early reflections pass first
	double difference = 0.0;
	double level = 0.0;
	double previous = 0.0;

	for (size_t block = 0; block < blocks; ++block) {
		std::fill(output.begin(), output.end(), StereoSample{.l = 0, .r = 0});
		model.process(input, output);
		std::fill(input.begin(), input.end(), 0);

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

/// Renders the same sine through a fresh model at the given input level.
template <typename Model>
std::vector<int32_t> renderAt(int input_shift, double seconds) {
	auto model = makeModel<Model>(0.5f, 0.5f);
	std::vector<int32_t> input(kBlock);
	std::vector<StereoSample> output(kBlock);
	std::vector<int32_t> captured;

	const auto blocks = static_cast<size_t>(seconds * kSampleRate / kBlock);
	for (size_t block = 0; block < blocks; ++block) {
		for (size_t i = 0; i < kBlock; ++i) {
			const auto t = static_cast<double>(block * kBlock + i);
			input[i] = static_cast<int32_t>(std::ldexp(1.0, input_shift) * std::sin(0.05 * t));
		}
		std::fill(output.begin(), output.end(), StereoSample{.l = 0, .r = 0});
		model->process(input, output);

		if (block >= blocks / 4) { // let the tank fill
			for (const StereoSample& sample : output) {
				captured.push_back(sample.l);
			}
		}
	}
	return captured;
}

/// Percentage of samples where an overdriven render disagrees in sign with a clean one.
///
/// The reverb is a linear system up to the output conversion, so a hot render and a quiet one
/// must agree in sign everywhere. Saturation preserves that -- it only shortens a sample.
/// An unchecked narrowing cast does not: on x86 an over-range float becomes INT32_MIN, so
/// every clipped peak comes back inverted.
template <typename Model>
double overdriveSignDisagreement() {
	const std::vector<int32_t> hot = renderAt<Model>(30, 0.6);
	const std::vector<int32_t> quiet = renderAt<Model>(18, 0.6);

	long long checked = 0;
	long long disagreed = 0;
	for (size_t i = 0; i < hot.size(); ++i) {
		if (std::abs(quiet[i]) < 10000) {
			continue; // reference too close to zero to have a meaningful sign
		}
		++checked;
		if ((hot[i] < 0) != (quiet[i] < 0)) {
			++disagreed;
		}
	}
	return checked > 0 ? 100.0 * static_cast<double>(disagreed) / static_cast<double>(checked) : 0.0;
}

/// True if any delay line touched a slot outside its own reserved region while `body` ran.
template <typename Body>
bool readsOutOfBounds(Body body) {
	rv::debug::out_of_bounds_access = false;
	body();
	return rv::debug::out_of_bounds_access;
}

} // namespace

// clang-format off
describe reverb("Reverb", $ {
	context("the FxEngine modulators", _ {
		const LfoTrace trace = traceLfo();

		it("runs LFO_1 at the 0.5 Hz it is configured for", _ {
			expect(trace.hz_1).to_be_between(0.40, 0.55);
		});

		it("runs LFO_2 at the 0.3 Hz it is configured for", _ {
			expect(trace.hz_2).to_be_between(0.24, 0.34);
		});

		it("keeps the two modulators independent", _ {
			// The approximate cosine builds its coefficient as 2 - 32f^2. Configure it
			// with a small enough f and that rounds to exactly 2.0f for both lanes, which
			// silently fuses the two LFOs into one and costs the tank its decorrelation.
			expect(trace.divergence).to_be_greater_than(0.1f);
		});
	});

	context("delay line addressing", _ {
		it("keeps every Mutable read inside its own reserved region", _ {
			expect(readsOutOfBounds([] {
				for (float room_size : {0.0f, 0.5f, 1.0f}) {
					auto model = makeModel<rv::Mutable>(room_size, 0.5f);
					impulseEnvelope(*model, 0.5);
				}
			})).to_be_false();
		});

		it("keeps every Digital read inside its own reserved region", _ {
			// The modulated allpasses are the ones at risk: an interpolated read touches
			// both offset and offset + 1, so the line has to be a slot longer than the
			// furthest the LFO can push it.
			expect(readsOutOfBounds([] {
				for (float room_size : {0.0f, 0.5f, 1.0f}) {
					auto model = makeModel<rv::Digital>(room_size, 0.5f);
					impulseEnvelope(*model, 0.5);
				}
			})).to_be_false();
		});
	});

	context("the Digital plate's tank", _ {
		it("holds energy the way a unity-gain allpass tank should", _ {
			// Dattorro's tank loses only decay^4 per round trip. Standing a bare delay in
			// for either modulated allpass costs another ~0.49 a lap, which drags the tail
			// far below what the Size knob promises.
			auto model = makeModel<rv::Digital>(1.0f, 0.0f);
			expect(rt60(*model, 20.0)).to_be_between(4.0, 9.0);
		});

		it("lengthens the tail as the room grows", _ {
			auto small = makeModel<rv::Digital>(0.2f, 0.0f);
			auto large = makeModel<rv::Digital>(0.8f, 0.0f);
			expect(rt60(*large, 20.0)).to_be_greater_than(rt60(*small, 20.0));
		});

		it("settles to silence even at the longest decay", _ {
			auto model = makeModel<rv::Digital>(1.0f, 0.0f);
			expect(rt60(*model, 30.0)).to_be_less_than(25.0);
		});

		it("darkens the tail as damping rises", _ {
			auto open = makeModel<rv::Digital>(0.8f, 0.0f);
			auto damped = makeModel<rv::Digital>(0.8f, 1.0f);
			expect(tailBrightness(*damped, 4.0)).to_be_less_than(tailBrightness(*open, 4.0));
		});
	});

	context("the Mutable reverb", _ {
		it("does not run away at the longest room size", _ {
			// Max size with damping wide open is close to a freeze by design, so don't demand
			// silence -- just that the tail is falling rather than feeding itself.
			auto model = makeModel<rv::Mutable>(1.0f, 0.0f);
			const std::vector<double> envelope = impulseEnvelope(*model, 30.0);
			const size_t eighth = envelope.size() / 8;
			const double early = *std::max_element(envelope.begin() + eighth, envelope.begin() + 2 * eighth);
			const double late = *std::max_element(envelope.end() - eighth, envelope.end());
			expect(late).to_be_less_than(early);
		});

		it("darkens the tail as damping rises", _ {
			// The damping filter passes DC at unity, so damping barely moves RT60 -- what it
			// does is take the top off. The curve used to run backwards, leaving both ends of
			// the knob wide open and this comparison dead even.
			auto open = makeModel<rv::Mutable>(0.5f, 0.0f);
			auto damped = makeModel<rv::Mutable>(0.5f, 1.0f);
			expect(tailBrightness(*damped, 4.0)).to_be_less_than(tailBrightness(*open, 4.0));
		});

		it("lengthens the tail as the room grows", _ {
			auto small = makeModel<rv::Mutable>(0.2f, 0.0f);
			auto large = makeModel<rv::Mutable>(0.8f, 0.0f);
			expect(rt60(*large, 20.0)).to_be_greater_than(rt60(*small, 20.0));
		});
	});

	context("driven past full scale", _ {
		// Both models normalise to float, run the loop, then scale back to q31. An out-of-range
		// narrowing cast is UB, and the targets disagree about which way it goes: ARM saturates
		// positive, x86 hands back INT32_MIN. Clamp before converting so a hot send clips
		// rather than inverting.
		it("clips the Mutable tail instead of inverting it", _ {
			expect(overdriveSignDisagreement<rv::Mutable>()).to_be_less_than(0.5);
		});

		it("clips the Digital tail instead of inverting it", _ {
			expect(overdriveSignDisagreement<rv::Digital>()).to_be_less_than(0.5);
		});
	});

	context("the model selector", _ {
		it("starts every filter parameter at a known value", _ {
			// hpf_ was once missing from the constructor's init list, so setModel() pushed
			// whatever the stack happened to hold into the model it had just built.
			auto reverb = std::make_unique<Reverb>();
			reverb->setModel(Reverb::Model::MUTABLE);
			expect(reverb->getHPF()).to_equal(0.f);
			expect(reverb->getLPF()).to_equal(0.f);
		});

		it("carries its parameters onto the newly selected model", _ {
			auto reverb = std::make_unique<Reverb>();
			reverb->setModel(Reverb::Model::MUTABLE);
			reverb->setRoomSize(0.75f);
			reverb->setDamping(0.25f);
			reverb->setHPF(0.4f);

			reverb->setModel(Reverb::Model::DIGITAL);

			expect(reverb->getRoomSize()).to_be_within(0.001f).of(0.75f);
			expect(reverb->getDamping()).to_be_within(0.001f).of(0.25f);
			expect(reverb->getHPF()).to_be_within(0.001f).of(0.4f);
		});

		it("renders through whichever model is selected", _ {
			auto reverb = std::make_unique<Reverb>();
			reverb->setPanLevels(1 << 30, 1 << 30);
			reverb->setRoomSize(0.7f);

			for (Reverb::Model model : {Reverb::Model::FREEVERB, Reverb::Model::MUTABLE,
			                            Reverb::Model::DIGITAL}) {
				reverb->setModel(model);
				expect(rt60(*reverb, 5.0)).to_be_greater_than(0.05);
			}
		});
	});
});
// clang-format on

CPPSPEC_SPEC(reverb)
