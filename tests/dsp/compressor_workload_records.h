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

/// @file
/// @brief What each compressor workload (compressor_workloads.h) renders: each block's output digest (its samples, then
/// the gain reduction) and the digest of the compressor's `view()` at the end. Identical on the host and on ARM.

#pragma once

#include "compressor_workloads.h"

#include <array>

namespace compressor_workloads {

/// @brief Each compressor workload's record, recorded from the legacy compressor (the verbatim
/// reference), in CompressorWorkloadId order: no draws, and no noise digest.
inline const std::array<WorkloadRecord, kCompressorWorkloadCount> kCompressorRecorded{{
    // comp_master_silence_then_sound
    {.outputs = {0xd94d12186c0f2fb7ULL, 0xe4bc4fd9252be94fULL, 0x778b1a14b6876aa7ULL, 0xe604823a249029bfULL,
                 0xd62c1b27906e7cd0ULL, 0xd820fc1260f76f6cULL, 0x125303e3646056e5ULL, 0xfbedffafad77724cULL,
                 0x6cb693414a74614dULL, 0xd726b6432b300989ULL, 0xda65e480ab012028ULL, 0x94dfda3c18eca934ULL,
                 0x62039691c711e465ULL, 0x720f6576e7c6e433ULL, 0xc980884760f8ccf5ULL, 0x09b75a0a1a8e5fcdULL,
                 0x1ade4f0e5e159c8dULL, 0x8f3ec7148b6f9014ULL, 0xee828906046cef85ULL, 0x531a6956dc670571ULL,
                 0x8aa925f0d3ed38bbULL, 0x87ee5f8ed9c7750fULL, 0xe7173991d830f74fULL, 0x56f601b61c4fe19dULL,
                 0x74df749130bd1efaULL, 0x1492c378836697f3ULL, 0xeefee882f43cf60eULL, 0xee8f7203b67de439ULL,
                 0x02297ced6dde35dfULL, 0xcdc2f6c86bf526e8ULL, 0x40457d880ef3ea4aULL, 0x5c674ba473404812ULL,
                 0x4ab87c12973a5afdULL, 0xaf2756a4bf6c167cULL, 0xeac5604d3d3b5d1bULL, 0xb9181e9eb3b553f2ULL,
                 0xe7210280deb90267ULL, 0xe10da00e6a405fd4ULL, 0x410a19825f89b71fULL, 0x181167a8feda9aebULL},
     .state = 0xff1a08664cb7ea20ULL},
    // comp_master_onset_partial_wet
    {.outputs = {0xd94d12186c0f2fb7ULL, 0xe4bc4fd9252be94fULL, 0x778b1a14b6876aa7ULL, 0x16f03f468c1b3031ULL,
                 0xd1651ff56d454184ULL, 0xd668911107802c16ULL, 0x4ee16e3bb9d3353aULL, 0x72adcbb6d40976f2ULL,
                 0x84a852a095ea8af8ULL, 0xeec1a496be50d27bULL, 0xcb42bc984ee32eccULL, 0x90eb10978f6090c5ULL,
                 0x6aaaaa24b33786f0ULL, 0x569d85ee71f0f4d8ULL, 0xf55925f8fd354e8cULL, 0xaee65ceef9101435ULL,
                 0x6efb2e05dfb97a04ULL, 0x21e64517fc517b61ULL, 0x06d43383a8aa57beULL, 0x0b93086aa784784eULL,
                 0xe27decfccc063512ULL, 0x6b90bdd7fcf50e44ULL, 0x41e2277d73f29fb5ULL, 0x3b7978f38dde507eULL,
                 0x998ba31abd9b51e2ULL, 0x3c25e3544d377eabULL, 0x53d1f210b3363b2bULL, 0xaed371fdf2073d70ULL,
                 0x7627cc78a5d8629aULL, 0x5d2baa9d4008a240ULL, 0xafb2f9fc2e7d582cULL, 0xbc7f6a2f60d57407ULL,
                 0x5641df9771f03f61ULL, 0x8bed14a9f35d66d1ULL, 0x19ef4afa4432b337ULL, 0x056743b3937b8e5dULL,
                 0x22bf05726a437a55ULL, 0xb52ade1c74e82463ULL, 0x86979955f1a88799ULL, 0xed3e9cbf3b633281ULL},
     .state = 0xdbc5e3723f8ebb93ULL},
    // comp_sound_silence_then_sound
    {.outputs = {0xd94d12186c0f2fb7ULL, 0xe4bc4fd9252be94fULL, 0x778b1a14b6876aa7ULL, 0xe604823a249029bfULL,
                 0x57203c3a35bbbf91ULL, 0xca47637c0c1dcff8ULL, 0x5c3fba81a29b7b66ULL, 0x03e7226e2a2ead42ULL,
                 0x4e8c06e569aa9230ULL, 0x80a355443dacf303ULL, 0xfa9d8a5ba2e01428ULL, 0x4870baef2828c335ULL,
                 0x79668c5eb4aca9d9ULL, 0xa9d61499c9cb764eULL, 0x743bee670aa2d55bULL, 0x86ab765ce889b89cULL,
                 0x03304e7b72d0ed8bULL, 0x3be224fc5311c626ULL, 0x5e61f9766f48d201ULL, 0x26aee99dc32f0c0bULL,
                 0x2802169bf5aa9f04ULL, 0xc238405f2af330c9ULL, 0x9c31212e7c9df3afULL, 0x937466ed2efbeba7ULL,
                 0x860a9bf25a345ca3ULL, 0x71d2942c6de83b9eULL, 0xd34f0022f6cf6d16ULL, 0x87b70ac0cb2ab4bbULL,
                 0xc39bf7b0c279b020ULL, 0x38281958bf7cac1cULL, 0x7d3a50b21167bcf5ULL, 0x68ed3ae77bffe9f8ULL,
                 0xfa453425ff1770c7ULL, 0xad92734e870e1934ULL, 0x7ea8a9fe9ed848bdULL, 0xe89eaf6eaa3f130aULL,
                 0x9739102e68f801e5ULL, 0xe5bdb04d81326bbeULL, 0xe1201548c8d0e6fcULL, 0x706224b10c412e90ULL},
     .state = 0x5d76c010237ebe02ULL},
    // comp_sound_onset_partial_wet
    {.outputs = {0xd94d12186c0f2fb7ULL, 0xe4bc4fd9252be94fULL, 0x778b1a14b6876aa7ULL, 0x9af431ae0aae8545ULL,
                 0x501cc62ec43b31edULL, 0xbfa282a1890125aaULL, 0x654b23558d301c2eULL, 0x9b87de820fc3a2daULL,
                 0x0d05a0bd6f8695c7ULL, 0xde29537e7addf589ULL, 0x1b851ae144a93e62ULL, 0xde9e2aeda1ca36aaULL,
                 0x78b9a36632872893ULL, 0xb09803106e712889ULL, 0xe530b773854ffd12ULL, 0xb9b449fa9a5becf1ULL,
                 0x6eface05dfb8d6e4ULL, 0x21e6a517fc521e81ULL, 0x63ff0268cda6a0b7ULL, 0x06a15f29f32d4552ULL,
                 0x8e9984923cac7b3bULL, 0xca9bbe4d27cf07dbULL, 0xd1d91fc0bf4c1d9fULL, 0x6e168bda09e89187ULL,
                 0x089665edb2907139ULL, 0xf5cc778865535bd2ULL, 0xe8b982ccf8fed0c6ULL, 0x6b345626a8da4171ULL,
                 0x24a6f909d75418c1ULL, 0xd2274bcfc6d93d5fULL, 0x924bf89ecd47e8bfULL, 0xd1eb9000c9e607e2ULL,
                 0x376a636fa855a7e0ULL, 0x63c73d3dfdbbed9aULL, 0x326d3608bd5f5a1eULL, 0x8bdd14fc97d29276ULL,
                 0x0b09254517868afeULL, 0x165128f7eaba8f7eULL, 0x56c84bb70b7b0f50ULL, 0x84589a55fd66ac4eULL},
     .state = 0x4ebd5ab2ee3b6b07ULL},
    // comp_clip_silence_then_sound
    {.outputs = {0xd94d12186c0f2fb7ULL, 0xe4bc4fd9252be94fULL, 0x778b1a14b6876aa7ULL, 0xe604823a249029bfULL,
                 0x5db2b0af513f4c1bULL, 0x9b2d04638f738b27ULL, 0x37e6a8d0c147fcacULL, 0x678df8afd7803833ULL,
                 0xd8be5540add6f237ULL, 0xc09265479cd9c46aULL, 0xa1ee2e7b5ce0ba8eULL, 0x3f39dc79741c997cULL,
                 0x58102e5cc777356aULL, 0x5089ec065853df7bULL, 0x54b6c26725c93946ULL, 0xd29de2fe89731cd3ULL,
                 0x01055103ef986846ULL, 0x59abbc856a388a04ULL, 0x78d41bf33145706bULL, 0x1d1f9936f61d3d68ULL,
                 0x7d544419ddd11d9eULL, 0x7f4982956161a9d3ULL, 0x0bfb12eb6ba65b80ULL, 0xce4c1568dba30863ULL,
                 0x31a4f6ff264c08bfULL, 0xab308fe1b71bd456ULL, 0x909153e2ba6e1623ULL, 0xf89894a3a906d0f4ULL,
                 0x4cb8e170ad576421ULL, 0x2adc5fec08504d99ULL, 0x5d33ee4e98179953ULL, 0xff4fb352622d69b7ULL,
                 0xe6934c405f45bda4ULL, 0x5605fd68510c47aaULL, 0xdcedf82bcd0e6a64ULL, 0x3e8b8f6678b7b437ULL,
                 0xe9dfb016b5313061ULL, 0x9a9743121e82bbf3ULL, 0xb4d5c7dee2e9d520ULL, 0x4e1c03adb7ed2f5aULL},
     .state = 0x081e29756e1b7744ULL},
    // comp_clip_onset_partial_wet
    {.outputs = {0xd94d12186c0f2fb7ULL, 0xe4bc4fd9252be94fULL, 0x778b1a14b6876aa7ULL, 0x006816f94ecf1b23ULL,
                 0xdd06e8be11a1f3adULL, 0xff245520a5e4f02eULL, 0x4228c089204649d5ULL, 0x20b734c33a08c1fdULL,
                 0x7e1a6adcdbc05c12ULL, 0xe234627b2911f6a4ULL, 0xc8fa1f1f9ed2c256ULL, 0xb48b27565a007ff7ULL,
                 0xc9d3c859a3ffca51ULL, 0xc10f8d833a5e379dULL, 0xa313147c78f19fc3ULL, 0xac42b1ad3ded071eULL,
                 0x6efae505dfb8fdf9ULL, 0x21e67e17fc51dc3cULL, 0xae7d2d9a3c61bcf7ULL, 0x0fa98eb33d00f3d0ULL,
                 0x655750b29b955cbfULL, 0x57f6f21566678922ULL, 0x8f06366775e2ba6dULL, 0x1be21f15dd8b7e48ULL,
                 0xade8c2a31b689880ULL, 0xd207f795748b2e31ULL, 0xed9aa101b9833084ULL, 0xbabfaf9c2bfc99c9ULL,
                 0xc04cf31424bfbc98ULL, 0x3061a5db875147cbULL, 0x3b48d13bb72a9cd4ULL, 0x50e71452350b76c2ULL,
                 0xc0c064f9f793e9bfULL, 0x3f3b2f8a6fc7b691ULL, 0xd97ebe8787c9afe1ULL, 0x012f5cd3d496da10ULL,
                 0x9d31e9149a77ef0eULL, 0x1c607dd31dfae1acULL, 0x0f07ebf1d78e70eeULL, 0xfe2eb23309b84a1aULL},
     .state = 0xd39f7afe8a0ad115ULL},
    // comp_master_volume_steps
    {.outputs = {0xc564bbebdca54c59ULL, 0xab9fca16e1b32e46ULL, 0xfda3c13de6987cc3ULL, 0xa349200295547800ULL,
                 0x34d25a8b50b14db9ULL, 0x940f61b8434604e5ULL, 0xd57e100f770ac837ULL, 0x2778155349f8d1ecULL,
                 0xd66c916ae53b65e7ULL, 0x4c42d597ceb0285fULL, 0xa61754eedfbef546ULL, 0xa1c8d86bdc719dc1ULL,
                 0xa70ff40dd48c0a14ULL, 0x2847c424fe863e91ULL, 0xdae9a008135cff1dULL, 0x42d2536bc008439dULL,
                 0x6e3e56d3ed8bc3abULL, 0x41f6322ce1ba5c33ULL, 0xc4599db589687e95ULL, 0x6ca4a4fe8f22817bULL,
                 0x8828426e5f459c0aULL, 0x947f28b78ad35adeULL, 0x25eb310dcf5642b8ULL, 0x2355515be7e5d3d3ULL,
                 0xe95b529c77198a24ULL, 0xe0d7c3f2f1896c99ULL, 0x02e5bd4ded25f63dULL, 0xb8b7e15c91544d62ULL,
                 0x71f3b350ff207c85ULL, 0xc458c91f335d164fULL, 0xa8ed8935b9185dfcULL, 0xf592f00798b684c5ULL,
                 0x9cf6b00afa9b9b58ULL, 0x8c9529f5439268acULL, 0xf34a871035aa72fbULL, 0x05c272f092318862ULL,
                 0x7230b48869915422ULL, 0xa952c405174e136cULL, 0x7cae770de11c37ecULL, 0x31639807d0bb6c33ULL},
     .state = 0x03895ec595af3fa4ULL},
}};

} // namespace compressor_workloads
