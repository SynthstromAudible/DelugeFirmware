// Mic velocity's thump detector (src/deluge/dsp/mic_velocity/thump_detector.h): its filters, and its thumps on
// synthetic signals and on taps recorded from a Deluge's internal mic (data/mic_velocity/taps.wav).

#include "CppUTest/TestHarness.h"
#include "dsp/mic_velocity/thump_detector.h"
#include "mic_velocity_test_data.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

using namespace deluge::dsp::mic_velocity;
using namespace mic_velocity_test;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

int32_t toQ31(float x) {
	return static_cast<int32_t>(std::lround(std::clamp(x, -1.f, 0.99999f) * 2147483648.0));
}

float dB(float v) {
	return 20.f * std::log10(std::max(v, 1e-12f));
}

/// Runs q31 samples through a decimator into a detector, as the firmware will, keeping every thump it finds (the
/// detector itself only keeps the last kNumThumps)
struct Chain {
	Decimator decimator;
	ThumpDetector detector;
	Frame frame = 0;
	std::vector<Thump> thumps;

	void push(int32_t sample) {
		if (auto d = decimator.push(sample)) {
			detector.process(*d, frame);
			if (Thump const* t = detector.recent(0)) {
				if (thumps.empty() || thumps.back().onset != t->onset) {
					thumps.push_back(*t);
				}
				else {
					thumps.back() = *t;
				}
			}
		}
		frame++;
	}

	/// The loudest thump with its onset in [from, to]
	Thump const* loudest(Frame from, Frame to) const {
		Thump const* best = nullptr;
		for (Thump const& t : thumps) {
			if (t.onset >= from && t.onset <= to && (best == nullptr || t.level > best->level)) {
				best = &t;
			}
		}
		return best;
	}

	int32_t countIn(Frame from, Frame to) const {
		return static_cast<int32_t>(std::count_if(thumps.begin(), thumps.end(),
		                                          [&](Thump const& t) { return t.onset >= from && t.onset <= to; }));
	}
};

/// Deterministic white noise
struct Noise {
	uint32_t state = 12345;
	float next() {
		state = state * 1664525u + 1013904223u;
		return static_cast<float>(static_cast<int32_t>(state)) / 2147483648.f;
	}
};

/// A thump-like burst: a decaying 180 Hz sine
float burst(int32_t n) {
	float const t = static_cast<float>(n) / kInputRate;
	return std::exp(-t / 0.006f) * (1.f - std::exp(-t / 0.0005f)) * std::sin(2 * kPi * 180.f * t);
}

/// Steady-state gain of a biquad at a frequency, by running a sine through it
float gainAt(Biquad f, float hz, float rate) {
	float peak = 0;
	int32_t const n = static_cast<int32_t>(rate * 0.5f);
	for (int32_t i = 0; i < n; i++) {
		float const y = f.process(std::sin(2 * kPi * hz * static_cast<float>(i) / rate));
		if (i > n / 2) {
			peak = std::max(peak, std::abs(y));
		}
	}
	return peak;
}

constexpr Frame ms(float m) {
	return static_cast<Frame>(m * kInputRate / 1000);
}

} // namespace

TEST_GROUP(MicVelocityFilters){};

TEST(MicVelocityFilters, decimatorPassesDcAtUnityGain) {
	Decimator d;
	float last = 0;
	for (int32_t i = 0; i < 64; i++) {
		if (auto out = d.push(toQ31(0.25f))) {
			last = *out;
		}
	}
	DOUBLES_EQUAL(0.25, last, 1e-4);
}

TEST(MicVelocityFilters, decimatorRejectsWhatWouldAliasIntoTheThumpBand) {
	// 5512.5 + 200 Hz would fold down to 200 Hz at the decimated rate: it must come out well under a 200 Hz tone
	auto run = [](float hz) {
		Decimator d;
		float peak = 0;
		for (int32_t i = 0; i < kInputRate / 4; i++) {
			if (auto out = d.push(toQ31(0.5f * std::sin(2 * kPi * hz * static_cast<float>(i) / kInputRate)))) {
				if (i > kInputRate / 8) {
					peak = std::max(peak, std::abs(*out));
				}
			}
		}
		return peak;
	};
	float const inBand = run(200.f);
	float const aliasing = run(kRate + 200.f);
	CHECK(dB(inBand) > -7.f);
	CHECK(dB(aliasing) - dB(inBand) < -25.f);
}

