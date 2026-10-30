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

// FilterSet: a low-pass and a high-pass filter in series either way or in parallel, with the slots' resets and fades.
// Exact reference values pin the routing, the gain chain and the reset and fade rules, each case from a reset set
// with the synth-wide noise generator seeded the same.

#include "dsp-next/fixed/filter/filter_set.h"
#include "util/synth_noise.h"

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

/// ±0x60000000, the sign flipping every two samples: near full scale.
const std::vector<int32_t> kLoud = [] {
	std::vector<int32_t> block(64);
	for (std::size_t i = 0; i < block.size(); i++) {
		block[i] = ((i / 2) % 2) != 0U ? -0x60000000 : 0x60000000;
	}
	return block;
}();

constexpr FilterConfig kLow{.frequency = 1 << 24, .resonance = 1 << 28, .morph = 1 << 27, .allow_oversampling = true};
constexpr FilterConfig kHigh{.frequency = 1 << 23, .resonance = 1 << 27, .morph = 1 << 26, .allow_oversampling = true};
constexpr FilterConfig kOpenLow{.frequency = 1 << 26, .resonance = 0, .morph = 0, .allow_oversampling = true};
constexpr FilterConfig kOpenHigh{.frequency = 0, .resonance = 0, .morph = 0, .allow_oversampling = true};

constexpr LowPassSlot kLowOff{.kind = LowPassSlot::Kind::OFF};
constexpr LowPassSlot kLow12{.kind = LowPassSlot::Kind::LADDER, .ladder = LowPassLadderMode::TRANSISTOR_12DB};
constexpr LowPassSlot kLow24{.kind = LowPassSlot::Kind::LADDER, .ladder = LowPassLadderMode::TRANSISTOR_24DB};
constexpr LowPassSlot kLowDrive{.kind = LowPassSlot::Kind::LADDER, .ladder = LowPassLadderMode::TRANSISTOR_24DB_DRIVE};
constexpr LowPassSlot kLowBand{.kind = LowPassSlot::Kind::SVF, .svf = SvfMode::BAND};
constexpr LowPassSlot kLowNotch{.kind = LowPassSlot::Kind::SVF, .svf = SvfMode::NOTCH};
constexpr HighPassSlot kHighOff{.kind = HighPassSlot::Kind::OFF};
constexpr HighPassSlot kHighLadder{.kind = HighPassSlot::Kind::LADDER};
constexpr HighPassSlot kHighBand{.kind = HighPassSlot::Kind::SVF, .svf = SvfMode::BAND};
constexpr HighPassSlot kHighNotch{.kind = HighPassSlot::Kind::SVF, .svf = SvfMode::NOTCH};

/// A set with its scratch, starting reset.
struct Rig {
	FilterSet set;
	std::vector<int32_t> fade = std::vector<int32_t>(64);
	std::vector<int32_t> parallel = std::vector<int32_t>(64);

	int32_t configure(LowPassSlot low, HighPassSlot high, FilterRoute route = FilterRoute::HIGH_TO_LOW,
	                  const FilterConfig& low_config = kLow, const FilterConfig& high_config = kHigh) {
		return set.configure(kGain, low_config, low, high_config, high, route);
	}
	std::vector<int32_t> mono(const std::vector<int32_t>& in) {
		std::vector<int32_t> block = in;
		set.renderMono(block, {.fade = fade, .parallel = parallel}, deluge::synth_noise);
		return block;
	}
	std::vector<int32_t> stereo(const std::vector<int32_t>& in) {
		std::vector<int32_t> block = in;
		set.renderStereo(block, {.fade = fade, .parallel = parallel}, deluge::synth_noise);
		return block;
	}
};

/// The 24dB ladder and the high-pass ladder, high to low, mono.
const std::vector<int32_t> kRouteH2LMono{
    0,         320504,    2227516,   7136192,   14051576,  18967076,  18214740,  12038576,  4188864,   -2029208,
    -6905984,  -13251540, -22375464, -31542948, -35761848, -32288756, -23338436, -14103380, -7964736,  -3747052,
    2217272,   11583012,  21851948,  28720220,  30270144,  28478468,  26465184,  24582104,  19808168,  9280184,
    -5778968,  -20019196, -28087308, -29021748, -26605980, -25332564, -26110984, -25946784, -21630932, -13391304,
    -4628036,  1861220,   6862116,   13554960,  23378692,  33415172,  38290468,  34874432,  25072816,  13910852,
    4915444,   -2447728,  -10789572, -20718240, -29213136, -32265660, -29077316, -23290096, -19688488, -19582000,
    -19595136, -15138068, -5022000,  7446256};

