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

// DelayTape: the read and write heads of the delay and the stutterer, native or resampling, over storage the tape does
// not own. Exact reference values, recorded from the app's tape sizing, pin what each caller's call sequence leaves
// in the storage and reads back: the delay's secondary tape (clear-as-you-read, write behind), its primary tape (read
// from a saved head, write back from it), and the stutterer recording then playing forward, backward and ping-pong,
// native and resampled. A fresh tape's storage holds a known fill, so the samples the tape clears are pinned too.
// Long runs are pinned by a 64-bit FNV-1a digest of every word, with their opening samples spelled out.
//
// The stutter mirror below follows Stutterer::processStutter (src/deluge/model/fx/stutterer.cpp) call for call; a
// change to that function's use of its tape is a change to this mirror.

#include "dsp-next/fixed/delay/delay_tape.h"

#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

using deluge::dsp::StereoSample;
using deluge::dsp::fixed::DelayTape;

namespace {

/// The fill a fresh allocation holds: word k of the storage.
inline int32_t pattern(size_t word) {
	return static_cast<int32_t>(0x5A5A0000u + static_cast<uint32_t>(word));
}

/// The k-th input sample: two unrelated pseudo-random channels, 28 bits of magnitude.
inline StereoSample input(int32_t k) {
	const uint32_t a = static_cast<uint32_t>(k + 1) * 2654435761u;
	const uint32_t b = static_cast<uint32_t>(k + 7) * 2246822519u;
	return {.l = static_cast<int32_t>(a) >> 3, .r = static_cast<int32_t>(b) >> 3};
}

/// A 64-bit FNV-1a over 32-bit words.
inline uint64_t digest(const std::vector<int32_t>& words) {
	uint64_t h = 14695981039346656037ull;
	for (int32_t w : words) {
		h = (h ^ static_cast<uint32_t>(w)) * 1099511628211ull;
	}
	return h;
}

template <typename T>
std::vector<int32_t> storage(const T& tape) {
	std::vector<int32_t> words;
	for (const StereoSample* s = tape.begin(); s != tape.end(); ++s) {
		words.push_back(s->l);
		words.push_back(s->r);
	}
	return words;
}

/// What a run produced: the samples read, the strengths each advance returned, and where the tape reported a wrap.
struct Run {
	std::vector<int32_t> out;
	std::vector<int32_t> strengths;
	std::vector<int32_t> wraps;
	int32_t k = 0; ///< inputs consumed
};

inline void push(Run& run, StereoSample s) {
	run.out.push_back(s.l);
	run.out.push_back(s.r);
}

/// The delay's secondary tape for one block: the read head clears as it goes, the input written behind it.
template <typename T>
void secondaryBlock(T& tape, int32_t rate, int32_t n, Run& run) {
	tape.setup_for_render(rate);
	if (tape.is_native()) {
		for (int32_t i = 0; i < n; ++i, ++run.k) {
			if (tape.clear_and_move_on()) {
				run.wraps.push_back(run.k);
			}
			tape.write_native(input(run.k));
		}
		return;
	}
	for (int32_t i = 0; i < n; ++i, ++run.k) {
		const int32_t strength2 = tape.advance([&] {
			if (tape.clear_and_move_on()) {
				run.wraps.push_back(run.k);
			}
		});
		run.strengths.push_back(strength2);
		tape.write_resampled(input(run.k), 65536 - strength2, strength2);
	}
}

/// The delay's primary tape for one block: read (clearing) from the saved head, then the input plus half the read
/// written back from that head.
template <typename T>
void primaryBlock(T& tape, int32_t rate, int32_t n, Run& run) {
	tape.setup_for_render(rate);
	const auto head = tape.head();
	std::vector<StereoSample> work(n);
	bool wrapped = false;
	if (tape.is_native()) {
		for (StereoSample& sample : work) {
			wrapped = tape.clear_and_move_on() || wrapped;
			sample = tape.current();
		}
	}
	else {
		for (StereoSample& sample : work) {
			const int32_t strength2 = tape.advance([&] { wrapped = tape.clear_and_move_on() || wrapped; });
			run.strengths.push_back(strength2);
			const int32_t strength1 = 65536 - strength2;
			StereoSample* next = &tape.current() + 1;
			if (next == tape.end()) {
				next = tape.begin();
			}
			const StereoSample a = tape.current();
			const StereoSample b = *next;
			sample.l = (multiply_32x32_rshift32(a.l, strength1 << 14) + multiply_32x32_rshift32(b.l, strength2 << 14))
			           << 2;
			sample.r = (multiply_32x32_rshift32(a.r, strength1 << 14) + multiply_32x32_rshift32(b.r, strength2 << 14))
			           << 2;
		}
	}
	for (StereoSample& sample : work) {
		push(run, sample);
		const StereoSample in = input(run.k++);
		sample = {.l = in.l + (sample.l >> 1), .r = in.r + (sample.r >> 1)};
	}
	if (wrapped) {
		run.wraps.push_back(run.k);
	}
	if (tape.is_native()) {
		StereoSample* write_pos = head.pos - T::kWriteLag;
		if (write_pos < tape.begin()) {
			write_pos += tape.size_including_extra();
		}
		for (StereoSample sample : work) {
			tape.write_native_and_move_on(sample, write_pos);
		}
	}
	else {
		tape.set_head(head);
		for (StereoSample sample : work) {
			const int32_t strength2 = tape.advance([&] { tape.move_on(); });
			tape.write_resampled(sample, 65536 - strength2, strength2);
		}
	}
}

/// The stutterer: records until its tape is full, then plays forward, backward, or turning at each end.
template <typename T>
struct Stutter {
	T& tape;
	bool reverse;
	bool ping_pong;
	int32_t size_left;
	bool recording = true;
	Run run{};
	std::vector<int32_t> turns{}; ///< the played samples after which the direction turned

