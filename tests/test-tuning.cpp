
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include "TuningSystem.h"
#include "TuningSystem.cpp"

struct {
	int freq[12];
	int ival[12];
} expected = {
	.freq = {
		1027294024, 1088380105, 1153098554, 1221665363, 1294309365, 1371273005,
		1452813141, 1539201906, 1630727614, 1727695724, 1830429858, 1939272882,
	},
	.ival = {
		1073741824, 1137589835, 1205234447, 1276901417, 1352829926, 1433273380,
		1518500250, 1608794974, 1704458901, 1805811301, 1913190429, 2026954652,
	},
};

void fail(const char *str = "") {
	printf(" XX  %s");
	exit(1);
}

template <typename T> void assert_equal(T exp, T act) {
	if( exp != act )
	{
		printf(" XX  Got %f, expected %f\n", act, exp);
		exit(1);
	}
	else
	{
		printf(" OK");
	}
}

double stringToDouble(char const *__restrict__ mem) {
	return atof(mem);
}

int main(int argc, char** argv) {
	auto tuningSystem = new TuningSystem();

	printf("Check TuningSystem\nDeg\tOffset\t\tfrequency\tinterval\n");
	for(int i=0; i < 12; i++) {
		int32_t o = tuningSystem->offsets[i];
		int32_t freq = tuningFrequencyTable[i];
		int32_t ival = tuningIntervalTable[i];
		printf("%d:\t%d\t:\t%d\t%d\t", i, o, freq, ival);

		assert_equal( expected.freq[i], freq );
		assert_equal( expected.ival[i], ival );
		printf("\n");
	}

	const unsigned int umax = 0x80000000u;
	tuningSystem->setReference(4598);
	int64_t freq = ((uint64_t) tuningFrequencyTable[0]) << 1;
	int64_t ival = ((uint64_t) tuningIntervalTable[0]) << 1;
	if( freq > umax || ival > umax ) {
		printf("XX freq,ival = %u,%u > umax\n", freq,ival);
		exit(1);
	}
	

	printf("Check stringToDouble");
	assert_equal(123.45, stringToDouble("123.45"));
	assert_equal(-123.45, stringToDouble("-123.45"));
	assert_equal(1.0, stringToDouble("1.0"));
	/*
	if (!isnan(stringToDouble("123.45c"))) {
		fail("123.45c != NAN");
	}
	*/
	printf("\n");
}