/// The same in stereo.
const std::vector<int32_t> kRouteH2LStereo{
    0,         319620,    419464,    2115764,   2561028,   6476952,   7410280,   12697240,  14024816,  18358924,
    19433956,  19844300,  18957708,  13037764,  8593308,   -3389904,  -11112628, -25856544, -34635852, -47164124,
    -53231868, -58224408, -57603872, -51781968, -43020608, -27241460, -12325984, 8821304,   25434116,  45049516,
    57572824,  68517056,  72013360,  69909884,  62587108,  47606240,  31458452,  8152376,   -11777724, -35970740,
    -53327436, -70602852, -79643192, -83910908, -81156280, -70099744, -56095876, -33006948, -12361240, 15160872,
    35776964,  58828032,  72319060,  82949540,  83964040,  78158252,  66096148,  46027992,  25274624,  -1997860,
    -24502116, -49890388, -66738148, -81717960};

/// The 24dB ladder and the high-pass ladder, low to high, mono.
const std::vector<int32_t> kRouteL2HMono{
    0,         324224,    2254208,   7220352,   14215424,  19208320,  18521216,  12399872,  4614016,   -1518848,
    -6325888,  -12676224, -21943680, -31462016, -36225664, -33335808, -24739712, -15343744, -8418432,  -2961792,
    4333440,   14640896,  24964736,  30781056,  30443776,  26640000,  23343104,  21498496,  18159360,  9963648,
    -2788480,  -15701120, -24017152, -26769152, -27040896, -28218240, -30239616, -29651712, -23411584, -12471552,
    -1241216,  6580992,   11358976,  16508672,  24139520,  32049280,  35459456,  31647104,  22645504,  13254016,
    6418432,   871296,    -6615552,  -16984960, -27159552, -32634880, -31802624, -27426048, -23671168, -21821056,
    -19090688, -11822208, 188928,    12891776};

/// The same in stereo.
const std::vector<int32_t> kRouteL2HStereo{
    0,         323328,    425728,    2141568,   2603264,   6555136,   7539200,   12840576,  14267136,  18534528,
    19746304,  19970304,  19218048,  13021184,  8668416,   -3594496,  -11270784, -26241408, -35034752, -47827072,
    -54050304, -59422336, -59070592, -53508224, -44861056, -28803072, -13590016, 8423680,   25568384,  45990272,
    58661376,  69401856,  72055424,  68214144,  58935552,  41146624,  22538752,  -3366528,  -25268352, -50809216,
    -69115648, -86411008, -95520256, -98770688, -95254272, -82062720, -66238080, -39988992, -16717952, 14016896,
    37003392,  61623168,  75862272,  84859392,  84243840,  73516800,  58135680,  31909760,  8035584,   -23863552,
    -47785472, -74918656, -91374464, -105536000};

/// The 24dB ladder and the high-pass ladder in parallel, mono.
const std::vector<int32_t> kRouteParallelMono{
    0,         23667436,  30451688,  22575648,  17055952,  25736772,  41295036,  45274844,  27784140,  -2100228,
    -24823272, -29841956, -25512776, -28395416, -44670060, -62770568, -64305112, -43580936, -14427732, 4041128,
    6242496,   4620772,   15450644,  39653464,  60667644,  61865276,  43525116,  22583136,  15179012,  19816932,
    20066456,  2916672,   -27254056, -51989560, -56673016, -45213372, -34905812, -37622508, -47547600, -47052788,
    -25950124, 6194804,   29476164,  33598816,  27316260,  28377956,  43280156,  59377848,  58658148,  36757516,
    7832668,   -9161980,  -9534500,  -6566576,  -16155972, -39744816, -61996692, -65643972, -49485100, -29382628,
    -21084744, -24252564, -23254556, -4555416};

