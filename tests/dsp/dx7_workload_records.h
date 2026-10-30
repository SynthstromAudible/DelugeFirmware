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
/// @brief The DX7 reference's records over every workload (dx7_workloads.h) and its tables, which the components must
/// equal: recorded from it, identical on the host and on ARM.

#pragma once

#include "dx7_workloads.h"

#include <array>

namespace dx7_workloads {

/// @brief What the reference renders from each workload, in WorkloadId order.
inline const std::array<WorkloadRecord, kWorkloadCount> kRecordedReference{{
    // ALG_0_MODERN_FB0
    {.outputs = 0xe7eecadb0741878bULL, .state = 0x4d80ed13f3563577ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_0_MODERN_FB7
    {.outputs = 0xe7eecadb0741878bULL, .state = 0x97b559ea02e81e3cULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_0_MKI_FB0
    {.outputs = 0x446593ed23d7e48dULL, .state = 0xb4a3a85aa08efd0fULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_0_MKI_FB7
    {.outputs = 0x446593ed23d7e48dULL, .state = 0xb691f181dae2e24bULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_1_MODERN_FB0
    {.outputs = 0xe7eecadb0741878bULL, .state = 0x6adcb5f7b383f3bULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_1_MODERN_FB7
    {.outputs = 0x6508a00646474e6bULL, .state = 0xbdc96dffba74ca06ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_1_MKI_FB0
    {.outputs = 0x446593ed23d7e48dULL, .state = 0x65e84dc3293db4bbULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_1_MKI_FB7
    {.outputs = 0xc42c59a5b2aa848dULL, .state = 0x9c4f5645d03b10bbULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_2_MODERN_FB0
    {.outputs = 0x7c340b6c7c067e48ULL, .state = 0x26d1fcde892e4483ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_2_MODERN_FB7
    {.outputs = 0x7c340b6c7c067e48ULL, .state = 0x27696e0604737640ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_2_MKI_FB0
    {.outputs = 0x2c4ace8fa507648dULL, .state = 0xce8c7aaf9d44ac3fULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_2_MKI_FB7
    {.outputs = 0x2c4ace8fa507648dULL, .state = 0x36b1f7432d9d653fULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_3_MODERN_FB0
    {.outputs = 0x7c340b6c7c067e48ULL, .state = 0x33a30520142305bULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_3_MODERN_FB7
    {.outputs = 0x7c340b6c7c067e48ULL, .state = 0x7fb09f8d43825b88ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_3_MKI_FB0
    {.outputs = 0x2c4ace8fa507648dULL, .state = 0xa15d4deb9b8ac2b3ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_3_MKI_FB7
    {.outputs = 0xc9d6fe6b8e49648dULL, .state = 0x484fc878d3df269bULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_4_MODERN_FB0
    {.outputs = 0x4c0f31b2603c5028ULL, .state = 0xe3bb10b3ee59f893ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_4_MODERN_FB7
    {.outputs = 0x158dd9fd418372baULL, .state = 0x6d7ef3ffc4454ca4ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_4_MKI_FB0
    {.outputs = 0x51b40bef9597c48dULL, .state = 0x4fe53e9ee490a317ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_4_MKI_FB7
    {.outputs = 0x8d4698c06d74648dULL, .state = 0x27df63818d22873fULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_5_MODERN_FB0
    {.outputs = 0x4c0f31b2603c5028ULL, .state = 0x887b6a876edc19efULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_5_MODERN_FB7
    {.outputs = 0x158dd9fd418372baULL, .state = 0xfb98567b747523acULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_5_MKI_FB0
    {.outputs = 0x51b40bef9597c48dULL, .state = 0x928dce4ec5437893ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_5_MKI_FB7
    {.outputs = 0xf813a63002b248dULL, .state = 0xd00eb997bb1a0a77ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_6_MODERN_FB0
    {.outputs = 0xc47ddd9173b17d23ULL, .state = 0xd94159dac8bb90afULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_6_MODERN_FB7
    {.outputs = 0x7db4e3af923daebdULL, .state = 0x8d97ab0a87678494ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_6_MKI_FB0
    {.outputs = 0x27ae3bc4d8a1048dULL, .state = 0xcd533f1dc83f3cafULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_6_MKI_FB7
    {.outputs = 0xd88b730b31ce848dULL, .state = 0x12981faba881b67ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_7_MODERN_FB0
    {.outputs = 0xc47ddd9173b17d23ULL, .state = 0x4cb3ea0aa7015d1fULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_7_MODERN_FB7
    {.outputs = 0xc47ddd9173b17d23ULL, .state = 0xf27ee4e9872bedc3ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_7_MKI_FB0
    {.outputs = 0x27ae3bc4d8a1048dULL, .state = 0x77c19322be95e33bULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_7_MKI_FB7
    {.outputs = 0x27ae3bc4d8a1048dULL, .state = 0x4d0ea7125ddf7b07ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_8_MODERN_FB0
    {.outputs = 0xc47ddd9173b17d23ULL, .state = 0x6855720fc84d418fULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_8_MODERN_FB7
    {.outputs = 0x1938f486dc768c95ULL, .state = 0x18d49b060e14cc4eULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_8_MKI_FB0
    {.outputs = 0x27ae3bc4d8a1048dULL, .state = 0xd8a95e330ad66d37ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_8_MKI_FB7
    {.outputs = 0x38fec151094e248dULL, .state = 0x1aca2496deb00f57ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_9_MODERN_FB0
    {.outputs = 0x7c340b6c7c067e48ULL, .state = 0xa6d7d98df6446dc3ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_9_MODERN_FB7
    {.outputs = 0x443251fdc7780ef5ULL, .state = 0x5ea33af7883d3930ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_9_MKI_FB0
    {.outputs = 0x2c4ace8fa507648dULL, .state = 0x7a98c5af6eba9dffULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_9_MKI_FB7
    {.outputs = 0x2ab3a77fcb40248dULL, .state = 0x2a9d11cf5345d44fULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_10_MODERN_FB0
    {.outputs = 0x7c340b6c7c067e48ULL, .state = 0xee5c21fdd1b8a37ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_10_MODERN_FB7
    {.outputs = 0x7c340b6c7c067e48ULL, .state = 0x7a156eab4b1e292cULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_10_MKI_FB0
    {.outputs = 0x2c4ace8fa507648dULL, .state = 0xf5709e02dcb25cabULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_10_MKI_FB7
    {.outputs = 0x2c4ace8fa507648dULL, .state = 0x7eac316ee55f9b9bULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_11_MODERN_FB0
    {.outputs = 0xd9bf261491e56504ULL, .state = 0x3f4f7ff4cf7ab903ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_11_MODERN_FB7
    {.outputs = 0x80eec165445adfcaULL, .state = 0xf2ed3c8f018e4a36ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_11_MKI_FB0
    {.outputs = 0xbdc681305e1d448dULL, .state = 0x15e415c7daf72f0bULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_11_MKI_FB7
    {.outputs = 0x13d995f16fdfe48dULL, .state = 0xba446dfe8d89c18fULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_12_MODERN_FB0
    {.outputs = 0xd9bf261491e56504ULL, .state = 0xa79222cf20432a6fULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_12_MODERN_FB7
    {.outputs = 0xc2ce6a05089d1006ULL, .state = 0xf5d2ea3d5b71b3b4ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_12_MKI_FB0
    {.outputs = 0xbdc681305e1d448dULL, .state = 0xa7f1e95edf322023ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_12_MKI_FB7
    {.outputs = 0x50750bd9df9e048dULL, .state = 0xca9065e618f0db53ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_13_MODERN_FB0
    {.outputs = 0xe7eecadb0741878bULL, .state = 0xe4295e6be8b81517ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_13_MODERN_FB7
    {.outputs = 0xe7eecadb0741878bULL, .state = 0xf132f2e3d0b04ea8ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_13_MKI_FB0
    {.outputs = 0x446593ed23d7e48dULL, .state = 0x639dbd696705970fULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_13_MKI_FB7
    {.outputs = 0x446593ed23d7e48dULL, .state = 0x38cdce549270fd7ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_14_MODERN_FB0
    {.outputs = 0xe7eecadb0741878bULL, .state = 0x208c4cc16653ee1bULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_14_MODERN_FB7
    {.outputs = 0x6508a00646474e6bULL, .state = 0x7382dba2c5da4f1eULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_14_MKI_FB0
    {.outputs = 0x446593ed23d7e48dULL, .state = 0xbe61eb7f312414e7ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_14_MKI_FB7
    {.outputs = 0xc42c59a5b2aa848dULL, .state = 0xb094a9866a980ddbULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_15_MODERN_FB0
    {.outputs = 0xb56587d4280157a0ULL, .state = 0xa8f92c96b9c0a91fULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_15_MODERN_FB7
    {.outputs = 0x930f0c3171a4c952ULL, .state = 0xfd0bbd347f2a6800ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_15_MKI_FB0
    {.outputs = 0x78fa1ae038d1448dULL, .state = 0xcb63570b75b2e093ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_15_MKI_FB7
    {.outputs = 0x2527547b9fb0648dULL, .state = 0x85399235366827a3ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_16_MODERN_FB0
    {.outputs = 0xb56587d4280157a0ULL, .state = 0x5a8d83c87fabde93ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_16_MODERN_FB7
    {.outputs = 0xaf01623a471d100dULL, .state = 0x3199ec2ccad5a90aULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_16_MKI_FB0
    {.outputs = 0x78fa1ae038d1448dULL, .state = 0xb7f9c461a7cf28b3ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_16_MKI_FB7
    {.outputs = 0xb0af6ebcf472848dULL, .state = 0x981e150fe99922efULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_17_MODERN_FB0
    {.outputs = 0xfe8e11e7f183c959ULL, .state = 0x3fe823f196f421ffULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_17_MODERN_FB7
    {.outputs = 0xa2749fa2e89b81c1ULL, .state = 0x1b837bd4bc1f1ee4ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_17_MKI_FB0
    {.outputs = 0x5a191eb41a25648dULL, .state = 0x5d44935a9fe4b62fULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_17_MKI_FB7
    {.outputs = 0xf1dc381c890d448dULL, .state = 0xce710fe4402c38bbULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_18_MODERN_FB0
    {.outputs = 0x91a3bcbb67e5950dULL, .state = 0xdd4e1079020b41c3ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_18_MODERN_FB7
    {.outputs = 0x2dbd1b8536ccc409ULL, .state = 0x9b223ca909cc39e4ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_18_MKI_FB0
    {.outputs = 0xb2df2f2461d5c48dULL, .state = 0xb77d8a1a12d7066fULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_18_MKI_FB7
    {.outputs = 0x5dd0318df11de48dULL, .state = 0x13764a4d6a1c1a7bULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_19_MODERN_FB0
    {.outputs = 0xc46c98721e1de57eULL, .state = 0x6218ebdab0a496dfULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_19_MODERN_FB7
    {.outputs = 0x6b4fc28368fe37a3ULL, .state = 0x62ce64a2bc505ab0ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_19_MKI_FB0
    {.outputs = 0x1a2c20244651848dULL, .state = 0xd376c11a53ebcc37ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_19_MKI_FB7
    {.outputs = 0xe1d7c7ccd036448dULL, .state = 0xf8fbaff3ad05c4dbULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_20_MODERN_FB0
    {.outputs = 0xe2f575b4ac6473e5ULL, .state = 0x1779e574ab87a933ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_20_MODERN_FB7
    {.outputs = 0x58a72afd12000f9cULL, .state = 0x3ea68371e2825364ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_20_MKI_FB0
    {.outputs = 0xd7b953fadddea48dULL, .state = 0x7a189084b1630d53ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_20_MKI_FB7
    {.outputs = 0x4cd6ec3890a0a48dULL, .state = 0x15ecb425bf78c2d7ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_21_MODERN_FB0
    {.outputs = 0x12c086104918cdf9ULL, .state = 0x5c9f633635618077ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_21_MODERN_FB7
    {.outputs = 0x92e8ba0b39e8b48dULL, .state = 0xcd8b7ab938b79aa4ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_21_MKI_FB0
    {.outputs = 0x356e04124a2bc48dULL, .state = 0xfa5491c0a75a2d5fULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_21_MKI_FB7
    {.outputs = 0x70d7f53644dfe48dULL, .state = 0x549995340963d95fULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_22_MODERN_FB0
    {.outputs = 0x80c2526c9c85fe65ULL, .state = 0xc323ee9f40cca543ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_22_MODERN_FB7
    {.outputs = 0x1899cbea9641d9ebULL, .state = 0x5710372506b3798ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_22_MKI_FB0
    {.outputs = 0xbe611af7bd49848dULL, .state = 0x5da449d3651a0fabULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_22_MKI_FB7
    {.outputs = 0x7affafd017bb248dULL, .state = 0xe9340b3a1290e8dbULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_23_MODERN_FB0
    {.outputs = 0x3826fb104e8f4465ULL, .state = 0xb72ee6798dee639bULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_23_MODERN_FB7
    {.outputs = 0x82a44783a7e6d885ULL, .state = 0xdbb4899fb892bc8ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_23_MKI_FB0
    {.outputs = 0xc2a59c929f21c48dULL, .state = 0x9fe9f498c1c90e1bULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_23_MKI_FB7
    {.outputs = 0x8e93e2f0cb0f248dULL, .state = 0x76eceea16ff880c7ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_24_MODERN_FB0
    {.outputs = 0x7f5f4fd894518ef4ULL, .state = 0x1b4417dc2c9b10c3ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_24_MODERN_FB7
    {.outputs = 0xd9b4440900573182ULL, .state = 0x8cdded508120a7f4ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_24_MKI_FB0
    {.outputs = 0x606021429be4048dULL, .state = 0x778595cf353f842fULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_24_MKI_FB7
    {.outputs = 0x8ce6988c3985248dULL, .state = 0x82784a7ecf6f3b27ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_25_MODERN_FB0
    {.outputs = 0xfa607c043a6fee7eULL, .state = 0x1a869b3ca0fb9bULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_25_MODERN_FB7
    {.outputs = 0xfa607c043a6fee7eULL, .state = 0xc7337abad30c14c0ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_25_MKI_FB0
    {.outputs = 0x86a5f165ad01248dULL, .state = 0xe978d95563f92af3ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_25_MKI_FB7
    {.outputs = 0x86a5f165ad01248dULL, .state = 0x57288d943593740fULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_26_MODERN_FB0
    {.outputs = 0xfa607c043a6fee7eULL, .state = 0x2dbbf9835ca65b8bULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_26_MODERN_FB7
    {.outputs = 0x652441ce37bc23beULL, .state = 0x7db7969789839f54ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_26_MKI_FB0
    {.outputs = 0x86a5f165ad01248dULL, .state = 0xa166d7c5ff270307ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_26_MKI_FB7
    {.outputs = 0x9a84fa0ca028448dULL, .state = 0xe7a98e1a833a748fULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_27_MODERN_FB0
    {.outputs = 0x35e2f0b5272d16c5ULL, .state = 0x70a301101bfd3b2bULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_27_MODERN_FB7
    {.outputs = 0x35e2f0b5272d16c5ULL, .state = 0xc39a84c92c7b8276ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_27_MKI_FB0
    {.outputs = 0x510fce12f4d0c48dULL, .state = 0x145fe0001b9842d3ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_27_MKI_FB7
    {.outputs = 0x510fce12f4d0c48dULL, .state = 0xc3f76824d7bd069fULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_28_MODERN_FB0
    {.outputs = 0x7f5f4fd894518ef4ULL, .state = 0xe3e4d62ec8c1303fULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_28_MODERN_FB7
    {.outputs = 0xd9b4440900573182ULL, .state = 0x983e8b93244c32d0ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_28_MKI_FB0
    {.outputs = 0x606021429be4048dULL, .state = 0x238cd83141cb458bULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_28_MKI_FB7
    {.outputs = 0x8ce6988c3985248dULL, .state = 0xa0f5584f0ab9a4a7ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_29_MODERN_FB0
    {.outputs = 0x7e20ba36ac70e36bULL, .state = 0x43898a7299ddbca3ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_29_MODERN_FB7
    {.outputs = 0x7e20ba36ac70e36bULL, .state = 0xe4e37cad3efc89aeULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_29_MKI_FB0
    {.outputs = 0xfe09e727c9d9048dULL, .state = 0xa0b556b235569ecbULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_29_MKI_FB7
    {.outputs = 0xfe09e727c9d9048dULL, .state = 0x1a4762d6274a8e2bULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_30_MODERN_FB0
    {.outputs = 0x7f5f4fd894518ef4ULL, .state = 0x124d01a6734ada1fULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_30_MODERN_FB7
    {.outputs = 0xd9b4440900573182ULL, .state = 0xeebe898197e190d0ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_30_MKI_FB0
    {.outputs = 0x606021429be4048dULL, .state = 0x852c2c57197912d7ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_30_MKI_FB7
    {.outputs = 0x8ce6988c3985248dULL, .state = 0x3ce845c5d1d3376fULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_31_MODERN_FB0
    {.outputs = 0x560e18e1a489569bULL, .state = 0x58636409c7ba5c33ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_31_MODERN_FB7
    {.outputs = 0x7c4e789228ccebf1ULL, .state = 0xa85272b475794348ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_31_MKI_FB0
    {.outputs = 0xd99db73d4e2ba48dULL, .state = 0x3c18a3b1364f9e37ULL, .alive = 0x251c2a09d3947719ULL},
    // ALG_31_MKI_FB7
    {.outputs = 0x84c34aac19e3648dULL, .state = 0x509144514a11e05bULL, .alive = 0x251c2a09d3947719ULL},
    // AUTO_FB2
    {.outputs = 0x78cac8042bfa48dULL, .state = 0x822895473f6a1ce7ULL, .alive = 0x251c2a09d3947719ULL},
    // AUTO_FB3
    {.outputs = 0x37a0140f46cc848dULL, .state = 0xa33858b0da070b7fULL, .alive = 0x251c2a09d3947719ULL},
    // MKI_ALG32_FB
    {.outputs = 0xd24bc494ecada48dULL, .state = 0xe9e12a6ca20b4cdfULL, .alive = 0x251c2a09d3947719ULL},
    // MODERN_ALG4_FB
    {.outputs = 0x981d13e3ece4ad43ULL, .state = 0x40819a8058db671aULL, .alive = 0x251c2a09d3947719ULL},
    // MODERN_ALG6_FB
    {.outputs = 0xdb3e359bb2190ad1ULL, .state = 0x152adc88d7a834c2ULL, .alive = 0x251c2a09d3947719ULL},
    // ENGINE_SWITCH_MID_NOTE
    {.outputs = 0x2b5e6c8f6455e18cULL, .state = 0x2fd5876abdcf6b41ULL, .alive = 0x251c2a09d3947719ULL},
    // PATCH_EDIT_MID_NOTE
    {.outputs = 0xd56d8efc660a1be4ULL, .state = 0x69c976d90cbe6612ULL, .alive = 0x251c2a09d3947719ULL},
    // LFO_WAVE_0
    {.outputs = 0x47c2d6bbacc625d5ULL, .state = 0x30824f993abbd429ULL, .alive = 0x251c2a09d3947719ULL},
    // LFO_WAVE_1
    {.outputs = 0xbcc47ab07431b69dULL, .state = 0x963813c1051f46eaULL, .alive = 0x251c2a09d3947719ULL},
    // LFO_WAVE_2
    {.outputs = 0xf92e3f6883e01688ULL, .state = 0x6209c049b7bca694ULL, .alive = 0x251c2a09d3947719ULL},
    // LFO_WAVE_3
    {.outputs = 0xdb706a0e3f185a41ULL, .state = 0xd65cb4b66d408a9aULL, .alive = 0x251c2a09d3947719ULL},
    // LFO_WAVE_4
    {.outputs = 0x47c2d6bbacc625d5ULL, .state = 0x3b798987cd502081ULL, .alive = 0x251c2a09d3947719ULL},
    // LFO_WAVE_5
    {.outputs = 0x95f5e30f33b250a8ULL, .state = 0xe84464efee2319f0ULL, .alive = 0x251c2a09d3947719ULL},
    // LFO_DELAY_KEY_SYNC
    {.outputs = 0xf861f797444063b2ULL, .state = 0x6b7aaafe54b25d97ULL, .alive = 0x251c2a09d3947719ULL},
    // OSC_KEY_SYNC_OFF
    {.outputs = 0x824b6bb303dc01d8ULL, .state = 0xcccb68ecaedd57afULL, .alive = 0x251c2a09d3947719ULL},
    // RANDOM_DETUNE
    {.outputs = 0x175f44ff9cdf4f16ULL, .state = 0x7fe41ce1923c3e45ULL, .alive = 0x251c2a09d3947719ULL},
    // FIXED_FREQ
    {.outputs = 0xa3ab0471cb777ad5ULL, .state = 0x4d62db14bb7881b5ULL, .alive = 0x251c2a09d3947719ULL},
    // RELEASE_TAIL_MODERN
    {.outputs = 0xde625ea91d9adaebULL, .state = 0x92892ad00c3fc9e3ULL, .alive = 0xf44381d68215826fULL},
    // RELEASE_TAIL_MKI
    {.outputs = 0xdd691e3d27a32725ULL, .state = 0xee8178833af78371ULL, .alive = 0x680aa42dad733dffULL},
    // RETRIGGER_KEEPS_FEEDBACK
    {.outputs = 0xe04964023eff1266ULL, .state = 0x1354cd114746b30eULL, .alive = 0x251c2a09d3947719ULL},
}};

/// @brief The reference's tables.
inline constexpr TableRecord kRecordedTables{0xf41af9505d26224bULL, 0x6be14c4ca0e56dbdULL, 0xfbb3375360c0c5bULL,
                                             0xa7e2f8331f1b344dULL, 0x4235cf2d96c95f5aULL};

/// @brief Whether an MkI voice's compute ever returns false after its release. Named quirk: it does not, as an MkI
/// voice's gain_out is an attenuation, so the modern engine's threshold holds it alive.
inline constexpr bool kMkIVoiceEnds = false;

} // namespace dx7_workloads
