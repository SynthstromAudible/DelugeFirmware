#pragma once

// Every menu item's param, by node id over nodes::kAllRoots, as the menu's kind tables named it: recorded from the
// tables before they named params by table and id (menu_param_ids_spec).

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
    {10, 341, 0, 45, true},    {15, 724, 0, 23, true},    {16, 459, 0, 25, true},    {26, 671, 0, 101, false},
    {28, 651, 0, 54, true},    {36, 356, 0, 102, false},  {38, 369, 0, 103, false},  {45, 377, 0, 113, false},
    {47, 378, 0, 114, false},  {49, 376, 0, 115, false},  {51, 361, 0, 105, false},  {53, 367, 0, 112, false},
    {55, 365, 0, 104, false},  {57, 366, 0, 111, false},  {59, 360, 0, 106, false},  {61, 363, 0, 109, false},
    {63, 362, 0, 108, false},  {65, 364, 0, 110, false},  {67, 368, 0, 107, false},  {69, 661, 0, 100, false},
    {78, 632, 0, 24, true},    {79, 634, 0, 11, true},    {80, 534, 0, 17, true},    {83, 632, 0, 30, true},
    {84, 634, 0, 12, true},    {85, 539, 0, 18, true},    {89, 596, 0, 91, false},   {90, 382, 0, 93, false},
    {91, 383, 0, 94, false},   {92, 598, 0, 92, false},   {94, 654, 0, 49, true},    {97, 651, 0, 50, true},
    {100, 654, 0, 47, true},   {115, 651, 0, 90, false},  {120, 641, 0, 48, true},   {121, 651, 0, 51, true},
    {122, 589, 0, 98, false},  {123, 593, 0, 97, false},  {126, 387, 0, 96, false},  {127, 619, 0, 95, false},
    {128, 886, 0, 6, true},    {129, 458, 0, 3, true},    {133, 379, 0, 99, false},  {146, 667, 0, 26, true},
    {147, 546, 0, 21, true},   {148, 556, 0, 19, true},   {149, 589, 0, 9, true},    {161, 667, 0, 27, true},
    {162, 546, 0, 22, true},   {163, 556, 0, 20, true},   {164, 589, 0, 10, true},   {174, 667, 0, 28, true},
    {175, 589, 0, 7, true},    {178, 667, 0, 29, true},   {179, 589, 0, 8, true},    {183, 745, 0, 0, true},
    {184, 745, 0, 1, true},    {185, 786, 0, 4, true},    {186, 786, 0, 5, true},    {189, 663, 0, 33, true},
    {190, 542, 0, 37, true},   {191, 544, 0, 13, true},   {192, 610, 0, 41, true},   {197, 663, 0, 34, true},
    {198, 542, 0, 38, true},   {199, 544, 0, 14, true},   {200, 610, 0, 42, true},   {205, 663, 0, 35, true},
    {206, 542, 0, 39, true},   {207, 544, 0, 15, true},   {208, 610, 0, 43, true},   {213, 663, 0, 36, true},
    {214, 542, 0, 40, true},   {215, 544, 0, 16, true},   {216, 610, 0, 44, true},   {223, 651, 0, 52, true},
    {227, 651, 0, 31, true},   {231, 651, 0, 53, true},   {235, 651, 0, 32, true},   {240, 567, 0, 116, false},
    {283, 341, 1, 129, false}, {284, 724, 1, 121, false}, {287, 632, 1, 122, false}, {288, 634, 1, 123, false},
    {289, 537, 1, 124, false}, {291, 632, 1, 125, false}, {292, 634, 1, 126, false}, {293, 537, 1, 127, false},
    {296, 654, 1, 119, false}, {297, 651, 1, 118, false}, {299, 654, 1, 128, false}, {301, 641, 1, 117, false},
    {302, 651, 1, 116, false}, {305, 658, 1, 130, false}, {307, 667, 1, 131, false}, {338, 917, 1, 131, false},
    {341, 651, 1, 120, false},
};

} // namespace deluge::ui::scenes::menu::reference
