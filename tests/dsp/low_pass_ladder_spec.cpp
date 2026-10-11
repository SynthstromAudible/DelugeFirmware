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

// LowPassLadder: the transistor ladder in its three modes, mono and stereo, with its dry/wet fade and its draws from
// the shared noise generator. Exact reference values pin the arithmetic, each case from a filter zeroed as the filter
// set zeroes it and with the synth-wide noise generator seeded the same.
//
// LowPassLadderKernel: the same ladder as a processor kernel, `Mono` and `Stereo`, with its noise a policy. Against the
// verbatim reference (reference_filters.h) over every ladder workload (filter_workloads.h): each block's output, the
// gain, the CONG draws (count and end state per block) and the whole state, at block lengths cycling 1–9, 24, 64 and
// 128, and what it renders must equal the record. Every mode change between the four modes carries the state, and a
// seeded stream shows the draws are left before right in every frame.

#include "dsp-next/fixed/filter/low_pass_ladder.h"
#include "dsp-next/fixed/filter/low_pass_ladder_kernel.h"

#include "dsp-next/core/concepts.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo.h"
#include "dsp-next/fixed/noise/cong_noise.h"
#include "dsp-next/fixed/noise/congruential_step.h"
#include "dsp-next/fixed/noise/noise.h"
#include "filter_workload_records.h"
#include "filter_workloads.h"
#include "kernel_filters.h"
#include "reference_filters.h"
#include "util/waves.h" // jcong, CONG

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
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

constexpr FilterConfig kDark{.frequency = 1 << 22, .resonance = 0, .morph = 0, .allow_oversampling = true};
constexpr FilterConfig kMid{.frequency = 1 << 24, .resonance = 1 << 28, .morph = 1 << 27, .allow_oversampling = true};
constexpr FilterConfig kBright{
    .frequency = 50000000, .resonance = 500000000, .morph = 1 << 28, .allow_oversampling = true};

/// One block's output, the compensated gain, and the noise generator's next value after the block.
struct Rendered {
	std::vector<int32_t> out;
	int32_t gain;
	int32_t next_noise;
};

Rendered renderMono(const FilterConfig& config, LowPassLadderMode mode) {
	jcong = kSeed;
	LowPassLadder filter;
	filter.zero();
	const int32_t gain = filter.configure(kGain, config, mode);
	std::vector<int32_t> block = kInput;
	std::vector<int32_t> scratch(block.size());
	filter.renderMono(block, scratch);
	return {.out = block, .gain = gain, .next_noise = static_cast<int32_t>(CONG)};
}

Rendered renderStereo(const FilterConfig& config, LowPassLadderMode mode) {
	jcong = kSeed;
	LowPassLadder filter;
	filter.zero();
	const int32_t gain = filter.configure(kGain, config, mode);
	std::vector<int32_t> block = kInput;
	std::vector<int32_t> scratch(block.size());
	filter.renderStereo(block, scratch);
	return {.out = block, .gain = gain, .next_noise = static_cast<int32_t>(CONG)};
}

const std::vector<int32_t> kLp12DarkMono{
    0,        -281052,  -1302452, -2857736, -4137448, -4684276,  -4764708,  -4862252,  -4907552,  -4121952,  -1683284,
    2457748,  7463616,  12324692, 16652160, 20745436, 24894148,  28682876,  31075628,  31226552,  29190172,  25791824,
    21812196, 17341892, 11975464, 5618236,  -988680,  -6598000,  -10341700, -12276828, -13032360, -12966264, -11738212,
    -8750028, -3984392, 1666316,  6950168,  11141324, 14362664,  17034192,  19078940,  19749104,  18273364,  14660624,
    9809928,  4800636,  85620,    -4579556, -9437948, -14037400, -17309544, -18383196, -17286804, -14821368, -11748856,
    -8150092, -3613020, 1965236,  7843736,  12772784, 15885884,  17241164,  17466256,  16917272};
const std::vector<int32_t> kLp12DarkStereo{
    0,        -280708,  -373488,  -1205436, -1411616, -2494864, -2788596, -3875780, -4292360, -5076420, -5160784,
    -4993956, -4343212, -3169288, -1985672, -22264,   1612308,  4226760,  5950432,  8502412,  9554432,  11451360,
    11607992, 12739728, 11886628, 12000744, 10073216, 9414832,  7054584,  6319424,  4076448,  3576312,  1766432,
    1950020,  1109600,  2315936,  2376344,  4202288,  4693968,  6796208,  7476644,  9462996,  9716440,  10860044,
    10208776, 10381876, 9003556,  8376552,  6350128,  5033100,  2730464,  1418728,  -384624,  -1103672, -2164604,
    -2179424, -2355872, -1414640, -611744,  1038148,  2208468,  3869348,  4885084,  6244268};
const std::vector<int32_t> kLp12MidMono{
    0,         -2129860,  -5359872,  -70048,    17778708,  37369928,  44705248,  37380712,  26953300,  25939864,
    33636100,  36348636,  22273612,  -5277668,  -30182176, -38575116, -32097652, -25307400, -29699340, -41568992,
    -46139140, -33592404, -9940840,  8910208,   13439024,  10005940,  13057324,  28818076,  48089664,  54950216,
    42951396,  22087560,  8588788,   8200500,   10851824,  2569716,   -18872332, -41102656, -49151812, -40514168,
    -27549276, -24025620, -30342032, -33268612, -20639676, 5032304,   28405072,  36200776,  30233556,  24699712,
    30421628,  43132924,  47808000,  34688960,  10077220,  -9684468,  -14679476, -11116676, -13588748, -28642212,
    -47380972, -54078448, -42316548, -21944116};
const std::vector<int32_t> kLp12MidStereo{
    0,         -2128696,  -2790780,  -4691924,   -4741088,   1025076,   2342396,   13806744,  15774536,  29525712,
    35676604,  48913504,  55111020,  56332544,   53224268,   39580296,  28505640,  6899232,   -10600036, -36227516,
    -56284532, -75258860, -84993684, -86096244,  -80985080,  -65901828, -49079668, -21632144, 4118900,   35766272,
    59885356,  81219568,  93648068,  98380884,   96885336,   83538208,  65852640,  36568168,  8781792,   -23825872,
    -49493976, -75172992, -93108640, -105399608, -108192604, -97826852, -81442288, -52488936, -26296852, 7736004,
    36010640,  67217436,  90359260,  106914856,  111895880,  104761164, 90481124,  65982516,  40676008,  6887424,
    -23359412, -56584692, -80329544, -98460056};