/// The same in stereo.
const std::vector<int32_t> kRouteParallelStereo{
    0,         23666508,  31292708,  22550720,  13626388,  19325408,  34715388,  46038300,  36210348,  14075464,
    -6013808,  -8427836,  300112,    153304,    -14986932, -43368436, -57491692, -56147084, -39615604, -34547116,
    -40423980, -52300752, -48437724, -24951636, 4017264,   23600380,  23227156,  21617188,  27257040,  50240820,
    67191732,  68218628,  46888276,  22412440,  10917628,  10806776,  9673008,   -11423964, -41366448, -69177112,
    -71806236, -61103060, -47992864, -48677424, -55051512, -49742764, -26428688, 11269796,  35749124,  46077968,
    39401620,  43185036,  55525080,  72366916,  70062500,  45740272,  12682236,  -12080376, -16742912, -21608680,
    -33047940, -61939960, -82808004, -86790968};

/// An SVF band low-pass and an SVF notch high-pass, high to low.
const std::vector<int32_t> kSvfPair{
    0,         7174170,   17762418,  20841480,  12314418,  -1068312,  -8864772, -8418993,  -6818493,  -12651603,
    -25503819, -34912908, -30922359, -13916103, 4646232,   13795584,  13474983, 12308634,  18327603,  29643483,
    35961159,  29755578,  14294208,  1107150,   -2275473,  712419,    15147,    -10364979, -25270989, -33538725,
    -29824617, -19372794, -12734208, -14238543, -17556324, -12780105, 2510805,  19598379,  27662637,  24609372,
    18132651,  18041952,  25334892,  30737766,  24629388,  7497636,   -9624480, -16822974, -14647554, -12197175,
    -17760702, -29326404, -36185823, -30396768, -14963550, -1376688,  2638674,  276837,    1336398,   11684313,
    26200314,  33894126,  29587470,  18685281};

/// The drive ladder alone.
const std::vector<int32_t> kDriveAlone{
    0,         242192,    2447147,   8371694,   15938415,  21431079,  24316585,  26136258,  28016953,  29734042,
    30589662,  30231834,  27869505,  21553706,  10301006,  -3234542,  -15855195, -24911043, -29550542, -31285325,
    -31607826, -31021171, -29471025, -26815791, -22281343, -12850224, 4518369,   21159508,  28451284,  30569524,
    31008083,  30949587,  30744981,  30089480,  27722931,  20108945,  2962048,   -15163675, -24410326, -28159781,
    -30048342, -31158048, -31572369, -31119067, -28984770, -23112512, -12014632, 1965631,   15264139,  24786042,
    29571153,  31323495,  31642172,  31053656,  29494977,  26815575,  22227625,  12669857,  -4846848,  -21402178,
    -28553531, -30610804, -31025862, -30953792};

/// An SVF band high-pass alone.
const std::vector<int32_t> kHighBandAlone{
    0,         24617124,  27982770,  12053898,  -3610515,  -3120663,  9036069,   13312095,  -3238053,  -32049759,
    -50750154, -45614739, -25354563, -11250105, -14471265, -24744615, -21904968, 1951143,   33186894,  49784223,
    43355580,  27336747,  22074975,  32809308,  44285385,  37486257,  10363068,  -19499004, -32372613, -26080944,
    -17600841, -24220653, -44273694, -58330008, -49841904, -22923495, 1944231,   8739903,   1765344,   417930,
    17770155,  45621237,  62015943,  53794524,  30384627,  13508553,  14470728,  23080200,  19226721,  -5064903,
    -36315273, -52594071, -45590832, -28862508, -22882941, -32986611, -43990881, -36912276, -9666258,  20213118,
    33029928,  26622375,  17986563,  24435450};

/// The 12dB ladder and the high-pass ladder, low to high, stereo.
const std::vector<int32_t> kLp12HighLadderStereo{
    0,          -2054272,   -2694144,   -4311552,   -4290944,   1815808,    3193856,    14255872,  16114304,  27594496,
    33005184,   41132288,   45481728,   39254912,   33566848,   12133504,   -1242752,   -28333568, -46543872, -73674880,
    -92263808,  -108477312, -114170368, -108514304, -97139328,  -72836224,  -48954880,  -12309376, 18927232,  56246272,
    81450368,   101851648,  108958080,  104840064,  92070016,   65202432,   34782720,   -6389504,  -43366912, -81822720,
    -110869632, -135848576, -152062208, -158524544, -155503616, -135187712, -110104320, -69459712, -34133632, 10140160,
    45085824,   81105024,   104843136,  117437056,  115405824,  96112128,   69660544,   30597120,  -5846912,  -49635584,
    -85722624,  -121968512, -145766016, -160493312};