TEST(MicVelocityFilters, biquadsHaveTheirCutoffs) {
	DOUBLES_EQUAL(-3.0, dB(gainAt(Biquad::lowPass2(300.f, kRate), 300.f, kRate)), 0.3);
	DOUBLES_EQUAL(0.0, dB(gainAt(Biquad::lowPass2(300.f, kRate), 20.f, kRate)), 0.1);
	CHECK(dB(gainAt(Biquad::lowPass2(300.f, kRate), 1200.f, kRate)) < -22.f);
	DOUBLES_EQUAL(-3.0, dB(gainAt(Biquad::highPass1(60.f, kRate), 60.f, kRate)), 0.3);
	DOUBLES_EQUAL(0.0, dB(gainAt(Biquad::highPass1(60.f, kRate), 1000.f, kRate)), 0.2);
	DOUBLES_EQUAL(-3.0, dB(gainAt(Biquad::lowPass1(300.f, kRate), 300.f, kRate)), 0.3);
}

TEST_GROUP(MicVelocityDetector){};

TEST(MicVelocityDetector, noiseAloneStartsNoThumps) {
	Chain c;
	Noise noise;
	for (int32_t i = 0; i < kInputRate * 3; i++) {
		c.push(toQ31(0.001f * noise.next()));
	}
	LONGS_EQUAL(0, c.countIn(ms(500), c.frame));
	CHECK(c.detector.noiseFloor() > 0);
}

TEST(MicVelocityDetector, findsABurstAtItsStart) {
	Chain c;
	Noise noise;
	Frame const start = kInputRate;
	for (int32_t i = 0; i < kInputRate * 2; i++) {
		float x = 0.0005f * noise.next();
		if (static_cast<Frame>(i) >= start) {
			x += 0.05f * burst(i - static_cast<int32_t>(start));
		}
		c.push(toQ31(x));
	}
	LONGS_EQUAL(1, c.countIn(0, c.frame));
	Thump const* t = c.detector.recent(0);
	CHECK(t != nullptr);
	CHECK(t->settled);
	CHECK(t->onset >= start && t->onset < start + ms(2));
}

TEST(MicVelocityDetector, levelFollowsTheBurstsAmplitude) {
	float levels[3];
	float const amplitudes[3] = {0.01f, 0.03f, 0.1f};
	for (int32_t k = 0; k < 3; k++) {
		Chain c;
		Noise noise;
		for (int32_t i = 0; i < kInputRate; i++) {
			float x = 0.0003f * noise.next();
			if (i >= kInputRate / 2) {
				x += amplitudes[k] * burst(i - kInputRate / 2);
			}
			c.push(toQ31(x));
		}
		Thump const* t = c.loudest(0, c.frame);
		CHECK(t != nullptr);
		levels[k] = dB(t->level);
	}
	// ~9.5 dB per step in amplitude, and the level should step by about as much
	DOUBLES_EQUAL(9.5, levels[1] - levels[0], 1.5);
	DOUBLES_EQUAL(10.5, levels[2] - levels[1], 1.5);
}

TEST(MicVelocityDetector, aBurstsOwnRingingStartsNoSecondThump) {
	Chain c;
	Noise noise;
	for (int32_t i = 0; i < kInputRate; i++) {
		float x = 0.0003f * noise.next();
		int32_t const n = i - kInputRate / 2;
		if (n >= 0) {
			// The burst, then a weaker bounce 18 ms on
			x += 0.05f * burst(n) + (n >= static_cast<int32_t>(ms(18)) ? 0.012f * burst(n - ms(18)) : 0.f);
		}
		c.push(toQ31(x));
	}
	LONGS_EQUAL(1, c.countIn(0, c.frame));
}