const std::vector<int32_t> kLp12BrightMono{
    0,         7840512,   38678048,  42568288,  6370028,   17330868,  41033156,  37747268,  51938884,  31178544,
    -18106472, -13610760, -4819832,  -13521348, -3360848,  -30271456, -66935136, -45032636, -26433492, -16549364,
    6938144,   -12402176, -27225900, 9790980,   29416692,  42287116,  57602776,  24606496,  6629784,   30815976,
    30470264,  29556688,  23453560,  -25469292, -42477524, -22016232, -24618132, -18311368, -21545212, -57467124,
    -49841488, -14625340, -2025416,  18989064,  18923464,  -7292636,  11911388,  42144848,  46220844,  55617172,
    35011236,  -3135404,  6222352,   17755864,  8282924,   5587280,   -27019112, -60063348, -41002108, -25495272,
    -22249564, -12875940, -34304140, -43070728};
const std::vector<int32_t> kLp12BrightStereo{
    0,          7806176,   10288272,   35940028,  41864668,  31861388,  25450992,   11825980,  21341736,  54214988,
    54156144,   6045068,   -37018212,  -63851492, -37898588, 21472772,  27105524,   -32987572, -82136328, -97586200,
    -26542248,  57470496,  68750484,   6068436,   -55518848, -50247704, 30826372,   114895124, 114788988, 14638740,
    -59926208,  -48346488, 14238052,   100259136, 66994420,  -45077988, -117833328, -96302140, -11563456, 72710336,
    32239956,   -52181148, -121101376, -59053796, 39746768,  117181312, 74865996,   -20209756, -74317848, -6500504,
    72747432,   138805724, 58271868,   -50375736, -95198616, -34927800, 43852132,   81611412,  -6864164,  -102318008,
    -133872264, -38362580, 50345976,   67131172};
const std::vector<int32_t> kLp24DarkMono{
    0,        2312,     20056,   85756,    246120,   544564,   1011120,  1668276,  2539832,  3649176,  5000812,
    6558176,  8242312,  9952900, 11590152, 13063556, 14284832, 15160212, 15601088, 15549188, 14997644, 13989048,
    12596396, 10904236, 9007032, 7020504,  5081860,  3334496,  1899656,  853752,   231140,   39336,    266868,
    875620,   1786680,  2880416, 4016916,  5065536,  5922500,  6506288,  6748592,  6596564,  6029440,  5076700,
    3817992,  2363396,  826764,  -685136,  -2067312, -3209876, -4007040, -4384324, -4318624, -3837072, -2998396,
    -1875660, -553688,  861036,  2237304,  3436372,  4341860,  4881028,  5021924,  4758720};
const std::vector<int32_t> kLp24DarkStereo{
    0,       2308,    3076,    19200,   24024,   79244,   95076,   224524,  262080,  502984,  573744,  953964,  1060124,
    1585812, 1711376, 2363320, 2474320, 3210888, 3259928, 4025280, 3962740, 4703188, 4493912, 5172356, 4805832, 5408700,
    4902428, 5442872, 4839048, 5350292, 4702524, 5222824, 4581112, 5140468, 4538512, 5147008, 4592500, 5234476, 4710492,
    5350156, 4825224, 5417496, 4857560, 5361676, 4745096, 5139704, 4467592, 4757496, 4053188, 4267036, 3571300, 3753388,
    3112940, 3307636, 2758280, 2995576, 2551956, 2839228, 2490820, 2809372, 2522760, 2834864, 2566216, 2829924};
const std::vector<int32_t> kLp24MidMono{
    0,         336492,    2378344,   7841568,   16237008,  24012100,  27503932,  26351964,  23360204,  21010044,
    18526488,  12816604,  2293688,   -10573848, -20801388, -25113864, -24637272, -23292296, -23555156, -23986264,
    -21068480, -13293980, -3107820,  5351768,   10263036,  13588156,  18450940,  25219552,  30513796,  30281060,
    23816472,  14554816,  6787160,   1697096,   -3146104,  -10430780, -19579476, -26811628, -28723024, -25624180,
    -21101100, -18195212, -16275644, -11940000, -2989020,  8718052,   18585244,  23412280,  24069348,  23994764,
    25157596,  25809412,  22416732,  13708240,  2504124,   -6666416,  -11728788, -14635972, -18721452, -24680036,
    -29439432, -29126292, -23033628, -14422424};
const std::vector<int32_t> kLp24MidStereo{
    0,         335564,    441764,    2261056,   2753044,   7132768,   8235132,   14684444,  16425260,  22946120,
    24777104,  28421316,  28560464,  26415192,  22916812,  14270348,  6878996,   -6081164,  -15684724, -28840876,
    -37258028, -45523920, -48714204, -48131028, -43900048, -33801476, -22861164, -6039004,  8434896,   26490932,
    39554740,  52345476,  59147348,  61819672,  59560572,  50598520,  39448624,  21292964,  4684496,   -16775192,
    -33317148, -50976084, -61863072, -69430320, -70554232, -64521388, -55053840, -37012060, -20115708, 3806608,
    22491540,  44377228,  58370136,  70637764,  74575908,  72628464,  64963452,  49503240,  33065216,  9511192,
    -10127236, -33810936, -50050500, -65827512};
const std::vector<int32_t> kLp24BrightMono{
    0,         10593140,  34366584,  32460776,  5274644,   12929016,  38744428,  37022312,  33819572,  24683696,
    -11758352, -22387044, 1707220,   -1453800,  -20319396, -25651596, -44425516, -51368236, -15523072, 2748888,
    -12724428, -10646956, -5170016,  -4975388,  27948604,  53646528,  30659240,  15385672,  21741004,  15207740,
    23076576,  37383808,  5492676,   -29766004, -24098760, -22085640, -23789744, -8696120,  -23203300, -53326004,
    -35984188, -6575644,  -717368,   14177788,  17455144,  -5742028,  6623040,   43781196,  43921408,  34157324,
    30344448,  2651412,   -6792640,  20805968,  16527780,  -14670280, -23822820, -37343120, -47767824, -17529868,
    -2490468,  -28518740, -31748872, -17230768};
