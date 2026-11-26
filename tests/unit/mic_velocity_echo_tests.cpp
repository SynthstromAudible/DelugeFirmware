// Mic velocity's echo canceller (src/deluge/dsp/mic_velocity/echo_canceller.h): on a synthetic echo path, and on a
// song recorded through a Deluge's speaker and internal mic (data/mic_velocity/song.wav).

#include "CppUTest/TestHarness.h"
#include "dsp/mic_velocity/echo_canceller.h"
#include "dsp/mic_velocity/thump_detector.h"
#include "mic_velocity_test_data.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace deluge::dsp::mic_velocity;
using namespace mic_velocity_test;

namespace {

struct Noise {
	uint32_t state = 777;
	float next() {
		state = state * 1664525u + 1013904223u;
		return static_cast<float>(static_cast<int32_t>(state)) / 2147483648.f;
	}
};

/// A short echo path at the decimated rate: what the mic hears of sent[n] is mostly 10-12 samples (~2 ms) later
struct EchoPath {
	std::vector<float> history = std::vector<float>(32, 0.f);
	float process(float sent) {
		history.insert(history.begin(), sent);
		history.pop_back();
		return 0.4f * history[10] - 0.25f * history[11] + 0.1f * history[12] + 0.05f * history[20];
	}
};

float dB(double power) {
	return static_cast<float>(10 * std::log10(std::max(power, 1e-24)));
}

constexpr uint32_t ms(float m) {
	return static_cast<uint32_t>(m * kInputRate / 1000);
}

} // namespace

TEST_GROUP(MicVelocityEcho){};

TEST(MicVelocityEcho, learnsAnEchoPathAndTakesItOut) {
	EchoCanceller aec;
	EchoPath path;
	Noise noise, roomNoise;
	double micPower = 0, residualPower = 0;
	int32_t const n = static_cast<int32_t>(kRate * 4);
	for (int32_t i = 0; i < n; i++) {
		float const sent = 0.1f * noise.next();
		float const mic = path.process(sent) + 1e-4f * roomNoise.next();
		float const residual = aec.process(mic, sent, true);
		if (i > n * 3 / 4) {
			micPower += mic * mic;
			residualPower += residual * residual;
		}
	}
	CHECK(dB(micPower) - dB(residualPower) > 25.f);
}

TEST(MicVelocityEcho, frozenItLearnsNothing) {
	EchoCanceller aec;
	EchoPath path;
	Noise noise;
	for (int32_t i = 0; i < static_cast<int32_t>(kRate); i++) {
		float const sent = 0.1f * noise.next();
		float const mic = path.process(sent);
		DOUBLES_EQUAL(mic, aec.process(mic, sent, false), 0);
	}
	DOUBLES_EQUAL(0, aec.weightEnergy(), 0);
}

TEST(MicVelocityEcho, nothingSentLearnsNothing) {
	EchoCanceller aec;
	Noise noise;
	for (int32_t i = 0; i < static_cast<int32_t>(kRate); i++) {
		aec.process(0.01f * noise.next(), 0.f, true);
	}
	DOUBLES_EQUAL(0, aec.weightEnergy(), 0);
}

TEST(MicVelocityEcho, aThumpWhileFrozenComesThrough) {
	EchoCanceller aec;
	EchoPath path;
	Noise noise;
	int32_t const learn = static_cast<int32_t>(kRate * 3);
	double thumpIn = 0, thumpError = 0;
	for (int32_t i = 0; i < learn + 200; i++) {
		float const sent = 0.1f * noise.next();
		float const echo = path.process(sent);
		float thump = 0;
		if (i >= learn) {
			float const t = static_cast<float>(i - learn) / kRate;
			thump = 0.05f * std::exp(-t / 0.006f) * std::sin(2 * 3.14159265f * 180.f * t);
		}
		float const residual = aec.process(echo + thump, sent, i < learn);
		if (i >= learn) {
			thumpIn += thump * thump;
			thumpError += (residual - thump) * (residual - thump);
		}
	}
	// What's left once the echo is out is the thump, to within 20 dB
	CHECK(dB(thumpIn) - dB(thumpError) > 20.f);
}

// A song played through a Deluge's speaker, heard by its mic: 1.2 s of the song alone, then normal and hard taps on
// the centre pad over it. Frozen as the firmware freezes it: while a thump is going on, and for 60 ms after a press.
TEST(MicVelocityEcho, recordedSongComesDownAndTapsStillRead) {
	Clip const clip = readClip("song.wav", 2);
	std::vector<Press> const presses = readPresses("song.csv");
	LONGS_EQUAL(5, presses.size());

	auto run = [&](bool cancel, float* songFloor, std::vector<float>* levels) {
		Decimator micDecimator, sentDecimator;
		EchoCanceller aec;
		ThumpDetector detector;
		std::vector<Thump> thumps;
		for (uint32_t f = 0; f < clip.mic.size(); f++) {
			auto mic = micDecimator.push(clip.mic[f]);
			auto sent = sentDecimator.push(clip.sent[f]);
			if (!mic) {
				continue;
			}
			bool afterPress = false;
			for (Press const& p : presses) {
				afterPress |= f >= p.frame && f < p.frame + ms(60);
			}
			float const x = cancel ? aec.process(*mic, *sent, !(detector.inThump() || afterPress)) : *mic;
			detector.process(x, f);
			if (f == presses[0].frame - ms(70)) {
				*songFloor = 20 * std::log10(detector.noiseFloor());
			}
			if (Thump const* t = detector.recent(0)) {
				if (thumps.empty() || thumps.back().onset != t->onset) {
					thumps.push_back(*t);
				}
				else {
					thumps.back() = *t;
				}
			}
		}
		for (Press const& p : presses) {
			float best = -200;
			for (Thump const& t : thumps) {
				if (t.onset + ms(25) >= p.frame && t.onset <= p.frame + ms(4)) {
					best = std::max(best, 20 * std::log10(t.level));
				}
			}
			levels->push_back(best);
		}
	};

	float floorRaw = 0, floorCancelled = 0;
	std::vector<float> levelsRaw, levelsCancelled;
	run(false, &floorRaw, &levelsRaw);
	run(true, &floorCancelled, &levelsCancelled);

	// The song's floor comes down by at least 5 dB
	CHECK(floorRaw - floorCancelled > 5.f);
	// Every tap is found over the song with it cancelled, and normal ones read under hard ones
	for (size_t i = 0; i < presses.size(); i++) {
		CHECK(levelsCancelled[i] > -100.f);
	}
	// It mustn't eat the thumps: hard taps, well over the song, read the same either way. (Quieter ones read a little
	// higher uncancelled, from the song adding in.)
	for (size_t i = 3; i < presses.size(); i++) {
		DOUBLES_EQUAL(levelsRaw[i], levelsCancelled[i], 1.0);
	}
	CHECK(*std::min_element(levelsCancelled.begin() + 3, levelsCancelled.end())
	      > *std::max_element(levelsCancelled.begin(), levelsCancelled.begin() + 3) + 4.f);
}
