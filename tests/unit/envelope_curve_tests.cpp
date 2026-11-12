#include "CppUTest/TestHarness.h"
#include "modulation/envelope_curve.h"
#include <cmath>

using deluge::modulation::EnvelopeCurve;
using deluge::modulation::fastExp2;

TEST_GROUP(EnvelopeCurveTest){};

TEST(EnvelopeCurveTest, fastExp2MatchesExp2) {
	for (float x = -20.f; x < 20.f; x += 0.0137f) {
		DOUBLES_EQUAL(1.0, fastExp2(x) / std::exp2(static_cast<double>(x)), 3e-7);
	}
	DOUBLES_EQUAL(1.0, fastExp2(0.f), 0.0);
	DOUBLES_EQUAL(0.25, fastExp2(-2.f), 0.0);
}

TEST(EnvelopeCurveTest, endpointsAreExact) {
	for (float k : {-8.f, -1.f, 0.f, 0.5f, 4.f, 8.f, 12.f}) {
		EnvelopeCurve curve(k);
		DOUBLES_EQUAL(1.0, curve.falling(0.f), 1e-6);
		DOUBLES_EQUAL(0.0, curve.falling(1.f), 0.0);
		DOUBLES_EQUAL(0.0, curve.rising(0.f), 1e-6);
		DOUBLES_EQUAL(1.0, curve.rising(1.f), 0.0);
	}
}

TEST(EnvelopeCurveTest, matchesClassicDecayTables) {
	// decayTableSmall8[128] and decayTableSmall4[128], out of 65535
	DOUBLES_EQUAL(3855.0 / 65535.0, EnvelopeCurve(8.f).falling(0.5f), 2e-5);
	DOUBLES_EQUAL(13107.0 / 65535.0, EnvelopeCurve(4.f).falling(0.5f), 2e-5);
	DOUBLES_EQUAL(0.5, EnvelopeCurve(0.f).falling(0.5f), 0.0);
}

TEST(EnvelopeCurveTest, monotonic) {
	for (float k : {-8.f, -0.01f, 0.f, 0.01f, 4.f, 8.f, 16.f}) {
		EnvelopeCurve curve(k);
		float previous = 2.f;
		for (int32_t i = 0; i <= 4096; i++) {
			float value = curve.falling(static_cast<float>(i) / 4096.f);
			CHECK(value <= previous);
			previous = value;
		}
	}
}

TEST(EnvelopeCurveTest, risingInverse) {
	for (float k : {-4.f, 0.f, 4.f, 8.f}) {
		EnvelopeCurve curve(k);
		for (float x : {0.f, 0.1f, 0.5f, 0.9f}) {
			DOUBLES_EQUAL(x, curve.risingInverse(curve.rising(x)), 1e-4);
		}
	}
}