const std::vector<int32_t> kLp24BrightStereo{
    0,         10563980,  13907564,  30705284,  33161792,  24024436,  21794416,  16005160,  22723708,  37660960,
    29990648,  873964,    -24633256, -44759752, -32119084, -1846200,  3324052,   -21489028, -45253956, -64755716,
    -31949700, 18684932,  40617332,  28033240,  28820,     -26779980, 7816412,   48933196,  70930952,  52180252,
    2865820,   -33213648, -18283972, 9953840,   30571056,  6208816,   -39186604, -70238472, -55409088, -15360048,
    12465872,  5038684,   -19855688, -38698576, -13127720, 28015016,  52815424,  48132444,  20738364,  251232,
    13292448,  32843132,  39244192,  19968024,  -12856648, -32964020, -26594128, -13520240, -13395220, -25926876,
    -42144184, -41186692, -18975928, 132604};
const std::vector<int32_t> kDriveDarkMono{
    0,        2304,     20048,   85744,    246112,   544560,   1011120,  1667666,  2536206,  3636100,  4966379,
    6478888,  8085012,  9675071, 11156541, 12446458, 13486174, 14212748, 14574407, 14531830, 14079385, 13237178,
    12043616, 10540596, 8800489, 6922697,  5045931,  3325115,  1898816,  853744,   231136,   39328,    266864,
    875616,   1785951,  2874802, 3999534,  5029914,  5864337,  6429078,  6661680,  6515735,  5968252,  5040859,
    3802938,  2360808,  826752,  -685136,  -2066318, -3201969, -3989779, -4361355, -4296862, -3821803, -2992096,
    -1874857, -553696,  861024,  2235450,  3425794,  4319662,  4848826,  4987107,  4728791};
const std::vector<int32_t> kDriveDarkStereo{
    0,       2304,    3072,    19200,   24016,   79232,   95072,   224512,  262080,  502976,  573744,  953952,  1060100,
    1585283, 1710728, 2360728, 2471086, 3202948, 3251413, 4007804, 3945993, 4674285, 4468889, 5134748, 4775021, 5365080,
    4869821, 5398288, 4807621, 5308334, 4673626, 5184276, 4554471, 5103452, 4512669, 5109874, 4565651, 5195708, 4681446,
    5308194, 4794053, 5373631, 4825789, 5319388, 4715412, 5102698, 4443057, 4727582, 4035381, 4246218, 3559140, 3739087,
    3105952, 3298566, 2753374, 2989279, 2548263, 2833843, 2487485, 2804162, 2519234, 2829517, 2562435, 2824602};
const std::vector<int32_t> kDriveMidMono{
    0,         242192,    2447147,   8371694,   15938415,  21431079,  24316585,  26136258,  28016953,  29734042,
    30589662,  30231834,  27869505,  21553706,  10301006,  -3234542,  -15855195, -24911043, -29550542, -31285325,
    -31607826, -31021171, -29471025, -26815791, -22281343, -12850224, 4518369,   21159508,  28451284,  30569524,
    31008083,  30949587,  30744981,  30089480,  27722931,  20108945,  2962048,   -15163675, -24410326, -28159781,
    -30048342, -31158048, -31572369, -31119067, -28984770, -23112512, -12014632, 1965631,   15264139,  24786042,
    29571153,  31323495,  31642172,  31053656,  29494977,  26815575,  22227625,  12669857,  -4846848,  -21402178,
    -28553531, -30610804, -31025862, -30953792};
const std::vector<int32_t> kDriveMidStereo{
    0,         241312,    316560,    2366343,   2932605,   7683033,   8697438,   14458631,  15798063,  20747200,
    22010850,  24821342,  25001883,  25618492,  24351942,  22860036,  19269116,  14114966,  6230834,   -3345277,
    -12074150, -19086285, -23037479, -25571335, -26438393, -26496839, -25227411, -22430017, -17389484, -9564593,
    -152256,   9600767,   17068996,  22294783,  24942523,  26138826,  25774168,  24244928,  20886021,  15366749,
    7309471,   -2666908,  -12001552, -19066094, -23130250, -25183007, -25702165, -25093475, -23123740, -19245952,
    -12805564, -3574875,  6228054,   14856086,  20374852,  23861272,  25387058,  25774988,  24732891,  22165058,
    17161918,  9845872,   132576,    -9034972};
const std::vector<int32_t> kDriveBrightMono{
    0,         7319591,   26758389,  31623944,  31248551,  28172239,  28409588,  32257877,  33244150,  33085934,
    27704972,  -12657164, -24976279, -14222185, -4847806,  -21733144, -32389214, -33382658, -33340354, -31657633,
    -17983599, -5168464,  -11195385, -7147329,  22195919,  32296432,  33267217,  33189471,  31787390,  28040298,
    29240046,  31617877,  30466136,  6506059,   -29908869, -32474193, -31596453, -28284163, -28437913, -32275215,
    -33247361, -33087076, -27726999, 12591007,  24971001,  14292679,  4879761,   21667516,  32382404,  33382413,
    33339314,  31632134,  17788011,  5086587,   11294023,  7093233,   -22410961, -32320629, -33270441, -33188110,
    -31764338, -27999081, -29254919, -31632310};
const std::vector<int32_t> kDriveBrightStereo{
    0,         7296469,   9468212,   25768826,  28066548,  29818988,  29899479,  30714394,  31982515,  32462060,
    32025099,  27753781,  10139813,  -13496358, -22571505, -25718729, -29075688, -31775280, -32690901, -32751148,
    -31646520, -27794254, -20970856, -11752536, 2960155,   23089858,  31082154,  32671350,  32831141,  32290304,
    31484608,  30899526,  30195184,  26575715,  7908017,   -21572238, -30329720, -31703064, -31418964, -31593253,
    -32234333, -32559759, -32076880, -27845450, -10214520, 13400769,  22573272,  25714278,  29086110,  31771206,
    32695942,  32750356,  31623423,  27795438,  20785488,  11755823,  -3285380,  -23094568, -31154412, -32669889,
    -32833085, -32295470, -31450896, -30905975};
