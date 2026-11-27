#pragma once

// Every menu item's param, by node id over nodes::kAllRoots, as the menu's kind tables named it: recorded from the
// tables before they named params by table and id (menu_param_ids_spec). The DX7 page's level, amp mod and transpose,
// which those tables never had, are its oscillator A volume, pulse width and pitch, in the same numbering.

#include <cstdint>

namespace deluge::ui::scenes::menu::reference {

/// @brief One menu item's param: its node, the table its id is in (0 the sound's, 1 the chain's), the id, and
///        whether it is a patched param.
struct MenuParamCell {
	uint16_t node{0};
	uint16_t name{0};
	uint8_t table{0};
	uint32_t id{0};
	bool patched{false};
};

inline constexpr MenuParamCell kMenuParamIds[] = {
    {16, 341, 0, 45, true},    {21, 723, 0, 23, true},    {22, 459, 0, 25, true},    {32, 670, 0, 101, false},
    {34, 650, 0, 54, true},    {42, 356, 0, 102, false},  {44, 369, 0, 103, false},  {51, 377, 0, 113, false},
    {53, 378, 0, 114, false},  {55, 376, 0, 115, false},  {57, 361, 0, 105, false},  {59, 367, 0, 112, false},
    {61, 365, 0, 104, false},  {63, 366, 0, 111, false},  {65, 360, 0, 106, false},  {67, 363, 0, 109, false},
    {69, 362, 0, 108, false},  {71, 364, 0, 110, false},  {73, 368, 0, 107, false},  {75, 660, 0, 100, false},
    {84, 631, 0, 24, true},    {85, 633, 0, 11, true},    {86, 533, 0, 17, true},    {89, 631, 0, 30, true},
    {90, 633, 0, 12, true},    {91, 538, 0, 18, true},    {95, 595, 0, 91, false},   {96, 382, 0, 93, false},
    {97, 383, 0, 94, false},   {98, 597, 0, 92, false},   {100, 653, 0, 49, true},   {103, 650, 0, 50, true},
    {106, 653, 0, 47, true},   {121, 650, 0, 90, false},  {126, 640, 0, 48, true},   {127, 650, 0, 51, true},
    {128, 588, 0, 98, false},  {129, 592, 0, 97, false},  {132, 387, 0, 96, false},  {133, 618, 0, 95, false},
    {134, 885, 0, 6, true},    {135, 458, 0, 3, true},    {139, 379, 0, 99, false},  {146, 666, 0, 26, true},
    {147, 545, 0, 21, true},   {148, 555, 0, 19, true},   {149, 588, 0, 9, true},    {159, 666, 0, 26, true},
    {160, 555, 0, 19, true},   {162, 341, 0, 0, true},    {166, 666, 0, 27, true},   {167, 545, 0, 22, true},
    {168, 555, 0, 20, true},   {169, 588, 0, 10, true},   {179, 666, 0, 28, true},   {180, 588, 0, 7, true},
    {183, 666, 0, 29, true},   {184, 588, 0, 8, true},    {188, 744, 0, 0, true},    {189, 744, 0, 1, true},
    {190, 785, 0, 4, true},    {191, 785, 0, 5, true},    {194, 662, 0, 33, true},   {195, 541, 0, 37, true},
    {196, 543, 0, 13, true},   {197, 609, 0, 41, true},   {202, 662, 0, 34, true},   {203, 541, 0, 38, true},
    {204, 543, 0, 14, true},   {205, 609, 0, 42, true},   {210, 662, 0, 35, true},   {211, 541, 0, 39, true},
    {212, 543, 0, 15, true},   {213, 609, 0, 43, true},   {218, 662, 0, 36, true},   {219, 541, 0, 40, true},
    {220, 543, 0, 16, true},   {221, 609, 0, 44, true},   {228, 650, 0, 52, true},   {232, 650, 0, 31, true},
    {236, 650, 0, 53, true},   {240, 650, 0, 32, true},   {245, 566, 0, 116, false}, {288, 341, 1, 129, false},
    {289, 723, 1, 121, false}, {292, 631, 1, 122, false}, {293, 633, 1, 123, false}, {294, 536, 1, 124, false},
    {296, 631, 1, 125, false}, {297, 633, 1, 126, false}, {298, 536, 1, 127, false}, {301, 653, 1, 119, false},
    {302, 650, 1, 118, false}, {304, 653, 1, 128, false}, {306, 640, 1, 117, false}, {307, 650, 1, 116, false},
    {310, 657, 1, 130, false}, {312, 666, 1, 131, false}, {343, 916, 1, 131, false}, {346, 650, 1, 120, false},
};

} // namespace deluge::ui::scenes::menu::reference