TEST(MicVelocityDetector, aSmallSoundJustBeforeAThumpDoesntTakeItsPlace) {
	Chain c;
	Noise noise;
	Frame const thumpAt = kInputRate / 2 + ms(7);
	for (int32_t i = 0; i < kInputRate; i++) {
		float x = 0.0003f * noise.next();
		int32_t const n = i - kInputRate / 2;
		if (n >= 0) {
			// A faint burst (a finger brushing the pad), then the pad's thump 7 ms on, 26 dB louder
			x += 0.0025f * burst(n) + (static_cast<Frame>(i) >= thumpAt ? 0.05f * burst(i - thumpAt) : 0.f);
		}
		c.push(toQ31(x));
	}
	Thump const* t = c.loudest(0, c.frame);
	CHECK(t != nullptr);
	CHECK(t->onset >= thumpAt && t->onset < thumpAt + ms(2));
	Chain alone;
	Noise noise2;
	for (int32_t i = 0; i < kInputRate; i++) {
		alone.push(
		    toQ31(0.0003f * noise2.next() + (static_cast<Frame>(i) >= thumpAt ? 0.05f * burst(i - thumpAt) : 0.f)));
	}
	DOUBLES_EQUAL(dB(alone.loudest(0, alone.frame)->level), dB(t->level), 1.0);
}

TEST(MicVelocityDetector, risingUntilSettled) {
	Chain c;
	Noise noise;
	bool sawRising = false;
	for (int32_t i = 0; i < kInputRate; i++) {
		float x = 0.0003f * noise.next();
		if (i >= kInputRate / 2) {
			x += 0.05f * burst(i - kInputRate / 2);
		}
		c.push(toQ31(x));
		if (c.detector.rising()) {
			sawRising = true;
			Thump const* t = c.detector.recent(0);
			CHECK(c.frame - t->onset <= ThumpDetector::kSettleFrames + kDecimation + 1);
		}
	}
	CHECK(sawRising);
	CHECK(!c.detector.rising());
}

// Taps recorded from a Deluge: the centre and top-left pads, two soft, two normal and two hard each
TEST(MicVelocityDetector, recordedTapsAreFoundAndOrderedByStrength) {
	std::vector<int32_t> const clip = readClip("taps.wav", 1).mic;
	std::vector<Press> const taps = readPresses("taps.csv");
	LONGS_EQUAL(12, taps.size());

	Chain c;
	std::vector<float> levels(taps.size(), 0);
	size_t next = 0;
	for (int32_t sample : clip) {
		c.push(sample);
		// Each press is read once its window (25 ms before it to 4 ms after) has passed and its thump settled
		while (next < taps.size() && c.frame > taps[next].frame + ms(12)) {
			Thump const* t = c.loudest(taps[next].frame - ms(25), taps[next].frame + ms(4));
			CHECK_TEXT(t != nullptr, ("no thump for tap " + std::to_string(next)).c_str());
			CHECK(t->settled);
			levels[next] = dB(t->level);
			next++;
		}
	}
	LONGS_EQUAL(taps.size(), next);

	// Per pad: both soft taps under both normal ones, both normal under both hard, by a few dB
	for (size_t pad = 0; pad < 2; pad++) {
		float const* l = &levels[pad * 6];
		float const softMax = std::max(l[0], l[1]);
		float const normalMin = std::min(l[2], l[3]);
		float const normalMax = std::max(l[2], l[3]);
		float const hardMin = std::min(l[4], l[5]);
		CHECK_TEXT(normalMin - softMax > 2.f, ("soft/normal overlap on pad " + std::to_string(pad)).c_str());
		CHECK_TEXT(hardMin - normalMax > 4.f, ("normal/hard overlap on pad " + std::to_string(pad)).c_str());
	}
}

TEST(MicVelocityDetector, recordedTapsGiveAtMostAPressAndAReleaseThump) {
	std::vector<int32_t> const clip = readClip("taps.wav", 1).mic;
	std::vector<Press> const taps = readPresses("taps.csv");
	Chain c;
	for (int32_t sample : clip) {
		c.push(sample);
	}
	for (Press const& tap : taps) {
		CHECK(c.countIn(tap.frame - ms(25), tap.frame + ms(60)) <= 2);
	}
	// And nothing in the quiet lead-in
	LONGS_EQUAL(0, c.countIn(ms(50), taps[0].frame - ms(30)));
}