const std::vector<int32_t> kDriveBrightUnderLoadMono{
    0,         10353863,  27975527,  31504529,  28323904,  19444562,  23531166,  31486021,  32977021,  32054730,
    12557053,  -23179774, -20488351, 13776,     -8431934,  -29265916, -32854288, -33228123, -32765231, -25233323,
    -377472,   -7746225,  -21566866, -6279048,  27279010,  32423684,  32919354,  32352227,  27024506,  18038199,
    25187113,  30110337,  26094689,  -12740939, -31560069, -32017531, -27692608, -19621730, -23621364, -31470344,
    -32978582, -32050565, -12546169, 23182719,  20427340,  -39136,    8590180,   29244787,  32842047,  33227429,
    32772677,  25333050,  405552,    7565845,   21728707,  6747232,   -27275939, -32430958, -32918563, -32352621,
    -27023223, -18039869, -25187955, -30108331};
const std::vector<int32_t> kDriveBrightUnderLoadStereo{
    0,         10327384,  13280695,  26755398,  28542947,  29539300,  30163630,  28468334,  29974129,  30465782,
    29046018,  20687940,  -6044712,  -22659394, -29362103, -25327483, -28840452, -28649340, -31371316, -31860468,
    -29640992, -21025183, -3183326,  6805920,   15779590,  17564199,  28361922,  31319127,  31827921,  31666786,
    28436115,  24979902,  21193516,  12242802,  -8843062,  -25330288, -31891174, -31420818, -31674510, -28593287,
    -29288545, -30097640, -29056075, -21028108, 5266124,   23109583,  29456432,  25299637,  28896317,  28606888,
    31361686,  31874789,  29652279,  20943984,  3391386,   -6830051,  -15909187, -17571881, -28391635, -31342466,
    -31820770, -31676859, -28440024, -24955836};
const std::vector<int32_t> kLp24FreshMono{
    0,         24294520,  31990346,  23804834,  15529162,  20599752,  35674880,  44336348,  34645044,  11817536,
    -7189788,  -11132680, -5430316,  -5323140,  -18870324, -37932562, -46514948, -37391378, -19402126, -7961908,
    -9087686,  -13810818, -8559616,  9792446,   30161760,  38524334,  32105210,  21563618,  19182690,  25807302,
    30307428,  21864450,  1866602,   -17131570, -24068686, -20182460, -16738564, -22407612, -33814704, -39154290,
    -30793664, -13395038, 892362,    4853752,   3057558,   6092022,   18581040,  33766562,  40280806,  33787030,
    21177244,  13100082,  12931420,  13839748,  6807416,   -9129432,  -25320078, -32123346, -28469514, -22327738,
    -21812448, -26508426, -28159260, -19792054};
const std::vector<int32_t> kLp24FadeMono{
    0,         24294538,  31990588,  23805970,  15532014,  20603690,  35676336,  44331418,  34634214,  11805622,
    -7198916,  -11140522, -5441008,  -5336984,  -18880762, -37929872, -46496332, -37365240, -19380892, -7955058,
    -9099362,  -13839120, -8594046,  9766472,   30154562,  38538082,  32130602,  21585904,  19195704,  25815838,
    30315650,  21870392,  1866948,   -17137670, -24079806, -20195786, -16752110, -22415798, -33806198, -39123918,
    -30753110, -13365746, 900534,    4852704,   3068426,   6127016,   18624552,  33785726,  40261226,  33745218,
    21141686,  13080138,  12910254,  13796900,  6748002,   -9176698,  -25330232, -32096318, -28429200, -22301778,
    -21807528, -26501106, -28123056, -19729532};
const std::vector<int32_t> kLp24FadeStereo{
    0,         24294548,  31951776,  23639796,  14997604,  19776112,  34546696,  43552652,  34108848,  11998156,
    -6591908,  -9489542,  -2445502,  -801430,   -13132522, -32461678, -41836426, -34755516, -18104406, -8874948,
    -12040118, -19985904, -17608916, -1247042,  18558776,  28027962,  22617886,  14174390,  13781768,  24871364,
    34420964,  31983204,  16277578,  -168926,   -5932332,  -2091492,  1118210,   -7474326,  -23327620, -36303916,
    -34975370, -24497416, -14906626, -15333666, -21088874, -20509294, -8535114,  11163678,  23636888,  26183514,
    20434614,  20815192,  27812030,  37513682,  37066700,  24497702,  7529228,   -4433808,  -5941552,  -7492692,
    -13498642, -29155872, -41062830, -44246144};
const std::vector<int32_t> kLp24ResetMono{
    0,         338276,    2390500,   7879852,   16309380,  24092456,  27528808,  26279408,  23220012,  20872284,
    18431036,  12741680,  2199576,   -10686888, -20880920, -25094648, -24511948, -23125868, -23426840, -23946852,
    -21132600, -13442724, -3281384,  5225916,   10229448,  13650028,  18561360,  25313328,  30566852,  30314828,
    23848060,  14577008,  6788416,   1675484,   -3184516,  -10475720, -19624096, -26837984, -28696228, -25530520,
    -20978592, -18108476, -16251912, -11942984, -2958608,  8814240,   18702792,  23463184,  24018188,  23887244,
    25067572,  25759684,  22364732,  13604480,  2362268,   -6777724,  -11752380, -14573996, -18630188, -24622000,
    -29428568, -29110320, -22955560, -14289140};
const std::vector<int32_t> kLp24MidFadeResetMono{
    0,         12929602,  17948200,  16250562, 15900798,  22258772,  31811118,  35767480,  29219274,  16106878,
    4960024,   189280,    -1816290,  -7874998, -19829646, -31840796, -36066862, -30610032, -21300304, -15541622,
    -14807974, -13651068, -6073700,  7612414,  20702012,  26731098,  25693304,  23354206,  24590216,  27950176,
    27247402,  18410384,  4201708,   -8212644, -14167006, -15584554, -18114592, -24513700, -31382010, -32675162,
    -26116044, -15615714, -7236650,  -3115230, 209180,    7401844,   18661670,  28888674,  32555470,  29068560,
    23004140,  19095360,  17395492,  13705614, 4667392,   -8038618,  -18888852, -23783668, -23780512, -23402500,
    -25422978, -27738926, -25671578, -17148592};