	void block(int32_t rate, int32_t n) {
		tape.setup_for_render(rate);
		if (recording) {
			for (int32_t i = 0; i < n; ++i) {
				int32_t strength1 = 0;
				int32_t strength2 = 0;
				if (tape.is_native()) {
					tape.clear_and_move_on();
					size_left--;
				}
				else {
					strength2 = tape.advance([&] {
						tape.clear_and_move_on();
						size_left--;
					});
					strength1 = 65536 - strength2;
					run.strengths.push_back(strength2);
				}
				tape.write(input(run.k++), strength1, strength2);
			}
			if (size_left < 0) {
				tape.set_current(reverse ? tape.end() - 1 : tape.begin());
				recording = false;
			}
			return;
		}
		for (int32_t i = 0; i < n; ++i) {
			StereoSample sample{};
			if (tape.is_native()) {
				if (reverse) {
					tape.move_back();
				}
				else {
					tape.move_on();
				}
				sample = tape.current();
			}
			else {
				const int32_t strength2 =
				    reverse ? tape.retreat([&] { tape.move_back(); }) : tape.advance([&] { tape.move_on(); });
				run.strengths.push_back(strength2);
				const int32_t strength1 = 65536 - strength2;
				StereoSample* other = nullptr;
				if (reverse) {
					other = &tape.current() - 1;
					if (other < tape.begin()) {
						other = tape.end() - 1;
					}
				}
				else {
					other = &tape.current() + 1;
					if (other == tape.end()) {
						other = tape.begin();
					}
				}
				const StereoSample a = tape.current();
				const StereoSample b = *other;
				sample.l =
				    (multiply_32x32_rshift32(a.l, strength1 << 14) + multiply_32x32_rshift32(b.l, strength2 << 14))
				    << 2;
				sample.r =
				    (multiply_32x32_rshift32(a.r, strength1 << 14) + multiply_32x32_rshift32(b.r, strength2 << 14))
				    << 2;
			}
			push(run, sample);
			if (ping_pong
			    && ((reverse && &tape.current() == tape.begin()) || (!reverse && &tape.current() == tape.end() - 1))) {
				reverse = !reverse;
				turns.push_back(static_cast<int32_t>(run.out.size() / 2));
			}
		}
	}
};

/// A tape over its own storage, set up as the delay's buffer does: sized for @p rate, its native rate made precise
/// when the size was clamped, the storage holding the fill a fresh allocation does.
class Rig {
public:
	explicit Rig(uint32_t rate) {
		const auto [size, clamped] = DelayTape::ideal_size_for_rate(rate);
		storage_.resize(static_cast<size_t>(size) + DelayTape::kWriteLag);
		for (size_t i = 0; i < storage_.size(); ++i) {
			storage_[i] = {.l = pattern(2 * i), .r = pattern((2 * i) + 1)};
		}
		tape = DelayTape{storage_, static_cast<size_t>(size), rate};
		if (clamped) {
			tape.make_native_rate_precise();
		}
	}
	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	DelayTape tape;

private:
	std::vector<StereoSample> storage_;
};

/// The delay rate whose tape is 256 samples: 1 is 16777216, so this is 64 times the neutral rate.
constexpr uint32_t kNative = 1U << 30;
constexpr int32_t kFast = 1610612736;  ///< 1.5 times kNative
constexpr int32_t kSlow = 805306368;   ///< 0.75 times kNative
constexpr int32_t kQuick = 1342177280; ///< 1.25 times kNative

/// A fresh tape's first 26 samples, interleaved L/R: the 22 the tape clears, then the fill.
const std::vector<int32_t> kFreshHead{
    0, 0, 0, 0, 0, 0,          0,          0,          0,          0,          0,          0,          0,
    0, 0, 0, 0, 0, 0,          0,          0,          0,          0,          0,          0,          0,
    0, 0, 0, 0, 0, 0,          0,          0,          0,          0,          0,          0,          0,
    0, 0, 0, 0, 0, 1515847724, 1515847725, 1515847726, 1515847727, 1515847728, 1515847729, 1515847730, 1515847731};
constexpr uint64_t kFreshStorageDigest = 0x7bb6d2179ab765f9ull;

/// The delay's secondary tape: 128 samples native, then 384 resampled.
constexpr uint64_t kFastWriteStrengthDigest = 0x3eca21bfde128125ull;
constexpr uint64_t kFastWriteStorageDigest = 0x694e8c98d1d497cdull;
const std::vector<int32_t> kFastWriteWraps{213, 397};
const std::vector<int32_t> kFastWriteStrengths{32768, 0, 32768, 0, 32768, 0, 32768, 0, 32768, 0, 32768, 0,
                                               32768, 0, 32768, 0, 32768, 0, 32768, 0, 32768, 0, 32768, 0};
const std::vector<int32_t> kFastWriteStored{
    30040568,  -60592856, -17189248, -52314368, -64421568, -44037440, -22177800, -35757776, 20069496,  -27479272,
    62315264,  -19202144, 104562152, -10922672, 102066792, -2644184,  54836424,  5633144,   7605216,   13912432,
    5109864,   22190912,  47353480,  30468424,  121304,    38747520,  -47108520, 47026016,  -94341048, 55303712,
    -52097064, 63582616,  -9849768,  71861112,  32395792,  80138992,  74642896,  88417720,  72147536,  96696208,
    24916936,  104974280, -22314032, 113252808, -24809384, 76788648,  17433992,  -4410200};

/// The delay's secondary tape: 128 samples native, then 384 resampled.
constexpr uint64_t kSlowWriteStrengthDigest = 0x7a317e5d95280125ull;
constexpr uint64_t kSlowWriteStorageDigest = 0xaf9ac6081ec375c1ull;
const std::vector<int32_t> kSlowWriteWraps{298};
const std::vector<int32_t> kSlowWriteStrengths{49152, 32768, 16384, 0, 49152, 32768, 16384, 0, 49152, 32768, 16384, 0,
                                               49152, 32768, 16384, 0, 49152, 32768, 16384, 0, 49152, 32768, 16384, 0};
const std::vector<int32_t> kSlowWriteStored{
    80738972,   -46463960, 75752584,  -29907736, -18710928, -13351504,  21041240,  3204716,   -73422272, 19760944,
    -33670096,  36317172,  50820628,  52873392,  45834240,  69429616,   -48629272, 85985848,  -8877104,  102542076,
    -103340616, 119098304, -63588444, 1438848,   20902280,  -116220608, 15915896,  -99664384, -78547612, -83108156,
    5943116,    -66551928, 90433848,  -49995704, 40708896,  -33439480,  -9016056,  -16883256, -14002448, -327028,
    -108465952, 16229200,  -23975228, 32785424,  60515504,  49341652,   10790552,  65897876};

/// The delay's secondary tape: 128 samples native, then 384 resampled.
constexpr uint64_t kQuarterWriteStrengthDigest = 0x261f9cf41d150125ull;
constexpr uint64_t kQuarterWriteStorageDigest = 0xd07526075cd93c59ull;
const std::vector<int32_t> kQuarterWriteWraps{};
const std::vector<int32_t> kQuarterWriteStrengths{16384, 32768, 49152, 0, 16384, 32768, 49152, 0,
                                                  16384, 32768, 49152, 0, 16384, 32768, 49152, 0,
                                                  16384, 32768, 49152, 0, 16384, 32768, 49152, 0};
const std::vector<int32_t> kQuarterWriteStored{
    0,         0,          -205066442, -181513944, 126738028,  99338871,   -78328414, -156679227, 253476056,  124173588,
    48409614,  -131844509, -156656828, 149008306,  175147642,  -107009791, -29918799, 173843024,  -234985241, -82175073,
    96819229,  198677742,  -108247213, -57340356,  223557257,  223512459,  18490815,  -32505638,  -186575627, 248347177,
    145228843, -7670920,   -59837598,  -263689017, -264904040, 17163798,   66900430,  -238854299, -138166012, 41998515,
    193638458, -214019582, -11427984,  66833233,   -216494426, -189184864, 115310044, 91667951};

/// The delay's secondary tape: 128 samples native, then 384 resampled.
constexpr uint64_t kNinefoldWriteStrengthDigest = 0xe9cd72f04db6ba50ull;
constexpr uint64_t kNinefoldWriteStorageDigest = 0x862f2d7456c5ac45ull;
const std::vector<int32_t> kNinefoldWriteWraps{334};
const std::vector<int32_t> kNinefoldWriteStrengths{19661, 39322, 58983, 13108, 32769, 52431, 6556,  26217,
                                                   45878, 3,     19665, 39326, 58987, 13112, 32773, 52435,
                                                   6560,  26221, 45882, 7,     19669, 39330, 58991, 13116};
const std::vector<int32_t> kNinefoldWriteStored{
    -10484048,  93474680,  -3671032,   94809704,  2124576,    86629904,  9534792,    93549928,  12439728,   98817944,
    4822536,    100152984, -2794648,   101488008, -10411832,  102823048, -18029024,  104158080, -25646216,  105493112,
    -33263408,  106828144, -31033136,  110760688, -43564968,  110799360, -56094424,  110838080, -63715656,  112171424,
    -71332864,  113506456, -78950048,  114841496, -86567232,  116176520, -94184424,  117511552, -101801616, 118846592,
    -118642328, 105777464, -122255800, 113365000, -125869264, 120952528, -122208096, 114073192};

/// The delay's primary tape, native: five blocks of 128.
constexpr uint64_t kNativePrimaryOutDigest = 0x3c41738e75b2c035ull;
constexpr uint64_t kNativePrimaryStorageDigest = 0xe83c23219fb74eb3ull;
const std::vector<int32_t> kNativePrimaryWraps{256, 640};
const std::vector<int32_t> kNativePrimaryWindow{1515848222, 1515848223, 1515848224, 1515848225, 1515848226, 1515848227,
                                                1515848228, 1515848229, 1515848230, 1515848231, -205066442, -181513944,
                                                884661890,  857262733,  679595449,  601244636,  1011399920, 882097452,
                                                806333479,  626079356,  601267038,  906932172,  933071509,  650914076};

/// The delay's primary tape: a block native, four at 1.5 times, four at 0.75 times.
constexpr uint64_t kResampledPrimaryOutDigest = 0xb63856dd0091e0eaull;
constexpr uint64_t kResampledPrimaryStrengthDigest = 0xb2ca533ec9c5f325ull;
constexpr uint64_t kResampledPrimaryStorageDigest = 0x8f7e4e916365a721ull;
const std::vector<int32_t> kResampledPrimaryWraps{256, 512, 640, 896};
const std::vector<int32_t> kResampledPrimaryWindow{811213796, 718658344, 772049592, 603066660, 732885380, 755910424,
                                                   693721180, 908754196, 922992428, 793162504, 615392768, 677570820,
                                                   844664020, 830414584, 537064360, 983258352, 766335604, 867666664,
                                                   995606860, 752074976, 688007196, 636483288, 917278448, 520891600};

/// The strengths of 20 advances at 1.5 times, from a head 10 advances in.
const std::vector<int32_t> kHeadStrengths{32768, 0, 32768, 0, 32768, 0, 32768, 0, 32768, 0,
                                          32768, 0, 32768, 0, 32768, 0, 32768, 0, 32768, 0};

/// What a stutter left and played: the storage once recorded, every sample played, every strength, the storage at the
/// end, the first 8 samples played and the played samples after which a ping-pong turned.
struct StutterRecord {
	uint64_t recorded;
	uint64_t out;
	uint64_t strengths;
	uint64_t storage;
	std::vector<int32_t> first;
	std::vector<int32_t> turns;

