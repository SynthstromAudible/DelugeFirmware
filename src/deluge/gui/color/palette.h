#pragma once
#include "rgb.h"

namespace colors {
using Color = RGB;

// Standard palette, with some modifications for the deluge (see _full)
constexpr Color black = RGB::monochrome(0);
constexpr Color grey = RGB::monochrome(7);
constexpr Color white_full = RGB::monochrome(255);
constexpr Color white = white_full.dim();
constexpr Color red = RGB(255, 0, 0);
constexpr Color red_alt = RGB(255, 1, 0);
constexpr Color red_dull = RGB(60, 15, 15);
constexpr Color orange = RGB(255, 128, 0);
constexpr Color yellow_orange = RGB(255, 160, 0);
constexpr Color yellow = RGB(255, 255, 0);
constexpr Color lime = RGB(128, 255, 0);
constexpr Color green = RGB(0, 255, 0);
constexpr Color turquoise = RGB(0, 255, 128);
constexpr Color cyan_full = RGB(0, 255, 255);
constexpr Color cyan = RGB(0, 128, 128);
constexpr Color darkblue = RGB(0, 128, 255);
constexpr Color blue = RGB(0, 0, 255);
constexpr Color purple = RGB(128, 0, 255);
constexpr Color magenta_full = RGB(255, 0, 255);
constexpr Color magenta = RGB(128, 0, 128);
constexpr Color magenta_dull = RGB(60, 15, 60);
constexpr Color pink_full = RGB(255, 128, 128);
constexpr Color pink = RGB(255, 44, 50);
constexpr Color amber = RGB(255, 48, 0);

// These colors are used globally by the deluge
constexpr Color disabled = colors::red;
constexpr Color group_enabled = colors::green;
constexpr Color enabled = RGB(0, 255, 6);
constexpr Color muted = colors::yellow_orange;
constexpr Color midi_command = RGB(255, 80, 120);
constexpr Color midi_no_command = RGB::monochrome(60);
constexpr Color selected_drum = RGB(30, 30, 10);

/**
 * @brief These are from "Twenty-two Colors of Maximum Contrast" by Kelly
 * @see http://www.iscc-archive.org/pdf/PC54_1724_001.pdf
 *
 */
namespace kelly {
constexpr Color vivid_yellow = RGB(255, 179, 0);
constexpr Color strong_purple = RGB(128, 62, 117);
constexpr Color vivid_orange = RGB(255, 104, 0);
constexpr Color very_light_blue = RGB(166, 189, 215);
constexpr Color vivid_red = RGB(193, 0, 32);
constexpr Color grayish_yellow = RGB(206, 162, 98);
constexpr Color medium_gray = RGB(129, 112, 102);

// these aren't good for people with defective color vision:
constexpr Color vivid_green = RGB(0, 125, 52);
constexpr Color strong_purplish_pink = RGB(246, 118, 142);
constexpr Color strong_blue = RGB(0, 83, 138);
constexpr Color strong_yellowish_pink = RGB(255, 122, 92);
constexpr Color strong_violet = RGB(83, 55, 122);
constexpr Color vivid_orange_yellow = RGB(255, 142, 0);
constexpr Color strong_purplish_red = RGB(179, 40, 81);
constexpr Color vivid_greenish_yellow = RGB(244, 200, 0);
constexpr Color strong_reddish_brown = RGB(127, 24, 13);
constexpr Color vivid_yellowish_green = RGB(147, 170, 0);
constexpr Color deep_yellowish_brown = RGB(89, 51, 21);
constexpr Color vivid_reddish_orange = RGB(241, 58, 19);
constexpr Color dark_olive_green = RGB(35, 44, 22);
} // namespace kelly

/**
 * @brief These are from the "WAD" color palette
 * @see https://alumni.media.mit.edu/~wad/color/palette.html
 *
 */
namespace wad {
constexpr Color black = RGB(0, 0, 0);
constexpr Color dark_gray = RGB(87, 87, 87);
constexpr Color red = RGB(173, 35, 35);
constexpr Color blue = RGB(42, 75, 215);
constexpr Color green = RGB(29, 105, 20);
constexpr Color brown = RGB(129, 74, 25);
constexpr Color purple = RGB(129, 38, 192);
constexpr Color light_gray = RGB(160, 160, 160);
constexpr Color light_green = RGB(129, 197, 122);
constexpr Color light_blue = RGB(157, 175, 255);
constexpr Color cyan = RGB(41, 208, 208);
constexpr Color orange = RGB(255, 146, 51);
constexpr Color yellow = RGB(255, 238, 51);
constexpr Color tan = RGB(233, 222, 187);
constexpr Color pink = RGB(255, 205, 243);
constexpr Color white = RGB(255, 255, 255);

} // namespace wad

} // namespace colors