/// The bright setting with the drive ladder's oversampling not allowed (under CPU load).
constexpr FilterConfig kBrightUnderLoad{
    .frequency = 50000000, .resonance = 500000000, .morph = 1 << 28, .allow_oversampling = false};

} // namespace

namespace {

using deluge::dsp::Mono;
using deluge::dsp::q31;
using deluge::dsp::Stereo;
using deluge::dsp::StereoSample;
using filter_workloads::WorkloadId;
using filters_reference::ReferenceLowPassLadder;
using Ladder = LowPassLadderKernel<CongNoise>;

static_assert(deluge::dsp::processor_kernel<Ladder>);
static_assert(deluge::dsp::moded_kernel<Ladder>);
static_assert(!std::is_polymorphic_v<Ladder>);
static_assert(deluge::dsp::processor<Mono<Ladder>>);
static_assert(deluge::dsp::processor<Stereo<Ladder>>);

/// Every ladder workload: each mode steady, drive oversampling and not, the mode walk, the oversampling toggle and the
/// fade, mono and stereo.
constexpr std::array<WorkloadId, 16> kLadderWorkloads{
    WorkloadId::LP12_MONO,           WorkloadId::LP12_STEREO,
    WorkloadId::LP24_MONO,           WorkloadId::LP24_STEREO,
    WorkloadId::LPDRIVE_MONO,        WorkloadId::LPDRIVE_STEREO,
    WorkloadId::LPDRIVE_OS_MONO,     WorkloadId::LPDRIVE_OS_STEREO,
    WorkloadId::LPDRIVE_OS_OFF_MONO, WorkloadId::LPDRIVE_OS_OFF_STEREO,
    WorkloadId::LP_MODE_WALK_MONO,   WorkloadId::LP_MODE_WALK_STEREO,
    WorkloadId::LP_OS_TOGGLE_MONO,   WorkloadId::LP_OS_TOGGLE_STEREO,
    WorkloadId::LP_FADE_MONO,        WorkloadId::LP_FADE_STEREO,
};

/// @return Each of @p ids where the kernel parts from the reference, named with the block and what parted.
std::vector<std::string> partings(std::span<const WorkloadId> ids) {
	std::vector<std::string> parted;
	for (const WorkloadId id : ids) {
		const filter_workloads::FilterWorkload& workload = filter_workloads::filter_workload(id);
		const auto parting = kernel_filters::first_parting(workload);
		if (parting) {
			parted.push_back(std::string{workload.name} + " block " + std::to_string(parting->block) + ": "
			                 + parting->what);
		}
	}
	return parted;
}

/// One block's settings: the ladder's mode, and the config that with it picks the drive ladder's oversampling.
struct Setting {
	LowPassLadderMode mode;
	FilterConfig config;
};

/// Every effective mode: 12dB, 24dB, drive at the rate, and drive oversampled (the same config, oversampling allowed).
const std::array<Setting, 4> kSettings{{
    {.mode = LowPassLadderMode::TRANSISTOR_12DB, .config = kMid},
    {.mode = LowPassLadderMode::TRANSISTOR_24DB, .config = kBright},
    {.mode = LowPassLadderMode::TRANSISTOR_24DB_DRIVE, .config = kBrightUnderLoad},
    {.mode = LowPassLadderMode::TRANSISTOR_24DB_DRIVE, .config = kBright},
}};

/// The configs the coefficient checks sweep: dark to bright, resonance off to past the limits, morph on and off, each
/// with oversampling allowed and not.
std::vector<FilterConfig> sweep_configs() {
	std::vector<FilterConfig> configs;
	for (const q31 frequency :
	     {q31{1 << 20}, q31{1 << 22}, q31{1 << 24}, q31{30000000}, q31{50000000}, q31{1 << 26}, q31{ONE_Q31}}) {
		for (const q31 resonance : {q31{0}, q31{1 << 27}, q31{400000000}, q31{500000000}, q31{536870911}}) {
			for (const q31 morph : {q31{0}, q31{1 << 28}}) {
				for (const bool allow : {true, false}) {
					configs.push_back(
					    {.frequency = frequency, .resonance = resonance, .morph = morph, .allow_oversampling = allow});
				}
			}
		}
	}
	return configs;
}

/// A channel's follower and its four sections' memories, Q31.
using ChannelWords = std::array<q31, 5>;

template <class Noise>
ChannelWords words(const typename LowPassLadderKernel<Noise>::State& s) {
	return {s.noise.y, s.sections[0].z, s.sections[1].z, s.sections[2].z, s.sections[3].z};
}

ChannelWords words(const ReferenceLowPassLadder::Channel& c) {
	return {c.noise_last, c.sections[0].memory(), c.sections[1].memory(), c.sections[2].memory(),
	        c.sections[3].memory()};
}

/// @return How many steps of the noise generator take @p from to @p to (at most @p limit), or limit + 1.
uint32_t draws_between(uint32_t from, uint32_t to, uint32_t limit) {
	uint32_t n = 0;
	while (from != to && n <= limit) {
		from = CongruentialStep::next(from);
		++n;
	}
	return n;
}

ChannelWords words(const Ladder::State& s) {
	return words<CongNoise>(s);
}

/// The stream the seeded policy draws from.
SeededNoise seeded_stream{filter_workloads::kCongSeed};

/// A noise policy drawing from @ref seeded_stream.
struct SeededPolicy {
	[[gnu::always_inline]] static q31 render() { return seeded_stream.render(); }
};

using SeededLadder = LowPassLadderKernel<SeededPolicy>;

/// The kernel's settings for @p setting at the test gain.
Ladder::Settings settings_for(const Setting& setting) {
	return {.gain = kGain, .config = setting.config, .mode = setting.mode};
}

/// kInput as interleaved stereo frames.
std::vector<StereoSample> input_frames() {
	std::vector<StereoSample> frames(kInput.size() / 2);
	for (std::size_t i = 0; i < frames.size(); ++i) {
		frames[i] = {.l = kInput[2 * i], .r = kInput[(2 * i) + 1]};
	}
	return frames;
}

} // namespace