	bool operator==(const StutterRecord&) const = default;
};

std::ostream& operator<<(std::ostream& out, const StutterRecord& r) {
	out << std::hex << "{recorded 0x" << r.recorded << ", out 0x" << r.out << ", strengths 0x" << r.strengths
	    << ", storage 0x" << r.storage << std::dec << ", first";
	for (int32_t v : r.first) {
		out << ' ' << v;
	}
	out << ", turns";
	for (int32_t v : r.turns) {
		out << ' ' << v;
	}
	return out << '}';
}

const StutterRecord kNativeForwardStutter{.recorded = 0x2740adf293b06f72ull,
                                          .out = 0x6ec7243691d3c9e7ull,
                                          .strengths = 0xcbf29ce484222325ull,
                                          .storage = 0x2740adf293b06f72ull,
                                          .first = {104912272, 24451633, -100154170, -231566464, 231650300, 49286351,
                                                    26583859, -206731746, -178482583, 74121069, 153321887, -181897028,
                                                    -51744555, 98955786, -256810997, -157062311},
                                          .turns = {}};
const StutterRecord kNativeReverseStutter{.recorded = 0x2740adf293b06f72ull,
                                          .out = 0xfc5795a71653bd85ull,
                                          .strengths = 0xcbf29ce484222325ull,
                                          .storage = 0x2740adf293b06f72ull,
                                          .first = {183240686, 255635013, -148563784, -25217802, 56502658, 230800295,
                                                    261569099, -50052520, -70235371, 205965577, 134831071, -74887238,
                                                    -196973399, 181130859, 8093043, -99721956},
                                          .turns = {}};
const StutterRecord kNativePingPongStutter{.recorded = 0x2740adf293b06f72ull,
                                           .out = 0x666683063f46962bull,
                                           .strengths = 0xcbf29ce484222325ull,
                                           .storage = 0x2740adf293b06f72ull,
                                           .first = {104912272, 24451633, -100154170, -231566464, 231650300, 49286351,
                                                     26583859, -206731746, -178482583, 74121069, 153321887, -181897028,
                                                     -51744555, 98955786, -256810997, -157062311},
                                           .turns = {275, 550}};
const StutterRecord kNativePingPongReverseStutter{
    .recorded = 0x2740adf293b06f72ull,
    .out = 0x39ae947f7c983f8bull,
    .strengths = 0xcbf29ce484222325ull,
    .storage = 0x2740adf293b06f72ull,
    .first = {183240686, 255635013, -148563784, -25217802, 56502658, 230800295, 261569099, -50052520, -70235371,
              205965577, 134831071, -74887238, -196973399, 181130859, 8093043, -99721956},
    .turns = {275, 550}};
const StutterRecord kFastForwardStutter{.recorded = 0x1e73d4c2347c287dull,
                                        .out = 0x470cf61d7e91ad99ull,
                                        .strengths = 0x4fd71b3d76667325ull,
                                        .storage = 0x1e73d4c2347c287dull,
                                        .first = {23954644, 90015528, -24524024, 102434872, -73002296, 114850624,
                                                  -32002872, 127266072, 31366340, 5465936, 94733968, -116334096,
                                                  23886524, -103918740, -46960552, -91502904},
                                        .turns = {}};
const StutterRecord kFastReverseStutter{.recorded = 0x1e73d4c2347c287dull,
                                        .out = 0x7677d1639e649249ull,
                                        .strengths = 0x4fd71b3d76667325ull,
                                        .storage = 0x1e73d4c2347c287dull,
                                        .first = {103531344, 48624440, 82409240, 44484584, -23207056, 23789248,
                                                  -44330728, 19649480, 29010840, -1045940, 52626216, -5185616, -8248388,
                                                  -25881136, -29371088, -30020712},
                                        .turns = {}};
const StutterRecord kFastPingPongStutter{.recorded = 0x1e73d4c2347c287dull,
                                         .out = 0x470cf61d7e91ad99ull,
                                         .strengths = 0x4fd71b3d76667325ull,
                                         .storage = 0x1e73d4c2347c287dull,
                                         .first = {23954644, 90015528, -24524024, 102434872, -73002296, 114850624,
                                                   -32002872, 127266072, 31366340, 5465936, 94733968, -116334096,
                                                   23886524, -103918740, -46960552, -91502904},
                                         .turns = {}};
const StutterRecord kQuickPingPongStutter{.recorded = 0xc5bd490591e4c35dull,
                                          .out = 0xc40a77c7f042e551ull,
                                          .strengths = 0xe28ca11b3067f325ull,
                                          .storage = 0xc5bd490591e4c35dull,
                                          .first = {-37614796, 10102176, 25755672, 22519824, 89123684, 34938304,
                                                    45118024, 47354728, -12306640, 59772376, -2627576, 72190016,
                                                    -33211136, 84608776, -104057280, 97024928},
                                          .turns = {220, 440, 660}};
const StutterRecord kSlowForwardStutter{.recorded = 0x78de7675c9225289ull,
                                        .out = 0x80fe616868562e4dull,
                                        .strengths = 0xfcf30aa915513d25ull,
                                        .storage = 0x78de7675c9225289ull,
                                        .first = {29281084, -41110288, 3172012, -28693120, -11752420, -16275952,
                                                  -49046128, -3858772, -86339848, 8558396, -56525720, 20975568, 6842328,
                                                  33392736, 70210372, 45809904},
                                        .turns = {}};
const StutterRecord kSlowReverseStutter{.recorded = 0x78de7675c9225289ull,
                                        .out = 0x1e0a9c8470e8b419ull,
                                        .strengths = 0x15d13511efaa3d25ull,
                                        .storage = 0x78de7675c9225289ull,
                                        .first = {-56421504, -90778960, 5734684, -111474244, 34336944, -65061692,
                                                  30597160, -119752360, 59199420, -6231964, 87801692, 107288440,
                                                  -17811720, 86593152, 45556328, 99010328},
                                        .turns = {}};
const StutterRecord kSlowPingPongReverseStutter{.recorded = 0x78de7675c9225289ull,
                                                .out = 0x30a5353a7b66d5e5ull,
                                                .strengths = 0xaa0a3b6a9873bd25ull,
                                                .storage = 0x78de7675c9225289ull,
                                                .first = {-56421504, -90778960, 5734684, -111474244, 34336944,
                                                          -65061692, 30597160, -119752360, 59199420, -6231964, 87801692,
                                                          107288440, -17811720, 86593152, 45556328, 99010328},
                                                .turns = {366, 732}};
const StutterRecord kRateRisesStutter{.recorded = 0x2740adf293b06f72ull,
                                      .out = 0x0ff7992373ff158eull,
                                      .strengths = 0x5f2be0817402df25ull,
                                      .storage = 0x606979d8baaf5f57ull,
                                      .first = {104912272, 24451633, -100154170, -231566464, 231650300, 49286351,
                                                26583859, -206731746, -178482583, 74121069, 153321887, -181897028,
                                                -51744555, 98955786, -256810997, -157062311},
                                      .turns = {}};

/// Records a stutter at @p record_rate, then plays six blocks at @p play_rate (after two at the record rate when
/// they differ).
StutterRecord stutter(int32_t record_rate, int32_t play_rate, bool reverse, bool ping_pong) {
	Rig rig{kNative};
	Stutter<DelayTape> s{.tape = rig.tape,
	                     .reverse = reverse,
	                     .ping_pong = ping_pong,
	                     .size_left = static_cast<int32_t>(rig.tape.size())};
	while (s.recording) {
		s.block(record_rate, 128);
	}
	const uint64_t recorded = digest(storage(rig.tape));
	if (play_rate != record_rate) {
		s.block(record_rate, 128);
		s.block(record_rate, 128);
	}
	for (int i = 0; i < 6; ++i) {
		s.block(play_rate, 128);
	}
	return {.recorded = recorded,
	        .out = digest(s.run.out),
	        .strengths = digest(s.run.strengths),
	        .storage = digest(storage(rig.tape)),
	        .first = {s.run.out.begin(), s.run.out.begin() + 16},
	        .turns = s.turns};
}

/// The delay's secondary tape: 128 samples native, then three blocks of 128 at @p rate.
Run secondary(Rig& rig, uint32_t native, int32_t rate) {
	Run run;
	secondaryBlock(rig.tape, static_cast<int32_t>(native), 128, run);
	for (int i = 0; i < 3; ++i) {
		secondaryBlock(rig.tape, rate, 128, run);
	}
	return run;
}

std::vector<int32_t> first(const std::vector<int32_t>& v, size_t n) {
	return {v.begin(), v.begin() + static_cast<std::ptrdiff_t>(n)};
}

} // namespace