/// The second block after the low-pass changes from the 24dB ladder to the SVF band.
const std::vector<int32_t> kFamilyChange{
    0,         6656979,   18345279,  26343945,  25846788,  20233512,  16895589,  18718020,  20535750,  14658087,
    -315885,   -16725600, -25397787, -24335466, -19492989, -18748002, -23433351, -26925714, -21946419, -8678115,
    4801938,   11004384,  10391934,  10121589,  16321821,  26947689,  33429039,  29555409,  17662512,  6603639,
    2493486,   2822751,   -112050,   -10558356, -23949807, -31357992, -28652904, -20165949, -14422365, -15070959,
    -17178288, -12739650, 354846,    15296448,  23297952,  22355937,  18295836,  18642288,  24294246,  28302117,
    23273850,  9493740,   -4684734,  -11492370, -11232135, -10972245, -16879968, -27064452, -33142386, -29024820,
    -17109387, -6229449,  -2397978,  -2999106};

/// The block after both filters were off for a block and come back on, faded in.
const std::vector<int32_t> kAfterOff{
    0,         24282666,  31903264,  23533612,  14981710,  19733616,  34427454,  42592628,  32322824,  8975714,
    -10282834, -14077528, -7884252,  -7065466,  -19756856, -37822854, -45174960, -34600536, -15215580, -2744642,
    -3358290,  -8033726,  -3041732,  14639226,  33592606,  39716186,  30569622,  17390460,  12914042,  18009180,
    21417470,  12398006,  -7468292,  -25419522, -30330462, -23757630, -17462460, -20455408, -29485008, -32614694,
    -22149520, -3140556,  11766356,  15179106,  11853056,  12763506,  22611586,  34359828,  36691172,  25885076,
    9680002,   -613394,   -1511072,  -128008,   -5691806,  -19288262, -32351872, -35319386, -27516336, -17545080,
    -13909570, -16247162, -16077862, -6262020};

/// From a reset set, the block after a block with both filters off: faded in.
const std::vector<int32_t> kOffThenOn{
    0,         24282582,  31902180,  23528960,  14971458,  19721104,  34422202,  42603328,  32348756,  9006450,
    -10258084, -14060832, -7870530,  -7053084,  -19754242, -37841504, -45214650, -34643458, -15238716, -2740124,
    -3341264,  -8028558,  -3059696,  14608466,  33567920,  39705398,  30565302,  17385360,  12917332,  18042204,
    21489924,  12488974,  -7401490,  -25409072, -30371916, -23815320, -17501068, -20466930, -29486716, -32624022,
    -22164972, -3149254,  11765152,  15164638,  11802810,  12683404,  22543170,  34351790,  36759162,  25997622,
    9784346,   -550504,   -1486224,  -120096,   -5692248,  -19308062, -32399254, -35376110, -27543946, -17519856,
    -13851364, -16209928, -16106736, -6355960};

/// The SVFs' block after both slots changed family one block into a fade, the fade carried on.
const std::vector<int32_t> kFadeCarried{
    0,         15985564,  24248462,  21043668,  13131624,  10012016,  13813068,  17012006,  10637778,  -5778352,
    -22784948, -29709196, -25052176, -16971284, -14214818, -17428346, -18746844, -10790552, 5078984,   19433950,
    23860898,  19416810,  14776844,  16863858,  23672784,  26384988,  18730100,  3629898,   -9395638,  -13555262,
    -11598932, -11921132, -19203436, -29155138, -32618642, -25025992, -11125178, -411076,   2232564,   1327800,
    5063956,   16324866,  29086698,  34036738,  28085728,  17171516,  10086318,  9689964,   10225350,  4145712,
    -9623098,  -23814770, -29666210, -25850792, -19093666, -16939238, -19989056, -21399836, -14451138, 107948,
    14136586,  20002492,  18357038,  16468010};

