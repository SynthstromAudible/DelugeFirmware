/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

// StateVariableFilter: the state-variable filter in band and notch modes, mono and stereo, with its dry/wet fade. Exact
// reference values pin the arithmetic, each case from a filter zeroed as the filter set zeroes it and with the
// synth-wide noise generator seeded the same.

#include "dsp-next/fixed/filter/state_variable_filter.h"
#include "util/waves.h" // jcong, CONG

#include <cppspec.hpp>
#include <cstdint>
#include <tuple>
#include <vector>

using namespace deluge::dsp::fixed;

namespace {

constexpr uint32_t kSeed = 123456789;
constexpr int32_t kGain = 536870912;

/// Two sines (3 and 11 cycles per 64 samples, at 2^25 and 2^24): a mono block, or 32 interleaved L/R frames.
const std::vector<int32_t> kInput{
    0,         24536521,  32591589,  24293472,  15500126,  20423895,  36182765,  45647162,  35589850,  10947281,
    -9908702,  -13932263, -6420363,  -4590277,  -18578569, -40018314, -50331648, -40018314, -18578569, -4590277,
    -6420363,  -13932263, -9908702,  10947281,  35589850,  45647162,  36182765,  20423895,  15500126,  24293472,
    32591589,  24536521,  0,         -24536521, -32591589, -24293472, -15500126, -20423895, -36182765, -45647162,
    -35589850, -10947281, 9908702,   13932263,  6420363,   4590277,   18578569,  40018314,  50331648,  40018314,
    18578569,  4590277,   6420363,   13932263,  9908702,   -10947281, -35589850, -45647162, -36182765, -20423895,
    -15500126, -24293472, -32591589, -24536521};

constexpr FilterConfig kDark{
    .frequency = 1 << 22, .resonance = 0, .morph = 0, .gain = kGain, .allow_oversampling = true};
constexpr FilterConfig kMid{
    .frequency = 1 << 24, .resonance = 1 << 28, .morph = 1 << 27, .gain = kGain, .allow_oversampling = true};
constexpr FilterConfig kBright{
    .frequency = 50000000, .resonance = 500000000, .morph = 1 << 28, .gain = kGain, .allow_oversampling = true};

/// One block's output, the compensated gain, and the noise generator's next value after the block.
struct Rendered {
	std::vector<int32_t> out;
	int32_t gain;
	int32_t next_noise;
};
/// The noise generator's next value when nothing has drawn from it since the seed.
constexpr int32_t kUndrawnNextNoise = 1528111448;

Rendered renderMono(const FilterConfig& config, SvfMode mode) {
	jcong = kSeed;
	StateVariableFilter filter;
	filter.zero();
	const int32_t gain = filter.configure(config, mode);
	std::vector<int32_t> block = kInput;
	std::vector<int32_t> scratch(block.size());
	filter.renderMono(block, scratch);
	return {.out = block, .gain = gain, .next_noise = static_cast<int32_t>(CONG)};
}

Rendered renderStereo(const FilterConfig& config, SvfMode mode) {
	jcong = kSeed;
	StateVariableFilter filter;
	filter.zero();
	const int32_t gain = filter.configure(config, mode);
	std::vector<int32_t> block = kInput;
	std::vector<int32_t> scratch(block.size());
	filter.renderStereo(block, scratch);
	return {.out = block, .gain = gain, .next_noise = static_cast<int32_t>(CONG)};
}

const std::vector<int32_t> kBandDarkMono{
    0,         159570,    1407366,   3895329,   6997560,   10157706,  13493034,  17527101,  22361268,  27208971,
    30821940,  32378142,  31972605,  30234630,  27483639,  23361180,  17356941,  9662181,   1364745,   -6242523,
    -12486657, -17501004, -21632244, -24679578, -25790280, -24172884, -19931649, -14116020, -7898190,  -1771995,
    4426962,   10779495,  16653486,  20872317,  22523373,  21626532,  18985743,  15352026,  10807038,  5025540,
    -1900620,  -9032043,  -15035742, -19040223, -21081330, -21714012, -21188964, -19083729, -14826702, -8563953,
    -1383468,  5375847,   10988007,  15549276,  19380162,  22257885,  23309310,  21724869,  17589579,  11931756,
    5905575,   -8826,     -5987046,  -12118854};
const std::vector<int32_t> kBandDarkStereo{
    0,        159570,   211962,   1353450,  1688502,  3467790,  3847569,  6284358,  7150668,  10256679, 11076567,
    13748988, 13729305, 15680334, 15039246, 16238403, 14566134, 14391630, 11606346, 10664553, 7781604,  6774684,
    3989409,  2903739,  583203,   256785,   -736740,  -36978,   -145179,  907338,   1346016,  2834085,  3824841,
    5215389,  5715099,  6077211,  5771211,  5406843,  4624626,  3480939,  1869300,  -88263,   -1908366, -3586803,
    -4741137, -5857467, -6494955, -7018191, -6668595, -5969757, -4496127, -3166059, -1541298, -267594,  1343184,
    2589252,  3838389,  4229811,  4265751,  3548841,  2824134,  1687815,  547347,   -1074444};
const std::vector<int32_t> kBandMidMono{
    0,         6656979,   18345279,  26343945,  25846788,  20233512,  16895589,  18718020,  20535750,  14658087,
    -315885,   -16725600, -25397787, -24335466, -19492989, -18748002, -23433351, -26925714, -21946419, -8678115,
    4801938,   11004384,  10391934,  10121589,  16321821,  26947689,  33429039,  29555409,  17662512,  6603639,
    2493486,   2822751,   -112050,   -10558356, -23949807, -31357992, -28652904, -20165949, -14422365, -15070959,
    -17178288, -12739650, 354846,    15296448,  23297952,  22355937,  18295836,  18642288,  24294246,  28302117,
    23273850,  9493740,   -4684734,  -11492370, -11232135, -10972245, -16879968, -27064452, -33142386, -29024820,
    -17109387, -6229449,  -2397978,  -2999106};
const std::vector<int32_t> kBandMidStereo{
    0,         6656979,   8841327,   16111512,  16836039,  22165059,  25387566,  30143838,  31790376,  27307920,
    20721657,  8793237,   598749,    -11073819, -20232888, -33005535, -42433467, -48669912, -48731724, -43534698,
    -36371319, -25656786, -14707356, 1442019,   17473140,  33866952,  44351256,  50538039,  52523226,  51998523,
    49009275,  40686126,  27502044,  8638821,   -9520350,  -25337409, -36386412, -45719712, -53193132, -57985611,
    -57538242, -49818282, -36406989, -19294890, -3049203,  12479685,  27036930,  41384094,  52777524,  58957092,
    58646544,  52821792,  43402245,  31286580,  16984098,  -481923,   -19011999, -35747535, -47199615, -53373651,
    -55622397, -55160079, -51674466, -43255641};
const std::vector<int32_t> kBandBrightMono{
    0,         17657892,  15614013,  -18645549, -26916774, 15041502,  41236476,  3388926,   -44059755, -30642816,
    15496572,  23654826,  -7249863,  -20991759, -5656476,  420807,    -7986210,  2779464,   26716683,  17632737,
    -20780208, -26434125, 16907199,  44343333,  14470725,  -26645655, -22225143, 8532309,   15205506,  -1182804,
    -6740523,  -3247725,  -13815942, -23622030, -2960718,  26483013,  15736578,  -24568863, -31854816, 7523517,
    36855483,  19037139,  -10962252, -11599005, 4818504,   8751165,   6104970,   11757588,  9411678,   -14642412,
    -29847315, -6574197,  25636785,  16372914,  -23361123, -35626272, -6690369,  19676007,  13538289,  -3240666,
    -5807907,  -2574798,  -3163479,  5455986};
const std::vector<int32_t> kBandBrightStereo{
    0,         17657892,  23437851,  9668778,   798849,    -18808959, -13050996, 4540995,   2777475,   1631829,
    -19550145, -32135961, -16414224, -7123560,  8937765,   7601934,   -8734818,  -2068959,  3421719,   17641812,
    27072267,  8330796,   1878117,   -932004,   5325846,   23784201,  12611412,  -3465642,  -18054918, -28378185,
    -8974782,  228117,    -2099808,  -7213113,  -24320946, -15413751, 1135287,   7615974,   14216457,  -569190,
    -4508652,  10011099,  18129108,  29966403,  17904558,  -4664778,  -7304184,  -7470321,  7507434,   13753563,
    -5225349,  -15979788, -26144145, -19557735, 52791,     -2563473,  -5917881,  -13208571, -14608905, 8577855,
    18325818,  18901077,  10982973,  -6825702};
const std::vector<int32_t> kNotchDarkMono{
    0,         159570,    1407366,   3895329,   6997560,   10157706,  13493034,  17527101,  22361268,  27208971,
    30821940,  32378142,  31972605,  30234630,  27483639,  23361180,  17356941,  9662181,   1364745,   -6242523,
    -12486657, -17501004, -21632244, -24679578, -25790280, -24172884, -19931649, -14116020, -7898190,  -1771995,
    4426962,   10779495,  16653486,  20872317,  22523373,  21626532,  18985743,  15352026,  10807038,  5025540,
    -1900620,  -9032043,  -15035742, -19040223, -21081330, -21714012, -21188964, -19083729, -14826702, -8563953,
    -1383468,  5375847,   10988007,  15549276,  19380162,  22257885,  23309310,  21724869,  17589579,  11931756,
    5905575,   -8826,     -5987046,  -12118854};
const std::vector<int32_t> kNotchDarkStereo{
    0,        159570,   211962,   1353450,  1688502,  3467790,  3847569,  6284358,  7150668,  10256679, 11076567,
    13748988, 13729305, 15680334, 15039246, 16238403, 14566134, 14391630, 11606346, 10664553, 7781604,  6774684,
    3989409,  2903739,  583203,   256785,   -736740,  -36978,   -145179,  907338,   1346016,  2834085,  3824841,
    5215389,  5715099,  6077211,  5771211,  5406843,  4624626,  3480939,  1869300,  -88263,   -1908366, -3586803,
    -4741137, -5857467, -6494955, -7018191, -6668595, -5969757, -4496127, -3166059, -1541298, -267594,  1343184,
    2589252,  3838389,  4229811,  4265751,  3548841,  2824134,  1687815,  547347,   -1074444};
const std::vector<int32_t> kNotchMidMono{
    0,         7288581,   13433094,  18771918,  23686194,  28132782,  31284666,  31881321,  28975311,  22509726,
    13344006,  2872476,   -7538205,  -17090415, -25177746, -30949551, -33628239, -32953038, -29327817, -23541930,
    -16345164, -8186277,  788178,    10176558,  19030482,  26213775,  30883311,  32783652,  32040870,  28760814,
    22989123,  14889912,  5139276,   -5065710,  -14447550, -22103061, -27764169, -31396758, -32699910, -31226961,
    -26792436, -19799631, -11144121, -1851048,  7279512,   15893340,  23619606,  29637525,  32991684,  33117132,
    30114627,  24579606,  17232378,  8648769,   -833391,   -10632591, -19674147, -26789577, -31202511, -32778297,
    -31763295, -28351308, -22612509, -14672904};
const std::vector<int32_t> kNotchMidStereo{
    0,         7288581,   9681249,   10969644,  9584346,   16376916,  21873549,  29877396,  29106564,  26680476,
    22205016,  20558901,  19893759,  14079816,  4531902,   -10629687, -20929023, -28592400, -30451260, -34365870,
    -38458566, -41422656, -38115780, -26849136, -13000716, 582837,    8947806,   17505504,  26599026,  38501721,
    46808283,  48437136,  42301101,  32407104,  23550399,  15206565,  5818086,   -8561865,  -24464313, -38162580,
    -44384802, -44666784, -42326790, -40227135, -37057491, -28575048, -13957434, 4304766,   19636482,  29550771,
    34870467,  39965916,  45544737,  48728298,  44918586,  33065067,  17355075,  2941848,   -7461465,  -16590390,
    -27230337, -39477318, -48675429, -50505099};
const std::vector<int32_t> kNotchBrightMono{
    0,        9816225,   13011654,  9736779,   6280614,   8206119,   14417532,  18226854,  14323521, 4494183,  -3956604,
    -5648031, -2595669,  -1793334,  -7409304,  -16039185, -20163279, -16024320, -7482813,  -1903929, -2568678, -5511348,
    -3953964, 4298763,   14180862,  18317688,  14574054,  8199447,   6171303,   9717363,   13077858, 9845361,  15909,
    -9784503, -13022463, -9769680,  -6275097,  -8172969,  -14417520, -18258936, -14329359, -4464741, 3967872,  5621508,
    2580306,  1816902,   7428153,   16018866,  20141460,  16041123,  7506792,   1891197,   2543130,  5519775,  3980400,
    -4302519, -14206785, -18318948, -14549163, -8194044,  -6194874,  -9726378,  -13055889, -9833028};
const std::vector<int32_t> kNotchBrightStereo{
    0,        9816225,   13038810,  9691815,   6165090,   8197902,   14529834,  18327846,  14278956, 4360977,  -3965328,
    -5546001, -2513790,  -1772775,  -7439214,  -16054968, -20185788, -16046364, -7422045,  -1847988, -2616324, -5629359,
    -4018767, 4387002,   14272581,  18272814,  14471832,  8140824,   6210852,   9788724,   13112442, 9878121,  2454,
    -9844929, -13034019, -9699228,  -6166788,  -8170686,  -14534805, -18316413, -14278032, -4386051, 3970395,  5530848,
    2513652,  1794951,   7434198,   16073610,  20185158,  16027347,  7426902,   1826625,   2617725,  5644707,  4014222,
    -4363380, -14274720, -18284223, -14467668, -8165763,  -6208086,  -9781656,  -13116075, -9852516};
const std::vector<int32_t> kBandMidInvertedMono{
    0,         18622143,  23303796,  9240474,   -8935758,  -13648659, -2996805,  7480050,   2006682,   -17704335,
    -33297018, -28656681, -7427538,  10929024,  11181765,  -2767218,  -12185148, -3574617,  16820535,  29508513,
    22316688,  3569892,   -7104360,  443427,    16358895,  21014976,  6000219,   -16650642, -26907075, -17238318,
    -510354,   4091244,   -8572710,  -24268839, -24430917, -6241764,  14276025,  19091511,  6588171,   -6723741,
    -3929247,  14181711,  29677026,  26264337,  6810318,   -9942567,  -9204021,  4888041,   13674327,  3997167,
    -17454342, -30816159, -23723304, -4577985,  6756270,   -117039,   -15574011, -20120814, -5328162,  16909470,
    26725851,  16748493,  -64905,    -4539798};
const std::vector<int32_t> kBandMidInvertedStereo{
    0,         18622143,  24734952,  17016351,  9878250,   6797634,   16872174,  16557111,  7457268,   -18231198,
    -37377864, -45145149, -39780441, -33938160, -38278644, -43429548, -42561933, -21566955, 5279634,   29879904,
    38217837,  40165119,  44667537,  57285999,  69712296,  66467481,  47278149,  18815925,  -536538,   -10568154,
    -16531461, -32389152, -56211966, -76591059, -79820169, -64464912, -44618430, -31089480, -26211525, -16637373,
    4963752,   36687843,  62804733,  72407601,  66826146,  58356441,  56899470,  57668931,  49512204,  24890802,
    -7710921,  -34498404, -46411752, -49428819, -54764862, -67019244, -77527797, -73024407, -50970660, -21617184,
    85398,     10618803,  18284949,  34519089};
const std::vector<int32_t> kBandFreshMono{
    0,         24357724,  32308088,  24354372,  15907826,  20414564,  35054082,  43817738,  34426840,  11268200,
    -8991458,  -14224620, -8576486,  -7008650,  -18698590, -37041712, -46336114, -37962032, -19135902, -5300872,
    -4376862,  -9187516,  -5881676,  10776868,  31460290,  41492538,  35549526,  22594066,  16030518,  19820988,
    24757060,  18723816,  -30816,    -20590912, -30090252, -26388474, -19493138, -20343790, -29275026, -35732186,
    -29495102, -11552598, 6618918,   14410952,  12452218,  11053682,  18473908,  31970782,  40366876,  35462086,
    20433176,  6556780,   1900216,   3432826,   1054210,   -10957882, -27537140, -37543340, -34839728, -24271232,
    -16228870, -16014460, -18589852, -14433440};
const std::vector<int32_t> kBandFadeMono{
    0,         24357724,  32308088,  24354372,  15907826,  20414564,  35054082,  43817738,  34426840,  11268200,
    -8991458,  -14224620, -8576486,  -7008650,  -18698590, -37041712, -46336114, -37962032, -19135902, -5300872,
    -4376862,  -9187516,  -5881676,  10776868,  31460290,  41492538,  35549526,  22594066,  16030518,  19820988,
    24757060,  18723816,  -30816,    -20590912, -30090252, -26388474, -19493138, -20343790, -29275026, -35732186,
    -29495102, -11552598, 6618918,   14410952,  12452218,  11053682,  18473908,  31970782,  40366876,  35462086,
    20433176,  6556780,   1900216,   3432826,   1054210,   -10957882, -27537140, -37543340, -34839728, -24271232,
    -16228870, -16014460, -18589852, -14433440};
const std::vector<int32_t> kBandFadeStereo{
    0,         24357724,  32118960,  24050458,  15552766,  20509228,  35551032,  44593948,  35296320,  12362194,
    -6979890,  -11553774, -5622886,  -5384376,  -18795706, -39036936, -49158436, -41377106, -23568502, -11360040,
    -11874226, -16163114, -10860610, 8985530,   31707042,  43029850,  38061164,  27580742,  24581178,  31298082,
    36865106,  28859716,  7563604,   -15172132, -25913692, -24603056, -21840928, -28279536, -41582630, -49648164,
    -42855390, -24074722, -6039682,  2272940,   3036050,   7460558,   21709654,  40532494,  51267708,  47383278,
    34405174,  23933234,  21473226,  21098956,  12872114,  -6503218,  -28454764, -41329988, -41049282, -35162868,
    -33669230, -38440100, -41440928, -33317558};
const std::vector<int32_t> kBandResetMono{
    0,         6656979,   18345279,  26343945,  25846788,  20233512,  16895589,  18718020,  20535750,  14658087,
    -315885,   -16725600, -25397787, -24335466, -19492989, -18748002, -23433351, -26925714, -21946419, -8678115,
    4801938,   11004384,  10391934,  10121589,  16321821,  26947689,  33429039,  29555409,  17662512,  6603639,
    2493486,   2822751,   -112050,   -10558356, -23949807, -31357992, -28652904, -20165949, -14422365, -15070959,
    -17178288, -12739650, 354846,    15296448,  23297952,  22355937,  18295836,  18642288,  24294246,  28302117,
    23273850,  9493740,   -4684734,  -11492370, -11232135, -10972245, -16879968, -27064452, -33142386, -29024820,
    -17109387, -6229449,  -2397978,  -2999106};
const std::vector<int32_t> kBandMidFadeResetMono{
    0,         15960428,  25684080,  25298232,  20622904,  20328672,  26439626,  31910342,  27836856,  12876374,
    -4875736,  -15411094, -16556572, -15228552, -19075458, -28363110, -35470962, -32726364, -20469222, -6903050,
    -22408,    391594,    1838582,   10466000,  24278544,  34592408,  34543558,  25896550,  16804740,  13550630,
    14195142,  11180294,  -69354,    -15831432, -27177204, -28746032, -23838564, -20259422, -22228870, -25930426,
    -23651962, -12115740, 3647218,   14831032,  17597472,  16415512,  18389430,  25647694,  32741962,  32065370,
    21780800,  7950084,   -1223708,  -3647742,  -4774478,  -10964696, -22481342, -32572120, -34034502, -26526352,
    -16646592, -11372418, -10908372, -9008952};

/// The mid setting with its morph inverted as the high-pass slot passes it.
constexpr FilterConfig kMidInverted{.frequency = 1 << 24,
                                    .resonance = 1 << 28,
                                    .morph = ((1 << 29) - 1) - (1 << 27),
                                    .gain = kGain,
                                    .allow_oversampling = true};

} // namespace

