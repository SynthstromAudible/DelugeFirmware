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

#pragma once

#include "real_fft_workloads.h"

#include <array>
#include <cstdint>

/// @file
/// @brief The outputs of NE10's int32 real FFT, recorded on x86 and on ARM: one digest per (direction, size, input
/// set), and one per size for its factor and twiddle tables, each with and without the firmware's float flags.
namespace real_fft_records {

using real_fft_workloads::Direction;
using real_fft_workloads::InputSet;

/// @brief One transform's recorded output digest.
struct Record {
	Direction direction;
	int32_t magnitude;
	InputSet set;
	uint64_t digest;
};

/// @brief One size's recorded table digest.
struct TwiddleRecord {
	int32_t magnitude;
	uint64_t digest;
};

/// @brief Forward m = 4..13, then inverse m = 3..13, each over every input set; built without the firmware's float
/// flags, as the host and the sim build.
inline constexpr std::array<Record, 84> kRecords{{
    {Direction::Forward, 4, InputSet::Random, 0xf729ab3e8f64ff56ULL},
    {Direction::Forward, 4, InputSet::Impulses, 0x4f6a412f57407d58ULL},
    {Direction::Forward, 4, InputSet::FullScale, 0x0653af95e0f35eadULL},
    {Direction::Forward, 4, InputSet::RealCycles, 0x426e830d9995027eULL},
    {Direction::Forward, 5, InputSet::Random, 0xd63d02202c2564fbULL},
    {Direction::Forward, 5, InputSet::Impulses, 0x10cd764881585fbbULL},
    {Direction::Forward, 5, InputSet::FullScale, 0x1a29dd125027a16dULL},
    {Direction::Forward, 5, InputSet::RealCycles, 0xe211fa8800d4bc48ULL},
    {Direction::Forward, 6, InputSet::Random, 0x6853db2bc509f46bULL},
    {Direction::Forward, 6, InputSet::Impulses, 0xa60c81764ed88f14ULL},
    {Direction::Forward, 6, InputSet::FullScale, 0xea45472e6bbb22edULL},
    {Direction::Forward, 6, InputSet::RealCycles, 0xb5576fce5e518e1dULL},
    {Direction::Forward, 7, InputSet::Random, 0x0f57dcd502fcfe4eULL},
    {Direction::Forward, 7, InputSet::Impulses, 0x5a0c6688d9b142f3ULL},
    {Direction::Forward, 7, InputSet::FullScale, 0x8921e41653e395edULL},
    {Direction::Forward, 7, InputSet::RealCycles, 0xd3a99c841ca921d5ULL},
    {Direction::Forward, 8, InputSet::Random, 0xb2022133531a2e7aULL},
    {Direction::Forward, 8, InputSet::Impulses, 0x183acef21a4596feULL},
    {Direction::Forward, 8, InputSet::FullScale, 0x69b0b4099d90bbedULL},
    {Direction::Forward, 8, InputSet::RealCycles, 0x36b6f8ed815b1d2dULL},
    {Direction::Forward, 9, InputSet::Random, 0xe535e12fb2e3c855ULL},
    {Direction::Forward, 9, InputSet::Impulses, 0x88506fe3c69f3700ULL},
    {Direction::Forward, 9, InputSet::FullScale, 0x4adccccc7d0a07edULL},
    {Direction::Forward, 9, InputSet::RealCycles, 0xb9ca4adc3a8b666dULL},
    {Direction::Forward, 10, InputSet::Random, 0xb8b25fdfea72f8e5ULL},
    {Direction::Forward, 10, InputSet::Impulses, 0x040743095da6824cULL},
    {Direction::Forward, 10, InputSet::FullScale, 0x49d3c89b53589fedULL},
    {Direction::Forward, 10, InputSet::RealCycles, 0xa89f3de70b350350ULL},
    {Direction::Forward, 11, InputSet::Random, 0x5cd5e67755c1eac3ULL},
    {Direction::Forward, 11, InputSet::Impulses, 0x752cbc6d048b3006ULL},
    {Direction::Forward, 11, InputSet::FullScale, 0x8ffc39542a85cfedULL},
    {Direction::Forward, 11, InputSet::RealCycles, 0x9c012fa91896b81bULL},
    {Direction::Forward, 12, InputSet::Random, 0x290d79444adb1b12ULL},
    {Direction::Forward, 12, InputSet::Impulses, 0xeded6c4f84fc7f86ULL},
    {Direction::Forward, 12, InputSet::FullScale, 0xfaac26e455202fedULL},
    {Direction::Forward, 12, InputSet::RealCycles, 0xf76e976773b6d3cbULL},
    {Direction::Forward, 13, InputSet::Random, 0x5a77074c62d7c4f7ULL},
    {Direction::Forward, 13, InputSet::Impulses, 0x95617e9a8d86c2a8ULL},
    {Direction::Forward, 13, InputSet::FullScale, 0xb7d01861c954efedULL},
    {Direction::Forward, 13, InputSet::RealCycles, 0x6518141cb5ac453eULL},
    {Direction::Inverse, 3, InputSet::Random, 0x7557c7e20431a3b9ULL},
    {Direction::Inverse, 3, InputSet::Impulses, 0xa421cd8a7972e985ULL},
    {Direction::Inverse, 3, InputSet::FullScale, 0x391522c8edc45a7dULL},
    {Direction::Inverse, 3, InputSet::RealCycles, 0xf035186eb390a8b5ULL},
    {Direction::Inverse, 4, InputSet::Random, 0xdf4d1cd1500b5f69ULL},
    {Direction::Inverse, 4, InputSet::Impulses, 0x2bde3c88f84a1899ULL},
    {Direction::Inverse, 4, InputSet::FullScale, 0x5506e19f2c820a15ULL},
    {Direction::Inverse, 4, InputSet::RealCycles, 0x04f27688fa324c05ULL},
    {Direction::Inverse, 5, InputSet::Random, 0xf0f3d7bbfd4c266dULL},
    {Direction::Inverse, 5, InputSet::Impulses, 0xe73f792d69973f09ULL},
    {Direction::Inverse, 5, InputSet::FullScale, 0x0ff8edfeb14b7f45ULL},
    {Direction::Inverse, 5, InputSet::RealCycles, 0x26f1f68ec825d7b5ULL},
    {Direction::Inverse, 6, InputSet::Random, 0xa53e7571aa454d49ULL},
    {Direction::Inverse, 6, InputSet::Impulses, 0xfe2231411611663dULL},
    {Direction::Inverse, 6, InputSet::FullScale, 0xdb665aa7b2c281e5ULL},
    {Direction::Inverse, 6, InputSet::RealCycles, 0xb4f8d5a98313db99ULL},
    {Direction::Inverse, 7, InputSet::Random, 0x7351650d6afad429ULL},
    {Direction::Inverse, 7, InputSet::Impulses, 0x5ceacb4cb81718fdULL},
    {Direction::Inverse, 7, InputSet::FullScale, 0xde9d3db52f6ba6a5ULL},
    {Direction::Inverse, 7, InputSet::RealCycles, 0x0423c7719b541b65ULL},
    {Direction::Inverse, 8, InputSet::Random, 0xc6b359383e37b875ULL},
    {Direction::Inverse, 8, InputSet::Impulses, 0x522820734172daddULL},
    {Direction::Inverse, 8, InputSet::FullScale, 0x53bb80e50b0b7025ULL},
    {Direction::Inverse, 8, InputSet::RealCycles, 0xf309127274b07721ULL},
    {Direction::Inverse, 9, InputSet::Random, 0x98343f52cd01f001ULL},
    {Direction::Inverse, 9, InputSet::Impulses, 0x73ece38adbb7eb15ULL},
    {Direction::Inverse, 9, InputSet::FullScale, 0xdf6ddc39c6910525ULL},
    {Direction::Inverse, 9, InputSet::RealCycles, 0x2337ae2184340349ULL},
    {Direction::Inverse, 10, InputSet::Random, 0x07da27af03951309ULL},
    {Direction::Inverse, 10, InputSet::Impulses, 0x9632f0d8cdd30de5ULL},
    {Direction::Inverse, 10, InputSet::FullScale, 0xbaf86c2582a42f25ULL},
    {Direction::Inverse, 10, InputSet::RealCycles, 0xf29b5101373cb2f9ULL},
    {Direction::Inverse, 11, InputSet::Random, 0x53c480a6051b5201ULL},
    {Direction::Inverse, 11, InputSet::Impulses, 0x1521d455da2ca295ULL},
    {Direction::Inverse, 11, InputSet::FullScale, 0x56bbdd720dea7b25ULL},
    {Direction::Inverse, 11, InputSet::RealCycles, 0x9053bc29590be1bdULL},
    {Direction::Inverse, 12, InputSet::Random, 0x647972c8f37971ddULL},
    {Direction::Inverse, 12, InputSet::Impulses, 0x9d6100bee8a4b3e1ULL},
    {Direction::Inverse, 12, InputSet::FullScale, 0xc3acc73db1f71325ULL},
    {Direction::Inverse, 12, InputSet::RealCycles, 0xf0614bda9a7fec7dULL},
    {Direction::Inverse, 13, InputSet::Random, 0x9f62294bdc0b34d5ULL},
    {Direction::Inverse, 13, InputSet::Impulses, 0x2005b2200d2b16b1ULL},
    {Direction::Inverse, 13, InputSet::FullScale, 0xdb6e9f0f30104325ULL},
    {Direction::Inverse, 13, InputSet::RealCycles, 0xfb156f3dc8cda2b9ULL},
}};

/// @brief m = 3..13, built without the firmware's float flags.
inline constexpr std::array<TwiddleRecord, 11> kTwiddleRecords{{
    {3, 0xdf21c92641c73e0bULL},
    {4, 0xd48b3ffab3194266ULL},
    {5, 0x70970ef715d28b4eULL},
    {6, 0xb311026b4a648d1aULL},
    {7, 0x5863d51b60c7529aULL},
    {8, 0xa88a3825175a4f86ULL},
    {9, 0x9d7d6afa8b40180dULL},
    {10, 0xbfd610b2cd0ef4fcULL},
    {11, 0x6c3545f9803913f6ULL},
    {12, 0x02b9a90ff6b3606dULL},
    {13, 0x620e24f7ac5409b2ULL},
}};

/// @brief kRecords' transforms built as the firmware builds float code (-funsafe-math-optimizations): the device's
/// arithmetic. Its twiddle tables differ from 32 points up, and so do most transforms.
inline constexpr std::array<Record, 84> kRecordsShipping{{
    {Direction::Forward, 4, InputSet::Random, 0xf729ab3e8f64ff56ULL},
    {Direction::Forward, 4, InputSet::Impulses, 0x4f6a412f57407d58ULL},
    {Direction::Forward, 4, InputSet::FullScale, 0x0653af95e0f35eadULL},
    {Direction::Forward, 4, InputSet::RealCycles, 0x426e830d9995027eULL},
    {Direction::Forward, 5, InputSet::Random, 0x3e5332722aa06c5cULL},
    {Direction::Forward, 5, InputSet::Impulses, 0x9a76e914b1339b36ULL},
    {Direction::Forward, 5, InputSet::FullScale, 0x1a29dd125027a16dULL},
    {Direction::Forward, 5, InputSet::RealCycles, 0xe211fa8800d4bc48ULL},
    {Direction::Forward, 6, InputSet::Random, 0x9c2b633fe06de76eULL},
    {Direction::Forward, 6, InputSet::Impulses, 0x4fff020d77a288c7ULL},
    {Direction::Forward, 6, InputSet::FullScale, 0xea45472e6bbb22edULL},
    {Direction::Forward, 6, InputSet::RealCycles, 0xb5576fce5e518e1dULL},
    {Direction::Forward, 7, InputSet::Random, 0xbb7288f0f546f0ffULL},
    {Direction::Forward, 7, InputSet::Impulses, 0xd1f6f16d51e756c4ULL},
    {Direction::Forward, 7, InputSet::FullScale, 0x8921e41653e395edULL},
    {Direction::Forward, 7, InputSet::RealCycles, 0x67cf4267e5bc0cacULL},
    {Direction::Forward, 8, InputSet::Random, 0x1369f1f998dd7215ULL},
    {Direction::Forward, 8, InputSet::Impulses, 0xf6b31e3fd27bfa03ULL},
    {Direction::Forward, 8, InputSet::FullScale, 0x69b0b4099d90bbedULL},
    {Direction::Forward, 8, InputSet::RealCycles, 0x18386711394de7d4ULL},
    {Direction::Forward, 9, InputSet::Random, 0x2a9b45ff768ee67cULL},
    {Direction::Forward, 9, InputSet::Impulses, 0xc0a69db7d8aafbf2ULL},
    {Direction::Forward, 9, InputSet::FullScale, 0x4adccccc7d0a07edULL},
    {Direction::Forward, 9, InputSet::RealCycles, 0xe7faf9b48abf1f67ULL},
    {Direction::Forward, 10, InputSet::Random, 0xb7a8c759bf89fa72ULL},
    {Direction::Forward, 10, InputSet::Impulses, 0x4f2665427df1900dULL},
    {Direction::Forward, 10, InputSet::FullScale, 0x49d3c89b53589fedULL},
    {Direction::Forward, 10, InputSet::RealCycles, 0xb023d97a4047e005ULL},
    {Direction::Forward, 11, InputSet::Random, 0x5e0b5c3fe1921b21ULL},
    {Direction::Forward, 11, InputSet::Impulses, 0x049b71faf16a148aULL},
    {Direction::Forward, 11, InputSet::FullScale, 0x8ffc39542a85cfedULL},
    {Direction::Forward, 11, InputSet::RealCycles, 0x6aa0ef2536a7696aULL},
    {Direction::Forward, 12, InputSet::Random, 0xf6e3c7d4ad1fb3f1ULL},
    {Direction::Forward, 12, InputSet::Impulses, 0x05c7e1822ea75edfULL},
    {Direction::Forward, 12, InputSet::FullScale, 0xfaac26e455202fedULL},
    {Direction::Forward, 12, InputSet::RealCycles, 0xa6afc10a0e5b47f8ULL},
    {Direction::Forward, 13, InputSet::Random, 0xba92fd62d4fc4d72ULL},
    {Direction::Forward, 13, InputSet::Impulses, 0xcf92a46cc2e9d816ULL},
    {Direction::Forward, 13, InputSet::FullScale, 0xb7d01861c954efedULL},
    {Direction::Forward, 13, InputSet::RealCycles, 0x328a6ece346ffb1eULL},
    {Direction::Inverse, 3, InputSet::Random, 0x7557c7e20431a3b9ULL},
    {Direction::Inverse, 3, InputSet::Impulses, 0xa421cd8a7972e985ULL},
    {Direction::Inverse, 3, InputSet::FullScale, 0x391522c8edc45a7dULL},
    {Direction::Inverse, 3, InputSet::RealCycles, 0xf035186eb390a8b5ULL},
    {Direction::Inverse, 4, InputSet::Random, 0xdf4d1cd1500b5f69ULL},
    {Direction::Inverse, 4, InputSet::Impulses, 0x2bde3c88f84a1899ULL},
    {Direction::Inverse, 4, InputSet::FullScale, 0x5506e19f2c820a15ULL},
    {Direction::Inverse, 4, InputSet::RealCycles, 0x04f27688fa324c05ULL},
    {Direction::Inverse, 5, InputSet::Random, 0x530b713549178061ULL},
    {Direction::Inverse, 5, InputSet::Impulses, 0xe73f792d69973f09ULL},
    {Direction::Inverse, 5, InputSet::FullScale, 0x0ff8edfeb14b7f45ULL},
    {Direction::Inverse, 5, InputSet::RealCycles, 0x5a7b4916dc2a83f5ULL},
    {Direction::Inverse, 6, InputSet::Random, 0x65a2af89b9a0ab49ULL},
    {Direction::Inverse, 6, InputSet::Impulses, 0xec0e1320cc9726a9ULL},
    {Direction::Inverse, 6, InputSet::FullScale, 0xdb665aa7b2c281e5ULL},
    {Direction::Inverse, 6, InputSet::RealCycles, 0xd3d3a7fed86ab855ULL},
    {Direction::Inverse, 7, InputSet::Random, 0x42a197d4bdee7efdULL},
    {Direction::Inverse, 7, InputSet::Impulses, 0xfb4e9bca77ee5639ULL},
    {Direction::Inverse, 7, InputSet::FullScale, 0xde9d3db52f6ba6a5ULL},
    {Direction::Inverse, 7, InputSet::RealCycles, 0x41e6dce4b70be6bdULL},
    {Direction::Inverse, 8, InputSet::Random, 0x7874c34f748fd469ULL},
    {Direction::Inverse, 8, InputSet::Impulses, 0x4c4108f60d57ea91ULL},
    {Direction::Inverse, 8, InputSet::FullScale, 0x53bb80e50b0b7025ULL},
    {Direction::Inverse, 8, InputSet::RealCycles, 0x802ccedbabef6b51ULL},
    {Direction::Inverse, 9, InputSet::Random, 0x736fc09919a922c1ULL},
    {Direction::Inverse, 9, InputSet::Impulses, 0x8d21dee9ef4c53adULL},
    {Direction::Inverse, 9, InputSet::FullScale, 0xdf6ddc39c6910525ULL},
    {Direction::Inverse, 9, InputSet::RealCycles, 0xbf06854893e682cdULL},
    {Direction::Inverse, 10, InputSet::Random, 0xa4b45c6d8e3923b5ULL},
    {Direction::Inverse, 10, InputSet::Impulses, 0x97aef8d1f5c7848dULL},
    {Direction::Inverse, 10, InputSet::FullScale, 0xbaf86c2582a42f25ULL},
    {Direction::Inverse, 10, InputSet::RealCycles, 0xb5c4599401418049ULL},
    {Direction::Inverse, 11, InputSet::Random, 0x40da1707833b8045ULL},
    {Direction::Inverse, 11, InputSet::Impulses, 0xe5e0332322d5824dULL},
    {Direction::Inverse, 11, InputSet::FullScale, 0x56bbdd720dea7b25ULL},
    {Direction::Inverse, 11, InputSet::RealCycles, 0x149b8b8a04a376a1ULL},
    {Direction::Inverse, 12, InputSet::Random, 0x0c01712c2395f37dULL},
    {Direction::Inverse, 12, InputSet::Impulses, 0x96072016460451b1ULL},
    {Direction::Inverse, 12, InputSet::FullScale, 0xc3acc73db1f71325ULL},
    {Direction::Inverse, 12, InputSet::RealCycles, 0xfcd0a94559e82b99ULL},
    {Direction::Inverse, 13, InputSet::Random, 0x7c2d90c36ccb9d79ULL},
    {Direction::Inverse, 13, InputSet::Impulses, 0x6d39cb7ca7df5189ULL},
    {Direction::Inverse, 13, InputSet::FullScale, 0xdb6e9f0f30104325ULL},
    {Direction::Inverse, 13, InputSet::RealCycles, 0x2066e6f9c277f39dULL},
}};

/// @brief m = 3..13, built as the firmware builds float code.
inline constexpr std::array<TwiddleRecord, 11> kTwiddleRecordsShipping{{
    {3, 0xdf21c92641c73e0bULL},
    {4, 0xd48b3ffab3194266ULL},
    {5, 0x18ba11044f43ac3cULL},
    {6, 0x90b7e8521641d8e8ULL},
    {7, 0xcd6f31ca95e57e3eULL},
    {8, 0xed38a67357ce98c0ULL},
    {9, 0xfdbc41e6896385f5ULL},
    {10, 0x7d0e6df997eb9239ULL},
    {11, 0xc7151c317716d475ULL},
    {12, 0xcc402f3b69f5a5b7ULL},
    {13, 0x80b94daf9ea75648ULL},
}};

} // namespace real_fft_records
