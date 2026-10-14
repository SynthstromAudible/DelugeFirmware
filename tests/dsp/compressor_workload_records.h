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
/// the gain reduction) and the digest of the compressor's `view()` at the end, from the legacy compressor where it is
/// recorded and from the fixed reference everywhere. Identical on the host and on ARM.

#pragma once

#include "compressor_workloads.h"

#include <array>

namespace compressor_workloads {

/// @brief The record of each compressor workload where neither level defect fires, recorded from the legacy compressor
/// (the verbatim reference), in CompressorWorkloadId order: no draws, and no noise digest.
inline const std::array<WorkloadRecord, kLegacyRecordedCount> kCompressorRecorded{{
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

/// @brief Each compressor workload's record, rendered by the fixed reference, in CompressorWorkloadId order: no draws,
/// and no noise digest.
inline const std::array<WorkloadRecord, kCompressorWorkloadCount> kFixedCompressorRecorded{{
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
                 0x84a852a095ea8af8ULL, 0xeec1a496be50d27bULL, 0x8c7835ba1427fc4bULL, 0xda45171bbaf31c13ULL,
                 0xe38e17ad8ba92441ULL, 0x5d58b56e2a08a341ULL, 0x082b5bc38a5b1bebULL, 0x12553fcba3922a3cULL,
                 0x6efb3f05dfb996e7ULL, 0x21e63417fc515e7eULL, 0x23ba3bc482a43086ULL, 0x7cff703f533e8aeaULL,
                 0x689183a1e009fc8eULL, 0xed4ba382ba8a756cULL, 0xeadda5e45c8ea067ULL, 0xb7888c97c2b6c4d8ULL,
                 0x4ea2f7c591b68bbeULL, 0x5ad4ecb3003c4979ULL, 0x0e2828be6e4c2425ULL, 0xd1ba306d65ed2c64ULL,
                 0x75dc8d674355055eULL, 0x6fc4ee88992ac980ULL, 0x245db5df9a982498ULL, 0xd290f57599e44da9ULL,
                 0x4be6cd1196e63dd9ULL, 0xdd20e140feda9b65ULL, 0x502625526e4bf443ULL, 0x9097b348749fb76cULL,
                 0xc91a7b60764ff5f2ULL, 0x0d76f229c3dcbbecULL, 0x76ae04c0743a0bc0ULL, 0x00b608a35236545aULL},
     .state = 0xd77f06e1f1112a4fULL},
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
                 0x0d05a0bd6f8695c7ULL, 0xde29537e7addf589ULL, 0x9041b10947bfd0e8ULL, 0x190572e471947aadULL,
                 0x2ccb4c698a8ef5dbULL, 0x814354d55201b0a3ULL, 0x3d05921c3f8ac0cfULL, 0x3f123e10d70ef54cULL,
                 0x6efad305dfb8df63ULL, 0x21e69017fc51fad2ULL, 0x9642dee6a45d37e5ULL, 0xc1e5bbab5326cccaULL,
                 0xf32292c3f05391efULL, 0xb14d466d463a71adULL, 0xf7b2a847ab69fa99ULL, 0x947b422d565cd30bULL,
                 0xff67e07fa8bcc993ULL, 0xe8befdccf25f3364ULL, 0x3d45e4b18b2e5a1eULL, 0xf54eb9815ec88f6fULL,
                 0x67525f3a3862c6d3ULL, 0x5a6d1bb6efe35f09ULL, 0x532a15fef6c64dd7ULL, 0xb13ea4feb17c9cd0ULL,
                 0x1d0f388c7cd38f42ULL, 0x4594e4f3dbbadc9cULL, 0x96764ff1bea1fe50ULL, 0x43f9f42c91a72322ULL,
                 0x0b62aa7a700c945cULL, 0x4896ed735fe5ddb8ULL, 0x52be76eea35ae572ULL, 0x5916b18a5b7a941eULL},
     .state = 0x4b43f4ad762d1920ULL},
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
                 0x7e1a6adcdbc05c12ULL, 0xe234627b2911f6a4ULL, 0x3af1bbeed67cadf0ULL, 0xd92f026d53eddd4dULL,
                 0xdfa82936b8f9744dULL, 0xfca29e81b2527880ULL, 0x108b3a223ea9efe8ULL, 0x3d4686cd5a7981fcULL,
                 0x6efafd05dfb926c1ULL, 0x21e67617fc51cea4ULL, 0x226f5a17abd0d96fULL, 0x9379e0299584510cULL,
                 0x8c08ddf9c855f56fULL, 0xacbe021fcadb463aULL, 0xea8ec1fdb95fb27bULL, 0xd4d2e2a2e1634d60ULL,
                 0xc65407c56a1388f6ULL, 0x0196d3f32c7f4657ULL, 0x64fd04b00f35d936ULL, 0x2295414d58295933ULL,
                 0x7262022a1a4ac8d4ULL, 0x25a53b38a86958b5ULL, 0x9b8828da46c2bdccULL, 0x29f1b7f6727f372aULL,
                 0xe2ae8e8cd9fba14fULL, 0x0e46f10d39b051efULL, 0x1f3cb64472fb662fULL, 0x28ba83223fe05e76ULL,
                 0x8e667195006804deULL, 0xf4af67a257c8ea5eULL, 0x0eb64980387b1ff8ULL, 0x0a00f640b549d644ULL},
     .state = 0x0becaa00d416c20dULL},
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
    // comp_blend_switch
    {.outputs = {0xd94d12186c0f2fb7ULL, 0xe4bc4fd9252be94fULL, 0x545ab0bc35303928ULL, 0x5573353328c04602ULL,
                 0x5517cb220dc7b9f0ULL, 0x0c218a132d8e9ab0ULL, 0xe5bcf61aa84d80b2ULL, 0x22c7c312bf550bd4ULL,
                 0xcaebec56e7f5cc88ULL, 0x081fa51b87f33413ULL, 0x8f7034c928e9c771ULL, 0x1557a6cc0b363270ULL,
                 0x88066777dd1d0746ULL, 0x50058ca92abf1b51ULL, 0x16859cae4380a53cULL, 0x18ca731a8a7c76c2ULL,
                 0x9b005093de32060eULL, 0xf5f24d88be5dba52ULL, 0xffae63b46bf427d2ULL, 0x1824234602fe6294ULL,
                 0xea06acd83c1356a5ULL, 0xfb07677ab42a4d50ULL, 0xb023462600d5b04cULL, 0xe3a9ddd83eac3ff8ULL,
                 0x3f9b0eb627fc5d78ULL, 0x5c87952075e7d49aULL, 0x9d5433fb777aa3e9ULL, 0x5b3dfbc2d9d577d0ULL,
                 0x954b99ec8b68c596ULL, 0x86faee4585e0d2e0ULL, 0x5d58f4191a279368ULL, 0x812ebfabba962f65ULL,
                 0x685a0cfd1e77211fULL, 0xa527cc96bc548a8aULL, 0x6bf59759399f0dfeULL, 0x2e2d68ceca0172eaULL,
                 0x477dfeadcf392d37ULL, 0xaf444370c8a777f6ULL, 0x456ffddab2259f77ULL, 0xb40d4983090be978ULL},
     .state = 0x797c39671cb343bfULL},
    // comp_block_lengths
    {.outputs = {0xc564bbebdca54c59ULL, 0xab9fca16e1b32e46ULL, 0xfda3c13de6987cc3ULL, 0xa349200295547800ULL,
                 0x34d25a8b50b14db9ULL, 0x940f61b8434604e5ULL, 0xd57e100f770ac837ULL, 0x2778155349f8d1ecULL,
                 0xd66c916ae53b65e7ULL, 0x95bd74d6008b0331ULL, 0xc88fa2b9b75e2decULL, 0x8f5bfd526b616076ULL,
                 0x956723c55a1d8e3bULL, 0xfdc97abd704c8acbULL, 0x850dcc40295bb36eULL, 0xd4f2957ae4d64cf6ULL,
                 0xb150bcdbfd78bfe2ULL, 0x1d3159e158384decULL, 0x9bd69b50b2fc7440ULL, 0x9593dc4a9ec07a16ULL,
                 0x20546737766046d0ULL, 0xc05e30c61ece2fe7ULL, 0xeb9b8e841cd03603ULL, 0x28181946a72db540ULL,
                 0x32f3f4aef2e1177eULL, 0xc31d724cba4c59e7ULL, 0x08993285e0e6c0b5ULL, 0x6ca48261cf6b05d2ULL,
                 0x317a4caceb5d0374ULL, 0x1091ca1909c940a7ULL, 0xe101052e70211ed4ULL, 0xe5532da3a0d21923ULL,
                 0x0c00368969a2a192ULL, 0x23e51cfc3be6f404ULL, 0x37f4053538bff7e8ULL, 0xd6344e0f7357a2b1ULL,
                 0x0788e43fa9f90f14ULL, 0x7483ad8b48eceb48ULL, 0x97dd70895861a32dULL, 0x65cee1ce5028331bULL},
     .state = 0xdf5aa1cd75b058dcULL},
    // comp_unequal_volumes
    {.outputs = {0x90fc6779869eaaaeULL, 0x2b1f26e281b3c34dULL, 0xdfd6b7ea5c691895ULL, 0x6101ae2c03420a76ULL,
                 0x49b4012f149e9bb9ULL, 0x774ab22b216cab90ULL, 0xa1ab2e8aed083d31ULL, 0xe8770a33612ede21ULL,
                 0xc6360f5ef1e88b74ULL, 0xad382a6563a01298ULL, 0x2241e131d069ec61ULL, 0xb1abf310a45c3f07ULL,
                 0x88a3617a63b30edbULL, 0x9c1c776055fad5beULL, 0xb34257799e7baaffULL, 0xfd40c3ca03a5995fULL,
                 0x82cc8547fff987f2ULL, 0x93217c9fcb1c80e2ULL, 0x6f067384cc694dfeULL, 0xae59b6dfc2fd4dc2ULL,
                 0x5872961831e82383ULL, 0x14d7e377ae564d68ULL, 0xb6262145803158f9ULL, 0xe1cda4cc675637d2ULL,
                 0xd108145056b18e56ULL, 0xfe26fdbffdab1406ULL, 0xcc569fd8cddff3a6ULL, 0xab35ce3081401defULL,
                 0xfdbf70f55efcdd71ULL, 0xf0bbc1f80955f428ULL, 0xd904530aef90ce8dULL, 0x67fbe9e7193eddc8ULL,
                 0xb4e83ed0d7e554cfULL, 0x9707110be1f6e219ULL, 0x3ae4a6d92a273e0bULL, 0x5e23b5bba59c2862ULL,
                 0x070ff71b35592640ULL, 0x1e7b6525b62882e5ULL, 0x86be06cc90c01d4bULL, 0xbb7fbbf71214e563ULL},
     .state = 0x909bd778fa4a0979ULL},
    // comp_wrapped_sum
    {.outputs = {0x4ffdadbfb25d2e61ULL, 0x8b04177c2c5aed07ULL, 0xccab19fb3c34b90dULL, 0x796b947305a12456ULL,
                 0x1ffc5d3433ae787bULL, 0xa2457755f50e8debULL, 0x406b82b813d1989aULL, 0x9c4b4ae8e0f79e42ULL,
                 0x47ce590181407831ULL, 0xcdc7bcaf619b6b3aULL, 0x57af1c6243cc55daULL, 0x6ae80d6692eb708cULL,
                 0x36850f4c66b3c64aULL, 0x9c886b6ad3737fd8ULL, 0x3ce24ba5b569d478ULL, 0x359625dd7544eee1ULL,
                 0xad693e5204575109ULL, 0x908bf9c9ec15746aULL, 0x61650ed0a7e42dd4ULL, 0xf3156898b0eb1a33ULL,
                 0x8d97baca3f6df941ULL, 0xb8581b429b1518ddULL, 0xa90a7130e4bfd95fULL, 0x168fa206cd5ebd87ULL,
                 0x02d02abfe8442cbfULL, 0xeb826f3f61aefd13ULL, 0x65d58433abe6aa3aULL, 0x25d1cc1388bd1216ULL,
                 0x0b0bb0009ac10ffaULL, 0xf7a536f67c7ecfceULL, 0xf70f8767539b5418ULL, 0xa1f582b628f82901ULL,
                 0x938e38292d6f8505ULL, 0x1c6f5f1b377ed92eULL, 0x9b4826dbcc4f01ceULL, 0x0a575bcec8f10e81ULL,
                 0xb35927cd15ee8d13ULL, 0x22af7a397eb5ff09ULL, 0x5ca39c1d91be536dULL, 0x17456445cd600429ULL},
     .state = 0x21d21def630fb314ULL},
    // comp_int32_min_detector
    {.outputs = {0xd6db3d21ac0f2fb7ULL, 0x728f13f6da941fd5ULL, 0x034083beb4fcd469ULL, 0x628892d6cb826feeULL,
                 0x88b07562aa896af9ULL, 0x1309f79e291f9658ULL, 0xca255e404708c745ULL, 0x2b6f462c789212deULL,
                 0xb1256d3b33da3c6fULL, 0xcf5905559b87110aULL, 0xde7156910b66672bULL, 0xae8276f0904dba1fULL,
                 0xd6db4721ac0f40b5ULL, 0x6b8c24fb5f34281dULL, 0x91838c9340f92591ULL, 0xd1f5af975ca05890ULL,
                 0x4a99d06c7c69f8eeULL, 0x5ff09e80482a5f1fULL, 0x11c1e821e754ddfbULL, 0x6bd831f313980662ULL,
                 0xec116ab51c16d2d5ULL, 0x5fcd61aded578e57ULL, 0xe5552c20adad7eb8ULL, 0x9efb232df6176c31ULL,
                 0xd6db1521ac0eebbfULL, 0x0d50dc8d15059a6fULL, 0x43e6cd3c251eb269ULL, 0x350ecf854015926dULL,
                 0x9c58b55de2c315d6ULL, 0x0e1a0dd5451da11aULL, 0xed207127345b619eULL, 0xc02f5ee4526efde3ULL,
                 0x1420e647781c462fULL, 0x87ff3d39a9bdba06ULL, 0xcfc6b076dfc79bdcULL, 0xcfd992168c4ffa98ULL,
                 0xd6db1221ac0ee6a6ULL, 0xebefb8f7974320eaULL, 0xc1508ea01948a626ULL, 0x2776b41efb65094cULL},
     .state = 0x8b68c24f883ee508ULL},
}};

} // namespace compressor_workloads
