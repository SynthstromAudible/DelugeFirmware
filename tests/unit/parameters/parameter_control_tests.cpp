#include "CppUTest/TestHarness.h"
#include "modulation/params/param_control.h"

using namespace deluge::modulation::params;
TEST_GROUP(ParameterControlTests){};

TEST(ParameterControlTests, ExpressionReadsAndWritesRemainLiveWithOrWithoutTrackLink) {
	for (bool linked : {false, true}) {
		for (bool scalar : {false, true}) {
			CHECK(controlLayer(Kind::EXPRESSION, true, scalar, linked) == ControlLayer::LIVE);
		}
		CHECK(controlLayer(Kind::EXPRESSION, true, true, linked, true) == ControlLayer::REGION);
	}
}