// clang-format off
describe state_variable_filter("StateVariableFilter", ${
	it("renders band mode, dark, in mono exactly, drawing no noise", _ {
		const Rendered r = renderMono(kDark, SvfMode::BAND);
		expect(r.out).to_equal(kBandDarkMono);
		expect(r.gain).to_equal(536870912);
		expect(r.next_noise).to_equal(kUndrawnNextNoise);
	});
	it("renders band mode, dark, in stereo exactly, drawing no noise", _ {
		const Rendered r = renderStereo(kDark, SvfMode::BAND);
		expect(r.out).to_equal(kBandDarkStereo);
		expect(r.gain).to_equal(536870912);
		expect(r.next_noise).to_equal(kUndrawnNextNoise);
	});
	it("renders band mode, mid, in mono exactly, drawing no noise", _ {
		const Rendered r = renderMono(kMid, SvfMode::BAND);
		expect(r.out).to_equal(kBandMidMono);
		expect(r.gain).to_equal(536870912);
		expect(r.next_noise).to_equal(kUndrawnNextNoise);
	});
	it("renders band mode, mid, in stereo exactly, drawing no noise", _ {
		const Rendered r = renderStereo(kMid, SvfMode::BAND);
		expect(r.out).to_equal(kBandMidStereo);
		expect(r.gain).to_equal(536870912);
		expect(r.next_noise).to_equal(kUndrawnNextNoise);
	});
	it("renders band mode, bright, in mono exactly, drawing no noise", _ {
		const Rendered r = renderMono(kBright, SvfMode::BAND);
		expect(r.out).to_equal(kBandBrightMono);
		expect(r.gain).to_equal(536870912);
		expect(r.next_noise).to_equal(kUndrawnNextNoise);
	});
	it("renders band mode, bright, in stereo exactly, drawing no noise", _ {
		const Rendered r = renderStereo(kBright, SvfMode::BAND);
		expect(r.out).to_equal(kBandBrightStereo);
		expect(r.gain).to_equal(536870912);
		expect(r.next_noise).to_equal(kUndrawnNextNoise);
	});
	it("renders notch mode, dark, in mono exactly, drawing no noise", _ {
		const Rendered r = renderMono(kDark, SvfMode::NOTCH);
		expect(r.out).to_equal(kNotchDarkMono);
		expect(r.gain).to_equal(536870912);
		expect(r.next_noise).to_equal(kUndrawnNextNoise);
	});
	it("renders notch mode, dark, in stereo exactly, drawing no noise", _ {
		const Rendered r = renderStereo(kDark, SvfMode::NOTCH);
		expect(r.out).to_equal(kNotchDarkStereo);
		expect(r.gain).to_equal(536870912);
		expect(r.next_noise).to_equal(kUndrawnNextNoise);
	});
	it("renders notch mode, mid, in mono exactly, drawing no noise", _ {
		const Rendered r = renderMono(kMid, SvfMode::NOTCH);
		expect(r.out).to_equal(kNotchMidMono);
		expect(r.gain).to_equal(536870912);
		expect(r.next_noise).to_equal(kUndrawnNextNoise);
	});
	it("renders notch mode, mid, in stereo exactly, drawing no noise", _ {
		const Rendered r = renderStereo(kMid, SvfMode::NOTCH);
		expect(r.out).to_equal(kNotchMidStereo);
		expect(r.gain).to_equal(536870912);
		expect(r.next_noise).to_equal(kUndrawnNextNoise);
	});
	it("renders notch mode, bright, in mono exactly, drawing no noise", _ {
		const Rendered r = renderMono(kBright, SvfMode::NOTCH);
		expect(r.out).to_equal(kNotchBrightMono);
		expect(r.gain).to_equal(536870912);
		expect(r.next_noise).to_equal(kUndrawnNextNoise);
	});
	it("renders notch mode, bright, in stereo exactly, drawing no noise", _ {
		const Rendered r = renderStereo(kBright, SvfMode::NOTCH);
		expect(r.out).to_equal(kNotchBrightStereo);
		expect(r.gain).to_equal(536870912);
		expect(r.next_noise).to_equal(kUndrawnNextNoise);
	});
	it("renders band mode, mid, with the high-pass slot's inverted morph, in mono exactly, drawing no noise", _ {
		const Rendered r = renderMono(kMidInverted, SvfMode::BAND);
		expect(r.out).to_equal(kBandMidInvertedMono);
		expect(r.gain).to_equal(536870912);
		expect(r.next_noise).to_equal(kUndrawnNextNoise);
	});
	it("renders band mode, mid, with the high-pass slot's inverted morph, in stereo exactly, drawing no noise", _ {
		const Rendered r = renderStereo(kMidInverted, SvfMode::BAND);
		expect(r.out).to_equal(kBandMidInvertedStereo);
		expect(r.gain).to_equal(536870912);
		expect(r.next_noise).to_equal(kUndrawnNextNoise);
	});
	it("fades in from dry when fresh (its initializers start a fade)", _ {
		jcong = kSeed;
		StateVariableFilter filter;
		std::ignore = filter.configure(kMid, SvfMode::BAND);
		std::vector<int32_t> block = kInput;
		std::vector<int32_t> scratch(block.size());
		filter.renderMono(block, scratch);
		expect(block).to_equal(kBandFreshMono);
	});
	it("fades in after reset(true) in mono", _ {
		jcong = kSeed;
		StateVariableFilter filter;
		filter.zero();
		std::ignore = filter.configure(kMid, SvfMode::BAND);
		std::vector<int32_t> block = kInput;
		std::vector<int32_t> scratch(block.size());
		filter.renderMono(block, scratch);
		filter.reset(true);
		block = kInput;
		filter.renderMono(block, scratch);
		expect(block).to_equal(kBandFadeMono);
	});
	it("fades in after reset(true) in stereo, each channel's sample stepping the fade", _ {
		jcong = kSeed;
		StateVariableFilter filter;
		filter.zero();
		std::ignore = filter.configure(kMid, SvfMode::BAND);
		std::vector<int32_t> block = kInput;
		std::vector<int32_t> scratch(block.size());
		filter.renderStereo(block, scratch);
		filter.reset(true);
		block = kInput;
		filter.renderStereo(block, scratch);
		expect(block).to_equal(kBandFadeStereo);
	});
	it("clears its memories without a fade after reset(false)", _ {
		jcong = kSeed;
		StateVariableFilter filter;
		filter.zero();
		std::ignore = filter.configure(kMid, SvfMode::BAND);
		std::vector<int32_t> block = kInput;
		std::vector<int32_t> scratch(block.size());
		filter.renderMono(block, scratch);
		filter.reset(false);
		block = kInput;
		filter.renderMono(block, scratch);
		expect(block).to_equal(kBandResetMono);
	});
	it("keeps fading through reset(false) when a fade is under way", _ {
		jcong = kSeed;
		StateVariableFilter filter;
		filter.zero();
		std::ignore = filter.configure(kMid, SvfMode::BAND);
		filter.reset(true);
		std::vector<int32_t> block = kInput;
		std::vector<int32_t> scratch(block.size());
		filter.renderMono(block, scratch);
		filter.reset(false);
		block = kInput;
		filter.renderMono(block, scratch);
		expect(block).to_equal(kBandMidFadeResetMono);
	});
});
// clang-format on

CPPSPEC_MAIN(state_variable_filter);
