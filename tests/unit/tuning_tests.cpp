#include <CppUTest/TestHarness.h>

#define IN_TUNING_TESTS 1
#include "storage/ScalaReader.h"
#undef IN_TUNING_TESTS

#include "midi_engine_mocks.h"
#include "model/tuning/tuning.h"
#include "model/tuning/tuning_sysex.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "tuning_tests.h"

typedef struct {
	int32_t freq[12];
	int32_t ival[12];
	int32_t offsets[12];
} Expectation;

struct {
	Expectation equaltemp, tweaked, pythagorean;
} expected = {.equaltemp =
                  {
                      .freq =
                          {
                              1027294024, // E
                              1088380105, // F
                              1153098554, // F#
                              1221665363, // G
                              1294309365, // G#
                              1371273005, // A = 440 Hz
                              1452813141, // A#
                              1539201906, // B
                              1630727614, // C
                              1727695724, // C#
                              1830429858, // D
                              1939272882, // D#
                          },
                      .ival =
                          {
                              1073741824, // 2^(0/12) -> 0x40000000
                              1137589835, // 2^(1/12)
                              1205234447, // 2^(2/12)
                              1276901417, // 2^(3/12)
                              1352829926, // 2^(4/12)
                              1433273380, // 2^(5/12)
                              1518500250, // 2^(6/12)
                              1608794974, // 2^(7/12)
                              1704458901, // 2^(8/12)
                              1805811301, // 2^(9/12)
                              1913190429, // 2^(10/12)
                              2026954652, // 2^(11/12)
                          },
                      .offsets = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                  },
              .tweaked =
                  {
                      .freq =
                          {
                              1027294024, //
                              1088380105, //
                              1153098554, //
                              1221665363, //
                              1294309365, //
                              1371273005, //
                              1452813141, //
                              1539201906, //
                              1630727614, //
                              1727695724, //
                              1830429858, //
                              1996097915, // quarter-tone E half-flat
                          },
                      .ival =
                          {
                              1073741824, //
                              1137589835, //
                              1205234447, //
                              1314317484, // 2^(3.5/12)
                              1352829926, //
                              1433273380, //
                              1518500250, //
                              1608794974, //
                              1704458901, //
                              1805811301, //
                              1913190429, //
                              2026954652, //
                          },
                      .offsets = {0, 0, 0, 5000, 0, 0, 0, 0, 0, 0, 0, 0},
                  },
              .pythagorean = {
                  // this needs to be validated by a listening test
                  .freq =
                      {
                          1031944816,
                          1087148604,
                          1160937917,
                          1223049242,
                          1288476142,
                          1375930396,
                          1449535659,
                          1547921694,
                          1630727614,
                          1717963229,
                          1834568564,
                          1932708631,
                      },

                  .ival =
                      {
                          1073741824,
                          1131181538,
                          1207959551,
                          1272579229,
                          1358954493,
                          1431651631,
                          1528823803,
                          1610617387,
                          1696777207,
                          1811944558,
                          1908874356,
                          2038437626,
                      },
                  .offsets = {0, -978, 391, -587, 782, -196, 1173, 196, -782, 587, -391, 978},
              }};

double stringToDouble(char const* __restrict__ mem) {
	return atof(mem);
}
double stringToInt(char const* __restrict__ mem) {
	return atoi(mem);
}
void intToString(int32_t number, char* __restrict__ buffer, int32_t minNumDigits) {
	auto x = std::to_string(number);
	strcpy(buffer, x.c_str());
}
uint32_t memToUIntOrError(char const* mem, char const* end) {
	char buf[128];
	memset(buf, 0, sizeof(buf));
	memcpy(buf, mem, (end - mem));
	auto i = atoi(buf);
	return (i > 0) ? i : -1;
}

Tuning* tuning;

TEST_GROUP(TestTuningSystem){

    void setup(){TuningSystem::initialize();

// overwrite tuning 1 with garbage
TuningSystem::selectForWrite(1);
tuning = TuningSystem::tuning;
for (int i = 0; i < 12; i++) {
	tuning->setOffset(i, -999 + i);
}
TuningSystem::select(0);
}
}
;

void check_expectation(Expectation& expected) {
	printf("\nDeg\tOffset\t\tfrequency\tinterval\n");
	for (int i = 0; i < 12; i++) {
		int32_t o = tuning->offsets[i];
		int32_t freq = tuning->noteFrequency(i);
		int32_t ival = tuning->noteInterval(i);
		printf("%d:\t%d\t:\t%d\t%d\n", i, o, freq, ival);
	}
	for (int i = 0; i < 12; i++) {
		int32_t o = tuning->offsets[i];
		int32_t freq = tuning->noteFrequency(i);
		int32_t ival = tuning->noteInterval(i);
		CHECK_EQUAL(expected.offsets[i], o);
		CHECK_EQUAL(expected.freq[i], freq);
		CHECK_EQUAL(expected.ival[i], ival);
	}
}