// clang-format off
describe low_pass_ladder("LowPassLadder", ${
	it("renders the 12dB ladder, dark, in mono exactly", _ {
		const Rendered r = renderMono(kDark, LowPassLadderMode::TRANSISTOR_12DB);
		expect(r.out).to_equal(kLp12DarkMono);
		expect(r.gain).to_equal(268435456);
		expect(r.next_noise).to_equal(872499352);
	});
	it("renders the 12dB ladder, dark, in stereo exactly", _ {
		const Rendered r = renderStereo(kDark, LowPassLadderMode::TRANSISTOR_12DB);
		expect(r.out).to_equal(kLp12DarkStereo);
		expect(r.gain).to_equal(268435456);
		expect(r.next_noise).to_equal(872499352);
	});
	it("renders the 12dB ladder, mid, in mono exactly", _ {
		const Rendered r = renderMono(kMid, LowPassLadderMode::TRANSISTOR_12DB);
		expect(r.out).to_equal(kLp12MidMono);
		expect(r.gain).to_equal(671088640);
		expect(r.next_noise).to_equal(872499352);
	});
	it("renders the 12dB ladder, mid, in stereo exactly", _ {
		const Rendered r = renderStereo(kMid, LowPassLadderMode::TRANSISTOR_12DB);
		expect(r.out).to_equal(kLp12MidStereo);
		expect(r.gain).to_equal(671088640);
		expect(r.next_noise).to_equal(872499352);
	});
	it("renders the 12dB ladder, bright, in mono exactly", _ {
		const Rendered r = renderMono(kBright, LowPassLadderMode::TRANSISTOR_12DB);
		expect(r.out).to_equal(kLp12BrightMono);
		expect(r.gain).to_equal(802774176);
		expect(r.next_noise).to_equal(872499352);
	});
	it("renders the 12dB ladder, bright, in stereo exactly", _ {
		const Rendered r = renderStereo(kBright, LowPassLadderMode::TRANSISTOR_12DB);
		expect(r.out).to_equal(kLp12BrightStereo);
		expect(r.gain).to_equal(802774176);
		expect(r.next_noise).to_equal(872499352);
	});
	it("renders the 24dB ladder, dark, in mono exactly", _ {
		const Rendered r = renderMono(kDark, LowPassLadderMode::TRANSISTOR_24DB);
		expect(r.out).to_equal(kLp24DarkMono);
		expect(r.gain).to_equal(268435456);
		expect(r.next_noise).to_equal(872499352);
	});
	it("renders the 24dB ladder, dark, in stereo exactly", _ {
		const Rendered r = renderStereo(kDark, LowPassLadderMode::TRANSISTOR_24DB);
		expect(r.out).to_equal(kLp24DarkStereo);
		expect(r.gain).to_equal(268435456);
		expect(r.next_noise).to_equal(872499352);
	});
	it("renders the 24dB ladder, mid, in mono exactly", _ {
		const Rendered r = renderMono(kMid, LowPassLadderMode::TRANSISTOR_24DB);
		expect(r.out).to_equal(kLp24MidMono);
		expect(r.gain).to_equal(671088640);
		expect(r.next_noise).to_equal(872499352);
	});
	it("renders the 24dB ladder, mid, in stereo exactly", _ {
		const Rendered r = renderStereo(kMid, LowPassLadderMode::TRANSISTOR_24DB);
		expect(r.out).to_equal(kLp24MidStereo);
		expect(r.gain).to_equal(671088640);
		expect(r.next_noise).to_equal(872499352);
	});
	it("renders the 24dB ladder, bright, in mono exactly", _ {
		const Rendered r = renderMono(kBright, LowPassLadderMode::TRANSISTOR_24DB);
		expect(r.out).to_equal(kLp24BrightMono);
		expect(r.gain).to_equal(802774176);
		expect(r.next_noise).to_equal(872499352);
	});
	it("renders the 24dB ladder, bright, in stereo exactly", _ {
		const Rendered r = renderStereo(kBright, LowPassLadderMode::TRANSISTOR_24DB);
		expect(r.out).to_equal(kLp24BrightStereo);
		expect(r.gain).to_equal(802774176);
		expect(r.next_noise).to_equal(872499352);
	});
	it("renders the drive ladder, dark, in mono exactly", _ {
		const Rendered r = renderMono(kDark, LowPassLadderMode::TRANSISTOR_24DB_DRIVE);
		expect(r.out).to_equal(kDriveDarkMono);
		expect(r.gain).to_equal(429496729);
		expect(r.next_noise).to_equal(872499352);
	});
	it("renders the drive ladder, dark, in stereo exactly", _ {
		const Rendered r = renderStereo(kDark, LowPassLadderMode::TRANSISTOR_24DB_DRIVE);
		expect(r.out).to_equal(kDriveDarkStereo);
		expect(r.gain).to_equal(429496729);
		expect(r.next_noise).to_equal(872499352);
	});
	it("renders the drive ladder, mid, in mono exactly", _ {
		const Rendered r = renderMono(kMid, LowPassLadderMode::TRANSISTOR_24DB_DRIVE);
		expect(r.out).to_equal(kDriveMidMono);
		expect(r.gain).to_equal(429496729);
		expect(r.next_noise).to_equal(670871512);
	});
	it("renders the drive ladder, mid, in stereo exactly", _ {
		const Rendered r = renderStereo(kMid, LowPassLadderMode::TRANSISTOR_24DB_DRIVE);
		expect(r.out).to_equal(kDriveMidStereo);
		expect(r.gain).to_equal(429496729);
		expect(r.next_noise).to_equal(670871512);
	});
	it("renders the drive ladder, bright, in mono exactly", _ {
		const Rendered r = renderMono(kBright, LowPassLadderMode::TRANSISTOR_24DB_DRIVE);
		expect(r.out).to_equal(kDriveBrightMono);
		expect(r.gain).to_equal(429496729);
		expect(r.next_noise).to_equal(670871512);
	});
	it("renders the drive ladder, bright, in stereo exactly", _ {
		const Rendered r = renderStereo(kBright, LowPassLadderMode::TRANSISTOR_24DB_DRIVE);
		expect(r.out).to_equal(kDriveBrightStereo);
		expect(r.gain).to_equal(429496729);
		expect(r.next_noise).to_equal(670871512);
	});
	it("renders the drive ladder, bright, in mono with oversampling not allowed exactly", _ {
		const Rendered r = renderMono(kBrightUnderLoad, LowPassLadderMode::TRANSISTOR_24DB_DRIVE);
		expect(r.out).to_equal(kDriveBrightUnderLoadMono);
		expect(r.gain).to_equal(429496729);
		expect(r.next_noise).to_equal(872499352);
	});
	it("renders the drive ladder, bright, in stereo with oversampling not allowed exactly", _ {
		const Rendered r = renderStereo(kBrightUnderLoad, LowPassLadderMode::TRANSISTOR_24DB_DRIVE);
		expect(r.out).to_equal(kDriveBrightUnderLoadStereo);
		expect(r.gain).to_equal(429496729);
		expect(r.next_noise).to_equal(872499352);
	});
	it("fades in from dry when fresh (its initializers start a fade)", _ {
		jcong = kSeed;
		LowPassLadder filter;
		std::ignore = filter.configure(kGain, kMid, LowPassLadderMode::TRANSISTOR_24DB);
		std::vector<int32_t> block = kInput;
		std::vector<int32_t> scratch(block.size());
		filter.renderMono(block, scratch);
		expect(block).to_equal(kLp24FreshMono);
	});
	it("fades in after reset(true) in mono", _ {
		jcong = kSeed;
		LowPassLadder filter;
		filter.zero();
		std::ignore = filter.configure(kGain, kMid, LowPassLadderMode::TRANSISTOR_24DB);
		std::vector<int32_t> block = kInput;
		std::vector<int32_t> scratch(block.size());
		filter.renderMono(block, scratch);
		filter.reset(true);
		block = kInput;
		filter.renderMono(block, scratch);
		expect(block).to_equal(kLp24FadeMono);
	});
	it("fades in after reset(true) in stereo, each channel's sample stepping the fade", _ {
		jcong = kSeed;
		LowPassLadder filter;
		filter.zero();
		std::ignore = filter.configure(kGain, kMid, LowPassLadderMode::TRANSISTOR_24DB);
		std::vector<int32_t> block = kInput;
		std::vector<int32_t> scratch(block.size());
		filter.renderStereo(block, scratch);
		filter.reset(true);
		block = kInput;
		filter.renderStereo(block, scratch);
		expect(block).to_equal(kLp24FadeStereo);
	});
	it("clears its memories without a fade after reset(false)", _ {
		jcong = kSeed;
		LowPassLadder filter;
		filter.zero();
		std::ignore = filter.configure(kGain, kMid, LowPassLadderMode::TRANSISTOR_24DB);
		std::vector<int32_t> block = kInput;
		std::vector<int32_t> scratch(block.size());
		filter.renderMono(block, scratch);
		filter.reset(false);
		block = kInput;
		filter.renderMono(block, scratch);
		expect(block).to_equal(kLp24ResetMono);
	});
	it("keeps fading through reset(false) when a fade is under way", _ {
		jcong = kSeed;
		LowPassLadder filter;
		filter.zero();
		std::ignore = filter.configure(kGain, kMid, LowPassLadderMode::TRANSISTOR_24DB);
		filter.reset(true);
		std::vector<int32_t> block = kInput;
		std::vector<int32_t> scratch(block.size());
		filter.renderMono(block, scratch);
		filter.reset(false);
		block = kInput;
		filter.renderMono(block, scratch);
		expect(block).to_equal(kLp24MidFadeResetMono);
	});
});
// clang-format on

