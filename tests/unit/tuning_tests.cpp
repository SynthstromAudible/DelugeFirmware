#include <CppUTest/TestHarness.h>

#define IN_TUNING_TESTS 1
#include "storage/ScalaReader.h"
#undef IN_TUNING_TESTS

#include "midi_engine_mocks.h"
#include "model/tuning/tuning.h"
#include "model/tuning/tuning_sysex.h"
#include <cmath>
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

// clang-format off
struct {
	Expectation equaltemp, tweaked, pythagorean;
} expected = {
	.equaltemp = {
		.freq = {
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
		.ival = {
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
	.tweaked = {
		.freq = {
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
		.ival = {
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
		.freq = {
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
		.ival = {
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
	},
};

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
};

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
};

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
};

double phaseIncrementToFrequency(int phaseIncrement) {
	return phaseIncrement * 44100.0 / (double)0x1.p32;
}

void check_note_frequency(int noteCode, double expected) {
	auto nwo = tuning->noteWithinOctave(noteCode);
	auto phaseIncrement = tuning->noteFrequency(nwo);
	auto freq = phaseIncrementToFrequency(phaseIncrement);
	// less precision is expected for higher frequencies
	auto precision = 0.00001 * std::pow(10, std::log10(expected));
	DOUBLES_EQUAL(expected, freq, precision);
}

TEST(TestTuningSystem, TestMidiFrequencies) {
	TuningSystem::select(0);
	tuning = TuningSystem::tuning;

	double equal_frequencies[] = {
		   8.1758,    8.6620,    9.1770,    9.7227,    10.3009,    10.9134,    11.5623,    12.2499,   12.9783,   13.7500,   14.5676,   15.4339,
		  16.3516,   17.3239,   18.3540,   19.4454,    20.6017,    21.8268,    23.1247,    24.4997,   25.9565,   27.5000,   29.1352,   30.8677,
		  32.7032,   34.6478,   36.7081,   38.8909,    41.2034,    43.6535,    46.2493,    48.9994,   51.9131,   55.0000,   58.2705,   61.7354,
		  65.4064,   69.2957,   73.4162,   77.7817,    82.4069,    87.3071,    92.4986,    97.9989,  103.8262,  110.0000,  116.5409,  123.4708,
		 130.8128,  138.5913,  146.8324,  155.5635,   164.8138,   174.6141,   184.9972,   195.9977,  207.6523,  220.0000,  233.0819,  246.9417,
		 261.6256,  277.1826,  293.6648,  311.1270,   329.6276,   349.2282,   369.9944,   391.9954,  415.3047,  440.0000,  466.1638,  493.8833,
		 523.2511,  554.3653,  587.3295,  622.2540,   659.2551,   698.4565,   739.9888,   783.9909,  830.6094,  880.0000,  932.3275,  987.7666,
		1046.5023, 1108.7305, 1174.6591, 1244.5079,  1318.5102,  1396.9129,  1479.9777,  1567.9817, 1661.2188, 1760.0000, 1864.6550, 1975.5332,
		2093.0045, 2217.4610, 2349.3181, 2489.0159,  2637.0205,  2793.8259,  2959.9554,  3135.9635, 3322.4376, 3520.0000, 3729.3101, 3951.0664,
		4186.0090, 4434.9221, 4698.6363, 4978.0317,  5274.0409,  5587.6517,  5919.9108,  6271.9270, 6644.8752, 7040.0000, 7458.6202, 7902.1328,
		8372.0181, 8869.8442, 9397.2726, 9956.0635, 10548.0818, 11175.3034, 11839.8215, 12543.8540,
	};
	for (int i = 0; i < 128; i++) {
		check_note_frequency(i, equal_frequencies[i]);
	}

	TuningSystem::select(1);
	tuning = TuningSystem::tuning;

	// check frequencies against a variation of just intonation
	int justs[] = { 0, 1173, 391, 1564, -1369, -196, -978, 196, 1369, -1564, 1760, -1173 };
	for (int i = 0; i < 12; i++) {
		tuning->setOffset((i + 9) % 12, justs[i]);
	}
	double just_frequencies[] = {
		   8.2500,    8.5938,    9.1667,    9.6680,    10.3125,    11.0000,    11.4583,    12.3750,   12.8906,   13.7500,   14.6667,   15.4688,
		  16.5000,   17.1875,   18.3333,   19.3359,    20.6250,    22.0000,    22.9167,    24.7500,   25.7813,   27.5000,   29.3333,   30.9375,
		  33.0000,   34.3750,   36.6667,   38.6719,    41.2500,    44.0000,    45.8333,    49.5000,   51.5625,   55.0000,   58.6667,   61.8750,
		  66.0000,   68.7500,   73.3333,   77.3438,    82.5000,    88.0000,    91.6667,    99.0000,  103.1250,  110.0000,  117.3333,  123.7500,
		 132.0000,  137.5000,  146.6667,  154.6875,   165.0000,   176.0000,   183.3333,   198.0000,  206.2500,  220.0000,  234.6667,  247.5000,
		 264.0000,  275.0000,  293.3333,  309.3750,   330.0000,   352.0000,   366.6667,   396.0000,  412.5000,  440.0000,  469.3333,  495.0000,
		 528.0000,  550.0000,  586.6667,  618.7500,   660.0000,   704.0000,   733.3333,   792.0000,  825.0000,  880.0000,  938.6667,  990.0000,
		1056.0000, 1100.0000, 1173.3333, 1237.5000,  1320.0000,  1408.0000,  1466.6667,  1584.0000, 1650.0000, 1760.0000, 1877.3333, 1980.0000,
		2112.0000, 2200.0000, 2346.6667, 2475.0000,  2640.0000,  2816.0000,  2933.3333,  3168.0000, 3300.0000, 3520.0000, 3754.6667, 3960.0000,
		4224.0000, 4400.0000, 4693.3333, 4950.0000,  5280.0000,  5632.0000,  5866.6667,  6336.0000, 6600.0000, 7040.0000, 7509.3333, 7920.0000,
		8448.0000, 8800.0000, 9386.6667, 9900.0000, 10560.0000, 11264.0000, 11733.3333, 12672.0000,
	};
	for (int i = 0; i < 128; i++) {
		check_note_frequency(i, just_frequencies[i]);
	}
};