/// The SVFs from a reset set, no fade: what the carried fade must differ from.
const std::vector<int32_t> kFadeNotCarriedReference{
    0,         6678858,   17135637,  21337893,  14668581,  2356461,   -6130428, -7558821, -7728939,  -14507367,
    -28091376, -39175602, -38064621, -23825286, -5855097,  5210925,   8139651,  10119879, 18388881,  31601178,
    40510530,  37757394,  25222236,  12763788,  7978056,   8620761,   5844426,  -6088560, -22798923, -33947844,
    -34073337, -27072825, -22144800, -23848893, -26955864, -22212990, -6689013, 11976114, 23281383,  24443136,
    21643854,  23647422,  32046852,  38824032,  34764369,  19202010,  1577286,  -8291316, -9879042,  -10803603,
    -18580497, -31864836, -41125641, -38663559, -26128707, -13301943, -7901412, -7887426, -4643862,  7404222,
    23864697,  34545384,  34133052,  26661915};

/// The block after both SVFs change from band to notch: the high-pass's memories cleared, the low-pass's kept.
const std::vector<int32_t> kSvfModeChange{
    26815131,  24765681,  14938650,  3600900,   -4044723,  -5879931,  -3330894,  -63696,    263079,    -4082946,
    -12256389, -21413754, -28331511, -30822438, -27598035, -18700764, -5472978,  9764046,   23897952,  33989040,
    38430039,  37521450,  32702787,  25582155,  17298795,  8394324,   -560157,   -8935593,  -16206696, -22128063,
    -26923314, -30697458, -32737320, -31873398, -27424260, -19726656, -9850296,  716505,    10655973,  19558263,
    27230889,  32914929,  35567934,  34604382,  30318117,  23468124,  14931669,  5472921,   -4579563,  -14700036,
    -23792331, -30560043, -34118001, -34461708, -32041491, -27179370, -20067531, -10969875, -699912,   9486648,
    18302829,  24934218,  29453898,  32169321};

/// SVFs in both slots from a new set.
const std::vector<int32_t> kSvfBothFresh{
    0,         6678858,   17135637,  21337893,  14668581,  2356461,   -6130428, -7558821, -7728939,  -14507367,
    -28091376, -39175602, -38064621, -23825286, -5855097,  5210925,   8139651,  10119879, 18388881,  31601178,
    40510530,  37757394,  25222236,  12763788,  7978056,   8620761,   5844426,  -6088560, -22798923, -33947844,
    -34073337, -27072825, -22144800, -23848893, -26955864, -22212990, -6689013, 11976114, 23281383,  24443136,
    21643854,  23647422,  32046852,  38824032,  34764369,  19202010,  1577286,  -8291316, -9879042,  -10803603,
    -18580497, -31864836, -41125641, -38663559, -26128707, -13301943, -7901412, -7887426, -4643862,  7404222,
    23864697,  34545384,  34133052,  26661915};

/// The 24dB ladder in parallel with the high-pass off: the low-pass plus the dry block.
const std::vector<int32_t> kParallelHighOff{
    0,         24873013,  34969933,  32135040,  31737134,  44435995,  63686697,  71999126,  58950054,  31957325,
    8617786,   -1115659,  -4126675,  -15164125, -39379957, -65132178, -74968920, -63310610, -42133725, -28576541,
    -27488843, -27226243, -13016522, 16299049,  45852886,  59235318,  54633705,  45643447,  46013922,  54574532,
    56408061,  39091337,  6787160,   -22839425, -35737693, -34724252, -35079602, -47235523, -64905789, -71271342,
    -56690950, -29142493, -6366942,  1992263,   3431343,   13308329,  37163813,  63430594,  74400996,  64013078,
    43736165,  30399689,  28837095,  27640503,  12412826,  -17613697, -47318638, -60283134, -54904217, -45103931,
    -44939558, -53419764, -55625217, -38958945};