// clang-format off
describe delay_tape("DelayTape", ${
	it("sizes the tape for a rate, clamping at both ends", _ {
		using Size = std::pair<int32_t, bool>;
		expect(DelayTape::ideal_size_for_rate(16777216)).to_equal(Size{16384, false});
		expect(DelayTape::ideal_size_for_rate(kNative)).to_equal(Size{256, false});
		expect(DelayTape::ideal_size_for_rate(4294967295U)).to_equal(Size{64, false});
		expect(DelayTape::ideal_size_for_rate(3200000)).to_equal(Size{85899, false});
		expect(DelayTape::ideal_size_for_rate(1000000)).to_equal(Size{88200, true});
		expect(DelayTape::ideal_size_for_rate(100)).to_equal(Size{88200, true});
	});

	it("sizes a rate of 64 or below at the largest size, never a tape shorter than its clear", _ {
		using Size = std::pair<int32_t, bool>;
		expect(DelayTape::ideal_size_for_rate(64)).to_equal(Size{88200, true});
		expect(DelayTape::ideal_size_for_rate(32)).to_equal(Size{88200, true});
		expect(DelayTape::ideal_size_for_rate(1)).to_equal(Size{88200, true});
	});

	it("clears a tape at the size a rate of 64 gets without writing past its storage", _ {
		const size_t size = static_cast<size_t>(DelayTape::ideal_size_for_rate(64).first);
		const size_t used = size + DelayTape::kWriteLag;
		constexpr StereoSample kSentinel{.l = 0x13579BDF, .r = 0x2468ACE0};
		std::vector<StereoSample> backing(used + 4, kSentinel);
		DelayTape tape{std::span{backing}.first(used), size, 64};
		tape.clear();
		for (size_t i = used; i < backing.size(); ++i) {
			expect(backing[i].l).to_equal(kSentinel.l);
			expect(backing[i].r).to_equal(kSentinel.r);
		}
	});

	it("resamples at a rate of 0 at the slowest spin it can write, two 16.16 steps a sample", _ {
		Rig rig{kNative};
		const Run run = secondary(rig, kNative, 0);
		expect(rig.tape.resampling()).to_be_true();
		expect(first(run.strengths, 4)).to_equal(std::vector<int32_t>{2, 4, 6, 8});
		expect(run.strengths.back()).to_equal(768);
		expect(run.wraps).to_equal(std::vector<int32_t>{});
	});

	it("says whether it will resample at a rate exactly as its setup for that rate decides", _ {
		// Native at its native rate stays native; native at any other rate starts resampling; resampling stays so, at
		// its native rate too; an inactive tape never resamples.
		Rig rig{kNative};
		expect(rig.tape.resamples_at(kNative)).to_be_false();
		rig.tape.setup_for_render(static_cast<int32_t>(kNative));
		expect(rig.tape.resampling()).to_be_false();
		expect(rig.tape.resamples_at(static_cast<uint32_t>(kFast))).to_be_true();
		rig.tape.setup_for_render(kFast);
		expect(rig.tape.resampling()).to_be_true();
		expect(rig.tape.resamples_at(kNative)).to_be_true();
		rig.tape.setup_for_render(static_cast<int32_t>(kNative));
		expect(rig.tape.resampling()).to_be_true();
		DelayTape inactive;
		expect(inactive.resamples_at(static_cast<uint32_t>(kFast))).to_be_false();
		inactive.setup_for_render(kFast);
		expect(inactive.resampling()).to_be_false();
	});

	it("sizes a rate of 0 at the largest size", _ {
		using Size = std::pair<int32_t, bool>;
		expect(DelayTape::ideal_size_for_rate(0)).to_equal(Size{88200, true});
	});

	it("makes its native rate precise for its size, alone or relative to another tape", _ {
		Rig clamped{1000000};
		expect(clamped.tape.native_rate()).to_equal(3116530U);
		Rig unclamped{3200000};
		expect(unclamped.tape.native_rate()).to_equal(3200000U);
		unclamped.tape.make_native_rate_precise();
		expect(unclamped.tape.native_rate()).to_equal(3200013U);
		Rig other{5000000};
		other.tape.make_native_rate_precise_relative_to(clamped.tape);
		expect(other.tape.size()).to_equal(size_t{54975});
		expect(other.tape.native_rate()).to_equal(5000054U);
	});

	it("starts native with its read head kWriteLag in, the samples up to two past it cleared", _ {
		expect(DelayTape{}.is_active()).to_be_false();
		Rig rig{kNative};
		expect(rig.tape.is_active()).to_be_true();
		expect(rig.tape.is_native()).to_be_true();
		expect(rig.tape.resampling()).to_be_false();
		expect(rig.tape.native_rate()).to_equal(kNative);
		expect(rig.tape.size()).to_equal(size_t{256});
		expect(rig.tape.size_including_extra()).to_equal(size_t{276});
		expect(rig.tape.end() - rig.tape.begin()).to_equal(std::ptrdiff_t{276});
		expect(&rig.tape.current() - rig.tape.begin()).to_equal(DelayTape::kWriteLag);
		expect(first(storage(rig.tape), 52)).to_equal(kFreshHead);
		expect(digest(storage(rig.tape))).to_equal(kFreshStorageDigest);
	});

	it("moves the read head on and back, reporting each wrap", _ {
		Rig rig{kNative};
		rig.tape.set_current(rig.tape.end() - 2);
		expect(rig.tape.move_on()).to_be_false();
		expect(rig.tape.move_on()).to_be_true();
		expect(&rig.tape.current() - rig.tape.begin()).to_equal(std::ptrdiff_t{0});
		expect(rig.tape.move_back()).to_be_true();
		expect(&rig.tape.current() - rig.tape.begin()).to_equal(std::ptrdiff_t{275});
		expect(rig.tape.move_back()).to_be_false();
		expect(&rig.tape.current() - rig.tape.begin()).to_equal(std::ptrdiff_t{274});
		rig.tape.current() = {.l = 5, .r = 6};
		expect(rig.tape.clear_and_move_on()).to_be_false();
		expect(rig.tape.end()[-2].l).to_equal(0);
		expect(rig.tape.end()[-2].r).to_equal(0);
	});

	it("round-trips natively as the delay's primary tape, the wrap reported where the read head passes the end", _ {
		Rig rig{kNative};
		Run run;
		for (int i = 0; i < 5; ++i) {
			primaryBlock(rig.tape, static_cast<int32_t>(kNative), 128, run);
		}
		expect(run.wraps).to_equal(kNativePrimaryWraps);
		expect(std::vector<int32_t>(run.out.begin() + 500, run.out.begin() + 524)).to_equal(kNativePrimaryWindow);
		expect(digest(run.out)).to_equal(kNativePrimaryOutDigest);
		expect(digest(storage(rig.tape))).to_equal(kNativePrimaryStorageDigest);
	});

	it("writes resampled at 1.5 times native as the delay's secondary tape", _ {
		Rig rig{kNative};
		const Run run = secondary(rig, kNative, kFast);
		expect(rig.tape.resampling()).to_be_true();
		expect(first(run.strengths, 24)).to_equal(kFastWriteStrengths);
		expect(digest(run.strengths)).to_equal(kFastWriteStrengthDigest);
		expect(run.wraps).to_equal(kFastWriteWraps);
		expect(first(storage(rig.tape), 48)).to_equal(kFastWriteStored);
		expect(digest(storage(rig.tape))).to_equal(kFastWriteStorageDigest);
	});

	it("writes resampled at 0.75 times native as the delay's secondary tape", _ {
		Rig rig{kNative};
		const Run run = secondary(rig, kNative, kSlow);
		expect(first(run.strengths, 24)).to_equal(kSlowWriteStrengths);
		expect(digest(run.strengths)).to_equal(kSlowWriteStrengthDigest);
		expect(run.wraps).to_equal(kSlowWriteWraps);
		expect(first(storage(rig.tape), 48)).to_equal(kSlowWriteStored);
		expect(digest(storage(rig.tape))).to_equal(kSlowWriteStorageDigest);
	});

	it("writes resampled at a quarter of native, four steps wide", _ {
		Rig rig{kNative};
		const Run run = secondary(rig, kNative, 268435456);
		expect(first(run.strengths, 24)).to_equal(kQuarterWriteStrengths);
		expect(digest(run.strengths)).to_equal(kQuarterWriteStrengthDigest);
		expect(run.wraps).to_equal(kQuarterWriteWraps);
		expect(first(storage(rig.tape), 48)).to_equal(kQuarterWriteStored);
		expect(digest(storage(rig.tape))).to_equal(kQuarterWriteStorageDigest);
	});

	it("writes resampled above 8 times native with the sped-up write's width held at 8 times", _ {
		Rig rig{1U << 27};
		const Run run = secondary(rig, 1U << 27, 1248225690);
		expect(first(run.strengths, 24)).to_equal(kNinefoldWriteStrengths);
		expect(digest(run.strengths)).to_equal(kNinefoldWriteStrengthDigest);
		expect(run.wraps).to_equal(kNinefoldWriteWraps);
		expect(first(storage(rig.tape), 48)).to_equal(kNinefoldWriteStored);
		expect(digest(storage(rig.tape))).to_equal(kNinefoldWriteStorageDigest);
	});

	it("reads and writes resampled as the delay's primary tape, from a head saved and restored per block", _ {
		Rig rig{kNative};
		Run run;
		primaryBlock(rig.tape, static_cast<int32_t>(kNative), 128, run);
		for (int i = 0; i < 4; ++i) {
			primaryBlock(rig.tape, kFast, 128, run);
		}
		for (int i = 0; i < 4; ++i) {
			primaryBlock(rig.tape, kSlow, 128, run);
		}
		expect(run.wraps).to_equal(kResampledPrimaryWraps);
		expect(std::vector<int32_t>(run.out.begin() + 512, run.out.begin() + 536)).to_equal(kResampledPrimaryWindow);
		expect(digest(run.out)).to_equal(kResampledPrimaryOutDigest);
		expect(digest(run.strengths)).to_equal(kResampledPrimaryStrengthDigest);
		expect(digest(storage(rig.tape))).to_equal(kResampledPrimaryStorageDigest);
	});

	it("returns to a saved head: pointer, long position and last short position", _ {
		Rig rig{kNative};
		rig.tape.setup_for_render(kFast);
		for (int i = 0; i < 10; ++i) {
			std::ignore = rig.tape.advance([&] { rig.tape.move_on(); });
		}
		const DelayTape::Head saved = rig.tape.head();
		expect(saved.pos - rig.tape.begin()).to_equal(std::ptrdiff_t{35});
		expect(saved.long_pos).to_equal(251658240U);
		expect(saved.last_short_pos).to_equal(uint8_t{15});
		auto twenty = [&] {
			std::vector<int32_t> strengths;
			for (int i = 0; i < 20; ++i) {
				strengths.push_back(rig.tape.advance([&] { rig.tape.move_on(); }));
			}
			return strengths;
		};
		expect(twenty()).to_equal(kHeadStrengths);
		expect(&rig.tape.current() - rig.tape.begin()).to_equal(std::ptrdiff_t{65});
		rig.tape.set_head(saved);
		expect(rig.tape.head().pos - rig.tape.begin()).to_equal(std::ptrdiff_t{35});
		expect(rig.tape.head().long_pos).to_equal(saved.long_pos);
		expect(rig.tape.head().last_short_pos).to_equal(saved.last_short_pos);
		expect(twenty()).to_equal(kHeadStrengths);
	});

	it("records and plays a native stutter forward", _ {
		expect(stutter(kNative, kNative, false, false)).to_equal(kNativeForwardStutter);
	});
	it("records and plays a native stutter backward", _ {
		expect(stutter(kNative, kNative, true, false)).to_equal(kNativeReverseStutter);
	});
	it("plays a native stutter ping-pong, turning at the last sample and the first", _ {
		expect(stutter(kNative, kNative, false, true)).to_equal(kNativePingPongStutter);
		expect(stutter(kNative, kNative, true, true)).to_equal(kNativePingPongReverseStutter);
	});
	it("records and plays a stutter at 1.5 times native forward and backward", _ {
		expect(stutter(kFast, kFast, false, false)).to_equal(kFastForwardStutter);
		expect(stutter(kFast, kFast, true, false)).to_equal(kFastReverseStutter);
	});
	it("plays ping-pong at 1.5 times native without turning: its steps of two pass over the last sample", _ {
		expect(stutter(kFast, kFast, false, true)).to_equal(kFastPingPongStutter);
	});
	it("plays ping-pong at 1.25 times native, turning at each end", _ {
		expect(stutter(kQuick, kQuick, false, true)).to_equal(kQuickPingPongStutter);
	});
	it("records and plays a stutter at 0.75 times native forward, backward and ping-pong", _ {
		expect(stutter(kSlow, kSlow, false, false)).to_equal(kSlowForwardStutter);
		expect(stutter(kSlow, kSlow, true, false)).to_equal(kSlowReverseStutter);
		expect(stutter(kSlow, kSlow, true, true)).to_equal(kSlowPingPongReverseStutter);
	});
	it("starts resampling mid-play when the stutter rate rises", _ {
		expect(stutter(kNative, kFast, false, false)).to_equal(kRateRisesStutter);
	});
});
// clang-format on

CPPSPEC_MAIN(delay_tape);
