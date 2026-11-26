// Mic velocity's velocity model and calibration checks (src/deluge/dsp/mic_velocity/velocity_map.h)

#include "CppUTest/TestHarness.h"
#include "dsp/mic_velocity/velocity_map.h"

using namespace deluge::dsp::mic_velocity;

namespace {

/// Centre soft -55 / hard -38; corners quieter, the bottom ones much quieter when soft (as measured on a Deluge)
Calibration measuredLike() {
	return Calibration{{-55.f, -64.f, -55.f, -75.f, -73.f}, {-38.f, -45.f, -43.f, -43.f, -45.f}};
}

} // namespace

TEST_GROUP(MicVelocityMap){};

TEST(MicVelocityMap, interpolationIsExactOnTheCalibratedPads) {
	Calibration const c = measuredLike();
	for (int32_t p = 0; p < kNumCalibrationPoints; p++) {
		DOUBLES_EQUAL(c.softDb()[p], c.softAt(kCalibrationPoints[p].x, kCalibrationPoints[p].y), 1e-5);
		DOUBLES_EQUAL(c.hardDb()[p], c.hardAt(kCalibrationPoints[p].x, kCalibrationPoints[p].y), 1e-5);
	}
}

TEST(MicVelocityMap, interpolationFallsBetweenNeighbours) {
	Calibration const c = measuredLike();
	// Halfway between the centre and the bottom-left corner: between their soft levels, nearer neither extreme
	float const soft = c.softAt(3, 2);
	CHECK(soft < c.softDb()[0] && soft > c.softDb()[3]);
	// Every pad's soft and hard (the sidebar's included) stay within the calibrated extremes
	for (int32_t x = 0; x < Calibration::kWidth; x++) {
		for (int32_t y = 0; y < 8; y++) {
			CHECK(c.softAt(x, y) >= -75.f && c.softAt(x, y) <= -55.f);
			CHECK(c.hardAt(x, y) >= -45.f && c.hardAt(x, y) <= -38.f);
		}
	}
}

TEST(MicVelocityMap, offTheGridUsesTheNearestEdge) {
	Calibration const c = measuredLike();
	DOUBLES_EQUAL(c.softAt(17, 7), c.softAt(40, 9), 0);
	DOUBLES_EQUAL(c.hardAt(0, 0), c.hardAt(-3, -1), 0);
}

TEST(MicVelocityMap, softAndHardMapToTheEndsOfTheRange) {
	Calibration const c = measuredLike();
	VelocityCurve curve{.minVelocity = 10, .maxVelocity = 120, .shape = 0};
	LONGS_EQUAL(10, velocityFor(c, curve, 7, 4, -55.f));
	LONGS_EQUAL(120, velocityFor(c, curve, 7, 4, -38.f));
	LONGS_EQUAL(10, velocityFor(c, curve, 7, 4, -90.f)); // Softer than calibrated (or no thump: the noise)
	LONGS_EQUAL(120, velocityFor(c, curve, 7, 4, -10.f));
	// The same level plays louder on a pad that hears taps more quietly
	CHECK(velocityFor(c, curve, 0, 0, -50.f) > velocityFor(c, curve, 7, 4, -50.f));
}

TEST(MicVelocityMap, linearShapeIsLinearInDb) {
	Calibration const c = measuredLike();
	VelocityCurve curve{.minVelocity = 1, .maxVelocity = 127, .shape = 0};
	LONGS_EQUAL(64, velocityFor(c, curve, 7, 4, -46.5f)); // Halfway between -55 and -38
}

TEST(MicVelocityMap, shapeBendsTheMiddle) {
	Calibration const c = measuredLike();
	VelocityCurve linear{.shape = 0};
	VelocityCurve sensitive{.shape = 50};
	VelocityCurve firm{.shape = -50};
	uint8_t const mid = velocityFor(c, linear, 7, 4, -46.5f);
	CHECK(velocityFor(c, sensitive, 7, 4, -46.5f) > mid + 20);
	CHECK(velocityFor(c, firm, 7, 4, -46.5f) < mid - 20);
	// The ends don't move
	LONGS_EQUAL(1, velocityFor(c, sensitive, 7, 4, -55.f));
	LONGS_EQUAL(127, velocityFor(c, firm, 7, 4, -38.f));
}

TEST(MicVelocityMap, velocityRisesWithLevel) {
	Calibration const c = measuredLike();
	VelocityCurve const curve{.shape = 20};
	uint8_t last = 0;
	for (float db = -80.f; db <= -20.f; db += 0.5f) {
		uint8_t const v = velocityFor(c, curve, 4, 5, db);
		CHECK(v >= last);
		CHECK(v >= 1 && v <= 127);
		last = v;
	}
}

TEST(MicVelocityMap, minAboveMaxStillGivesAVelocity) {
	Calibration const c = measuredLike();
	VelocityCurve const curve{.minVelocity = 100, .maxVelocity = 20, .shape = 0};
	LONGS_EQUAL(100, velocityFor(c, curve, 7, 4, -38.f));
}

TEST(MicVelocityMap, checkPointAcceptsGoodTaps) {
	float soft = 0, hard = 0;
	LONGS_EQUAL(static_cast<int32_t>(TapCheck::OK),
	            static_cast<int32_t>(checkPoint({-56.f, -54.f}, {-39.f, -37.f}, &soft, &hard)));
	DOUBLES_EQUAL(-55.0, soft, 1e-5);
	DOUBLES_EQUAL(-38.0, hard, 1e-5);
}

TEST(MicVelocityMap, checkPointCatchesUnevenAndTooCloseTaps) {
	float soft = 0, hard = 0;
	LONGS_EQUAL(static_cast<int32_t>(TapCheck::SOFT_UNEVEN),
	            static_cast<int32_t>(checkPoint({-60.f, -50.f}, {-38.f, -37.f}, &soft, &hard)));
	LONGS_EQUAL(static_cast<int32_t>(TapCheck::HARD_UNEVEN),
	            static_cast<int32_t>(checkPoint({-55.f, -55.f}, {-45.f, -30.f}, &soft, &hard)));
	LONGS_EQUAL(static_cast<int32_t>(TapCheck::TOO_CLOSE),
	            static_cast<int32_t>(checkPoint({-50.f, -49.f}, {-46.f, -45.f}, &soft, &hard)));
}