/// Near full scale in parallel, both filters open, mono: the sum wraps.
const std::vector<int32_t> kParallelLoudMono{
    -558336276, 1366994056, -1836729432, -253448700, 1472996028, 208786604, -1235871128, -525449344,
    1551529096, 244059880,  -1320180448, -420494900, 1453148988, 336325400, -1403195988, -359991592,
    1397208608, 372689016,  -1409447776, -347312960, 1381898224, 377895312, -1417742408, -347580236,
    1389768764, 370881964,  -1410298244, -351733516, 1392702388, 368452316, -1406572864, -357115108,
    1397534008, 364571908,  -1408182996, -358111740, 1405513552, 361278028, -1406842832, -356753124,
    1408728924, 362642392,  -1408895244, -359523644, 1402239740, 358767048, -1406307572, -358176476,
    1408785944, 359571384,  -1406351068, -360620752, 1405967884, 360712112, -1402600000, -359279480,
    1400710364, 361223136,  -1397195376, -360131900, 1400419300, 362981272, -1402483160, -361771104};

/// The same in stereo.
const std::vector<int32_t> kParallelLoudStereo{
    -558336276,  -561830260, -940011092, -943787120, 1748769012, 1763258752, -2133178484, -2143174828,
    -1138509448, 836678676,  -574555868, -103882660, 1528288204, 1175222072, -2010248764, -1735769872,
    766413168,   557813644,  -77270412,  81539308,   1157579468, 1031040240, -1740052040, -1634205380,
    562954108,   498843476,  75110448,   125007800,  1047301200, 999634016,  -1653575696, -1609837916,
    499918272,   483467004,  131760828,  133677928,  1016517372, 987039368,  -1628921548, -1604738556,
    473530264,   475402060,  152093068,  136794560,  1003728328, 980968224,  -1622393208, -1601686872,
    464116624,   471428848,  157010984,  142469132,  994812720,  981737324,  -1613091572, -1598533984,
    463203436,   473731520,  152168668,  144994760,  989551904,  981355772,  -1613076840, -1594232712};

} // namespace