TEST(TestTuningSystem, FirstTest) {

	tuning = TuningSystem::tuning;
	printf("equaltemp:\n");
	check_expectation(expected.equaltemp);

	const unsigned int umax = 0x80000000u;
	tuning->setReference(4598);
	int64_t freq = ((uint64_t)tuning->noteFrequency(0)) << 1;
	int64_t ival = ((uint64_t)tuning->noteInterval(0)) << 1;
	CHECK_FALSE(freq > umax);
	CHECK_FALSE(ival > umax);

	// if( freq > umax || ival > umax ) {
	//     printf("XX freq,ival = %u,%u > umax\n", freq,ival);
	//     exit(1);
	// }
};

TEST(TestTuningSystem, TestStringToDouble) {

	CHECK_EQUAL(123.45, stringToDouble("123.45"));
	CHECK_EQUAL(-123.45, stringToDouble("-123.45"));
	CHECK_EQUAL(1.0, stringToDouble("1.0"));

	// if (!isnan(stringToDouble("123.45c"))) {
	//     fail("123.45c != NAN");
	// }
	printf("\n");
};

TEST(TestTuningSystem, TestBanks) {

	TuningSystem::select(0);
	tuning = TuningSystem::tuning;

	CHECK_EQUAL(4400, tuning->getReference());
	for (int i = 0; i < 12; i++) {
		int32_t freq = tuning->noteFrequency(i);
		int32_t ival = tuning->noteInterval(i);
		CHECK_EQUAL(expected.equaltemp.freq[i], freq);
		CHECK_EQUAL(expected.equaltemp.ival[i], ival);
	}

	TuningSystem::select(1);
	tuning = TuningSystem::tuning;
	// TODO check applied tuning
};

TEST(TestTuningSystem, TestSysex) {
	// tuning->setOffset(3, 2);

	uint8_t msg[] = {0xF0, 0x7E, 0x7F, 0x08, 0x00, 0x00, 0xF7};
	uint8_t exp[] = {
	    0xF0, 0x7E, 0x00, 0x08, 0x01, 0x00, // sysex non-rt device=0 tuning bulkdump preset=0
	    'T',  'W',  'E',  'L',  'V',  'E',  ' ',  'T',  'O',  'N',  'E',  ' ',  'E',  'D',  'O',  0x00, // name
	    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00, 0x00, 0x04, 0x00, 0x00, 0x05, 0x00, 0x00, //
	    0x06, 0x00, 0x00, 0x07, 0x00, 0x00, 0x08, 0x00, 0x00, 0x09, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x0B, 0x00, 0x00, //
	};
	MIDICable cable;
	memset(cable.buffer, '\0', 1024);
	TuningSysex::sysexReceived(cable, msg, sizeof(msg));
	MEMCMP_EQUAL(exp, cable.buffer, sizeof(exp));
}

void check_offsets(int32_t* ex, int32_t* ac, int32_t num) {
	char fixme[128];
	for (int i = 0; i < 12; i++) {
		auto o = ac[i];
		auto e = ex[i];
		sprintf(fixme, "actual[%d] = %d, expected[%d] = %d\n", i, ac[i], i, ex[i]);
		CHECK_EQUAL_TEXT(ex[i], ac[i], fixme);
	}
}

TEST(TestTuningSystem, TestScala) {
	TuningSystem::selectForWrite(1);
	tuning = TuningSystem::tuning;
	ScalaReader reader;

	reader.fileClusterBuffer = scale_12tet;
	reader.fileSize = sizeof(scale_12tet);
	reader.openScalaFile(NULL, "12TET");
	printf("equaltemp:\n");
	check_expectation(expected.equaltemp);

	reader.fileClusterBuffer = scale_tweaked;
	reader.fileSize = sizeof(scale_tweaked);
	reader.openScalaFile(NULL, "TWEAKED");
	printf("tweaked:\n");
	check_expectation(expected.tweaked);

	reader.fileClusterBuffer = scale_pythagorean;
	reader.fileSize = sizeof(scale_pythagorean);
	reader.openScalaFile(NULL, "PYTHAGOREAN");
	printf("pythagorean:\n");
	check_expectation(expected.pythagorean);
}
