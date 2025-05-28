#include "model/tuning/tuning.h"
#include <CppUTest/TestHarness.h>

struct {
	int32_t freq[12];
	int32_t ival[12];
} expected = {
    .freq =
        {
            1027294024,
            1088380105,
            1153098554,
            1221665363,
            1294309365,
            1371273005,
            1452813141,
            1539201906,
            1630727614,
            1727695724,
            1830429858,
            1939272882,
        },
    .ival =
        {
            1073741824,
            1137589835,
            1205234447,
            1276901417,
            1352829926,
            1433273380,
            1518500250,
            1608794974,
            1704458901,
            1805811301,
            1913190429,
            2026954652,
        },
};

double stringToDouble(char const* __restrict__ mem) {
	return atof(mem);
}

TEST_GROUP(TestTuningSystem){};

TEST(TestTuningSystem, FirstTest) {
	TuningSystem::initialize();

	printf("\nDeg\tOffset\t\tfrequency\tinterval\n");
	for (int i = 0; i < 12; i++) {
		int32_t o = TuningSystem::tuning->offsets[i];
		int32_t freq = TuningSystem::tuning->noteFrequency(i);
		int32_t ival = TuningSystem::tuning->noteInterval(i);
		printf("%d:\t%d\t:\t%d\t%d\n", i, o, freq, ival);

		CHECK_EQUAL(expected.freq[i], freq);
		CHECK_EQUAL(expected.ival[i], ival);
	}

	const unsigned int umax = 0x80000000u;
	TuningSystem::tuning->setReference(4598);
	int64_t freq = ((uint64_t)TuningSystem::tuning->noteFrequency(0)) << 1;
	int64_t ival = ((uint64_t)TuningSystem::tuning->noteInterval(0)) << 1;
	CHECK_FALSE(freq > umax);
	CHECK_FALSE(ival > umax);

	/*
	if( freq > umax || ival > umax ) {
	    printf("XX freq,ival = %u,%u > umax\n", freq,ival);
	    exit(1);
	}
	*/
};

TEST(TestTuningSystem, TestStringToDouble) {
	CHECK_EQUAL(123.45, stringToDouble("123.45"));
	CHECK_EQUAL(-123.45, stringToDouble("-123.45"));
	CHECK_EQUAL(1.0, stringToDouble("1.0"));
	/*
	if (!isnan(stringToDouble("123.45c"))) {
	    fail("123.45c != NAN");
	}
	*/
	printf("\n");
};

TEST(TestTuningSystem, TestBanks) {
	TuningSystem::initialize();

	TuningSystem::select(0);
	CHECK_EQUAL(4400, TuningSystem::tuning->getReference());
	for (int i = 0; i < 12; i++) {
		int32_t freq = TuningSystem::tuning->noteFrequency(i);
		int32_t ival = TuningSystem::tuning->noteInterval(i);
		CHECK_EQUAL(expected.freq[i], freq);
		CHECK_EQUAL(expected.ival[i], ival);
	}

	TuningSystem::select(1);
	// TODO check applied tuning
};