// clang-format off
describe low_pass_ladder_kernel("LowPassLadderKernel", ${
	it("renders every ladder workload as the reference does, mono and stereo: each block's output, gain and CONG "
	   "draws, and the whole state", _ {
		expect(partings(kLadderWorkloads)).to_equal(std::vector<std::string>{});
	});

	it("renders every ladder workload as recorded, the CONG draws per block included", _ {
		for (const WorkloadId id : kLadderWorkloads) {
			const filter_workloads::FilterWorkload& workload = filter_workloads::filter_workload(id);
			expect(kernel_filters::record(workload)).to_equal(filter_workloads::kRecorded.at(static_cast<size_t>(id)));
		}
	});

	it("chooses oversampling and compensates the gain as the reference's configure does", _ {
		std::size_t oversampled = 0;
		std::size_t at_rate = 0;
		for (const FilterConfig& config : sweep_configs()) {
			for (const LowPassLadderMode mode : {LowPassLadderMode::TRANSISTOR_12DB, LowPassLadderMode::TRANSISTOR_24DB,
			                                     LowPassLadderMode::TRANSISTOR_24DB_DRIVE}) {
				ReferenceLowPassLadder reference;
				const q31 gain = reference.configure(kGain, config, mode);
				const Ladder::Coefficients c = Ladder::coefficients({.gain = kGain, .config = config, .mode = mode});
				expect(Ladder::gain(c)).to_equal(gain);
				Ladder::Mode expected = Ladder::Mode::T12;
				if (mode == LowPassLadderMode::TRANSISTOR_24DB) {
					expected = Ladder::Mode::T24;
				}
				else if (mode == LowPassLadderMode::TRANSISTOR_24DB_DRIVE) {
					expected = reference.oversample() ? Ladder::Mode::DRIVE_OVERSAMPLED : Ladder::Mode::DRIVE;
					if (reference.oversample()) {
						++oversampled;
					}
					else {
						++at_rate;
					}
				}
				expect(c.mode).to_equal(expected);
			}
		}
		expect(oversampled).to_be_greater_than(0u);
		expect(at_rate).to_be_greater_than(0u);
	});

	it("draws one CONG value per channel per sample, two when the drive ladder oversamples, and renders as the "
	   "reference does", _ {
		std::vector<q31> scratch(kInput.size());
		for (const Setting& setting : kSettings) {
			const uint32_t per_sample = Ladder::coefficients(settings_for(setting)).mode
			                                    == Ladder::Mode::DRIVE_OVERSAMPLED ? 2 : 1;
			for (const bool stereo : {false, true}) {
				ReferenceLowPassLadder reference;
				reference.zero();
				std::ignore = reference.configure(kGain, setting.config, setting.mode);
				std::vector<q31> expected = kInput;
				filter_workloads::reset_cong();
				if (stereo) {
					reference.renderStereo(expected, scratch);
				}
				else {
					reference.renderMono(expected, scratch);
				}
				const uint32_t reference_end = jcong;

				std::vector<q31> out = kInput;
				filter_workloads::reset_cong();
				if (stereo) {
					Stereo<Ladder> ladder{settings_for(setting)};
					const auto frames = deluge::dsp::as_frames(out);
					ladder.renderBlock(frames, frames);
				}
				else {
					Mono<Ladder> ladder{settings_for(setting)};
					ladder.renderBlock(out, out);
				}
				const uint32_t limit = 4 * static_cast<uint32_t>(kInput.size());
				expect(draws_between(filter_workloads::kCongSeed, jcong, limit))
				    .to_equal(per_sample * static_cast<uint32_t>(kInput.size()));
				expect(jcong).to_equal(reference_end);
				expect(out).to_equal(expected);
			}
		}
		filter_workloads::reset_cong();
	});

	it("draws for the left channel before the right in every frame, in every mode (a seeded stream)", _ {
		const std::vector<StereoSample> input = input_frames();
		for (const Setting& setting : kSettings) {
			const SeededLadder::Settings settings{.gain = kGain, .config = setting.config, .mode = setting.mode};
			const uint32_t per_sample = SeededLadder::coefficients(settings).mode
			                                    == SeededLadder::Mode::DRIVE_OVERSAMPLED ? 2 : 1;

			seeded_stream = SeededNoise{filter_workloads::kCongSeed};
			Stereo<SeededLadder> stereo{settings};
			std::vector<StereoSample> out(input.size());
			stereo.renderBlock(input, out);
			const uint32_t stereo_end = seeded_stream.state();

			// The same draws, made by two mono ladders stepped a frame at a time: the left channel's, then the right's.
			seeded_stream = SeededNoise{filter_workloads::kCongSeed};
			Mono<SeededLadder> left{settings};
			Mono<SeededLadder> right{settings};
			std::vector<StereoSample> expected(input.size());
			for (std::size_t i = 0; i < input.size(); ++i) {
				expected[i].l = left.render(input[i].l);
				expected[i].r = right.render(input[i].r);
			}
			expect(stereo_end).to_equal(seeded_stream.state());
			expect(draws_between(filter_workloads::kCongSeed, stereo_end, 4 * static_cast<uint32_t>(kInput.size())))
			    .to_equal(per_sample * static_cast<uint32_t>(kInput.size()));
			for (std::size_t i = 0; i < input.size(); ++i) {
				expect(out[i].l).to_equal(expected[i].l);
				expect(out[i].r).to_equal(expected[i].r);
			}
			expect(words<SeededPolicy>(stereo.left())).to_equal(words<SeededPolicy>(left.state()));
			expect(words<SeededPolicy>(stereo.right())).to_equal(words<SeededPolicy>(right.state()));
		}
	});

	it("carries each channel's state across every mode change, oversampling switched on and off, as the reference "
	   "does", _ {
		std::vector<q31> scratch(kInput.size());
		for (const Setting& from : kSettings) {
			for (const Setting& to : kSettings) {
				ReferenceLowPassLadder reference;
				reference.zero();
				Stereo<Ladder> ladder;
				uint32_t reference_cong = filter_workloads::kCongSeed;
				uint32_t kernel_cong = filter_workloads::kCongSeed;
				for (const Setting& setting : {from, to, from}) {
					std::vector<q31> expected = kInput;
					jcong = reference_cong;
					std::ignore = reference.configure(kGain, setting.config, setting.mode);
					reference.renderStereo(expected, scratch);
					reference_cong = jcong;

					const Ladder::State left = ladder.left();
					const Ladder::State right = ladder.right();
					ladder.configure(settings_for(setting));
					expect(words(ladder.left())).to_equal(words(left));
					expect(words(ladder.right())).to_equal(words(right));

					std::vector<q31> out = kInput;
					jcong = kernel_cong;
					const auto frames = deluge::dsp::as_frames(out);
					ladder.renderBlock(frames, frames);
					kernel_cong = jcong;

					expect(out).to_equal(expected);
					expect(kernel_cong).to_equal(reference_cong);
					expect(words(ladder.left())).to_equal(words(reference.left()));
					expect(words(ladder.right())).to_equal(words(reference.right()));
				}
			}
		}
		filter_workloads::reset_cong();
	});

	it("silences the sections on reset and keeps the noise follower, as the reference's reset does", _ {
		std::vector<q31> scratch(kInput.size());
		ReferenceLowPassLadder reference;
		reference.zero();
		std::ignore = reference.configure(kGain, kMid, LowPassLadderMode::TRANSISTOR_24DB);
		std::vector<q31> expected = kInput;
		filter_workloads::reset_cong();
		reference.renderMono(expected, scratch);
		reference.reset(false);

		Mono<Ladder> ladder{Ladder::Settings{.gain = kGain, .config = kMid, .mode = LowPassLadderMode::TRANSISTOR_24DB}};
		std::vector<q31> out = kInput;
		filter_workloads::reset_cong();
		ladder.renderBlock(out, out);
		ladder.reset();

		expect(ladder.state().noise.y).not_().to_equal(0);
		expect(words(ladder.state())).to_equal(words(reference.left()));
		expect(words(ladder.state())).to_equal(ChannelWords{ladder.state().noise.y, 0, 0, 0, 0});
		filter_workloads::reset_cong();
	});
});
// clang-format on

CPPSPEC_MAIN(low_pass_ladder, low_pass_ladder_kernel);