// clang-format off
describe filter_set("FilterSet", ${
	it("runs the high-pass then the low-pass, in mono exactly", _ {
		deluge::synth_noise.seed(kSeed);
		Rig rig;
		const int32_t gain = rig.configure(kLow24, kHighLadder, FilterRoute::HIGH_TO_LOW);
		expect(rig.mono(kInput)).to_equal(kRouteH2LMono);
		expect(gain).to_equal(536253356);
	});
	it("runs the high-pass then the low-pass, in stereo exactly", _ {
		deluge::synth_noise.seed(kSeed);
		Rig rig;
		const int32_t gain = rig.configure(kLow24, kHighLadder, FilterRoute::HIGH_TO_LOW);
		expect(rig.stereo(kInput)).to_equal(kRouteH2LStereo);
		expect(gain).to_equal(536253356);
	});
	it("runs the low-pass then the high-pass, in mono exactly", _ {
		deluge::synth_noise.seed(kSeed);
		Rig rig;
		const int32_t gain = rig.configure(kLow24, kHighLadder, FilterRoute::LOW_TO_HIGH);
		expect(rig.mono(kInput)).to_equal(kRouteL2HMono);
		expect(gain).to_equal(536253356);
	});
	it("runs the low-pass then the high-pass, in stereo exactly", _ {
		deluge::synth_noise.seed(kSeed);
		Rig rig;
		const int32_t gain = rig.configure(kLow24, kHighLadder, FilterRoute::LOW_TO_HIGH);
		expect(rig.stereo(kInput)).to_equal(kRouteL2HStereo);
		expect(gain).to_equal(536253356);
	});
	it("sums the two filters run side by side, in mono exactly", _ {
		deluge::synth_noise.seed(kSeed);
		Rig rig;
		const int32_t gain = rig.configure(kLow24, kHighLadder, FilterRoute::PARALLEL);
		expect(rig.mono(kInput)).to_equal(kRouteParallelMono);
		expect(gain).to_equal(536253356);
	});
	it("sums the two filters run side by side, in stereo exactly", _ {
		deluge::synth_noise.seed(kSeed);
		Rig rig;
		const int32_t gain = rig.configure(kLow24, kHighLadder, FilterRoute::PARALLEL);
		expect(rig.stereo(kInput)).to_equal(kRouteParallelStereo);
		expect(gain).to_equal(536253356);
	});
	it("runs an SVF in each slot exactly, the gain only scaled", _ {
		deluge::synth_noise.seed(kSeed);
		Rig rig;
		const int32_t gain = rig.configure(kLowBand, kHighNotch);
		expect(rig.mono(kInput)).to_equal(kSvfPair);
		expect(gain).to_equal(430000000);
	});
	it("runs the drive ladder alone exactly", _ {
		deluge::synth_noise.seed(kSeed);
		Rig rig;
		const int32_t gain = rig.configure(kLowDrive, kHighOff);
		expect(rig.mono(kInput)).to_equal(kDriveAlone);
		expect(gain).to_equal(343999998);
	});
	it("runs an SVF high-pass alone exactly", _ {
		deluge::synth_noise.seed(kSeed);
		Rig rig;
		const int32_t gain = rig.configure(kLowOff, kHighBand);
		expect(rig.mono(kInput)).to_equal(kHighBandAlone);
		expect(gain).to_equal(430000000);
	});
	it("passes the block through with both off, still scaling the gain", _ {
		deluge::synth_noise.seed(kSeed);
		Rig rig;
		const int32_t gain = rig.configure(kLowOff, kHighOff);
		expect(rig.mono(kInput)).to_equal(kInput);
		expect(gain).to_equal(430000000);
	});
	it("runs the 12dB ladder and the high-pass ladder low to high, in stereo exactly", _ {
		deluge::synth_noise.seed(kSeed);
		Rig rig;
		const int32_t gain = rig.configure(kLow12, kHighLadder, FilterRoute::LOW_TO_HIGH);
		expect(rig.stereo(kInput)).to_equal(kLp12HighLadderStereo);
		expect(gain).to_equal(536253356);
	});
	it("resets the low-pass without a fade when its family changes", _ {
		deluge::synth_noise.seed(kSeed);
		Rig rig;
		std::ignore = rig.configure(kLow24, kHighOff);
		std::ignore = rig.mono(kInput);
		const int32_t gain = rig.configure(kLowBand, kHighOff);
		expect(rig.mono(kInput)).to_equal(kFamilyChange);
		expect(gain).to_equal(430000000);
	});
	it("starts over with no fade after reset(), as a new set does", _ {
		deluge::synth_noise.seed(kSeed);
		Rig rig;
		std::ignore = rig.configure(kLow24, kHighLadder);
		std::ignore = rig.mono(kInput);
		std::ignore = rig.mono(kInput);
		rig.set.reset();
		deluge::synth_noise.seed(kSeed);
		std::ignore = rig.configure(kLow24, kHighLadder);
		expect(rig.mono(kInput)).to_equal(kRouteH2LMono);
	});
	it("fades both filters in when they come back on from off", _ {
		deluge::synth_noise.seed(kSeed);
		Rig rig;
		std::ignore = rig.configure(kLow24, kHighLadder);
		std::ignore = rig.mono(kInput);
		std::ignore = rig.mono(kInput);
		std::ignore = rig.configure(kLowOff, kHighOff);
		std::ignore = rig.mono(kInput);
		deluge::synth_noise.seed(kSeed);
		std::ignore = rig.configure(kLow24, kHighLadder);
		expect(rig.mono(kInput)).to_equal(kAfterOff);
	});
	it("fades in from off after reset(), where reset() itself fades nothing", _ {
		deluge::synth_noise.seed(kSeed);
		Rig rig;
		std::ignore = rig.configure(kLowOff, kHighOff);
		std::ignore = rig.mono(kInput);
		std::ignore = rig.configure(kLow24, kHighLadder);
		expect(rig.mono(kInput)).to_equal(kOffThenOn);
	});
	it("carries a fade under way across a family change in each slot", _ {
		deluge::synth_noise.seed(kSeed);
		Rig rig;
		std::ignore = rig.configure(kLowOff, kHighOff);
		std::ignore = rig.mono(kInput);
		std::ignore = rig.configure(kLow24, kHighLadder);
		std::ignore = rig.mono(kInput);
		std::ignore = rig.configure(kLowBand, kHighBand);
		const std::vector<int32_t> carried = rig.mono(kInput);
		expect(carried).to_equal(kFadeCarried);
		deluge::synth_noise.seed(kSeed);
		Rig fresh;
		std::ignore = fresh.configure(kLowBand, kHighBand);
		const std::vector<int32_t> unfaded = fresh.mono(kInput);
		expect(unfaded).to_equal(kFadeNotCarriedReference);
		expect(carried).not_().to_equal(unfaded);
	});
	it("clears the high-pass SVF on any mode change and keeps the low-pass SVF within its family", _ {
		deluge::synth_noise.seed(kSeed);
		Rig rig;
		std::ignore = rig.configure(kLowBand, kHighBand);
		std::ignore = rig.mono(kInput);
		std::ignore = rig.configure(kLowNotch, kHighNotch);
		expect(rig.mono(kInput)).to_equal(kSvfModeChange);
	});
	it("starts over after reset() with SVFs in both slots, as a new set does", _ {
		deluge::synth_noise.seed(kSeed);
		Rig fresh;
		std::ignore = fresh.configure(kLowBand, kHighBand);
		expect(fresh.mono(kInput)).to_equal(kSvfBothFresh);
		deluge::synth_noise.seed(kSeed);
		Rig rig;
		std::ignore = rig.configure(kLowBand, kHighBand);
		std::ignore = rig.mono(kInput);
		rig.set.reset();
		deluge::synth_noise.seed(kSeed);
		std::ignore = rig.configure(kLowBand, kHighBand);
		expect(rig.mono(kInput)).to_equal(kSvfBothFresh);
	});
	it("adds the dry block in parallel when the high-pass is off", _ {
		deluge::synth_noise.seed(kSeed);
		Rig rig;
		std::ignore = rig.configure(kLow24, kHighOff, FilterRoute::PARALLEL);
		expect(rig.mono(kInput)).to_equal(kParallelHighOff);
	});
	it("wraps the parallel sum near full scale rather than saturating, in mono", _ {
		deluge::synth_noise.seed(kSeed);
		Rig rig;
		std::ignore = rig.configure(kLow12, kHighLadder, FilterRoute::PARALLEL, kOpenLow, kOpenHigh);
		expect(rig.mono(kLoud)).to_equal(kParallelLoudMono);
	});
	it("wraps the parallel sum near full scale rather than saturating, in stereo", _ {
		deluge::synth_noise.seed(kSeed);
		Rig rig;
		std::ignore = rig.configure(kLow12, kHighLadder, FilterRoute::PARALLEL, kOpenLow, kOpenHigh);
		expect(rig.stereo(kLoud)).to_equal(kParallelLoudStereo);
	});
	it("takes no fade scratch on blocks neither filter fades, rendering as with it", _ {
		const auto render = [&](LowPassSlot low, HighPassSlot high, FilterRoute route, bool stereo) {
			deluge::synth_noise.seed(kSeed);
			Rig rig;
			rig.fade.clear();
			std::ignore = rig.configure(low, high, route);
			expect(FadeIn::active(rig.set.view().low_fade)).to_be_false();
			expect(FadeIn::active(rig.set.view().high_fade)).to_be_false();
			return stereo ? rig.stereo(kInput) : rig.mono(kInput);
		};
		expect(render(kLow24, kHighLadder, FilterRoute::HIGH_TO_LOW, false)).to_equal(kRouteH2LMono);
		expect(render(kLow24, kHighLadder, FilterRoute::HIGH_TO_LOW, true)).to_equal(kRouteH2LStereo);
		expect(render(kLowBand, kHighNotch, FilterRoute::HIGH_TO_LOW, false)).to_equal(kSvfPair);
		expect(render(kLow24, kHighLadder, FilterRoute::PARALLEL, true)).to_equal(kRouteParallelStereo);
	});
	it("reports which filters are on", _ {
		Rig rig;
		expect(rig.set.isOn()).to_be_false();
		std::ignore = rig.configure(kLow24, kHighOff);
		expect(rig.set.isOn()).to_be_true();
		expect(rig.set.isLowPassOn()).to_be_true();
		expect(rig.set.isHighPassOn()).to_be_false();
		std::ignore = rig.configure(kLowOff, kHighBand);
		expect(rig.set.isLowPassOn()).to_be_false();
		expect(rig.set.isHighPassOn()).to_be_true();
		rig.set.reset();
		expect(rig.set.isOn()).to_be_false();
	});
});
// clang-format on

CPPSPEC_MAIN(filter_set);
