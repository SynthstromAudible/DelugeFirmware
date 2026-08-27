#pragma once
#include "rgb.h"

namespace deluge::gui::colours {
using Colour = RGB;

// Standard palette, with some modifications for the deluge (see _full)
constexpr Colour black = RGB::monochrome(0);
constexpr Colour grey = RGB::monochrome(7);
constexpr Colour white_full = RGB::monochrome(255);
constexpr Colour white = white_full.dim();
constexpr Colour red = RGB(255, 0, 0);
constexpr Colour red_dull = RGB(60, 15, 15);
constexpr Colour red_orange = RGB(255, 64, 0);
constexpr Colour orange = RGB(255, 128, 0);
constexpr Colour yellow_orange = RGB(255, 160, 0);
constexpr Colour yellow = RGB(255, 255, 0);
constexpr Colour lime = RGB(128, 255, 0);
constexpr Colour green = RGB(0, 255, 0);
constexpr Colour turquoise = RGB(0, 255, 128);
constexpr Colour cyan_full = RGB(0, 255, 255);
constexpr Colour cyan = RGB(0, 128, 128);
constexpr Colour darkblue = RGB(0, 128, 255);
constexpr Colour blue = RGB(0, 0, 255);
constexpr Colour purple = RGB(128, 0, 255);
constexpr Colour magenta_full = RGB(255, 0, 255);
constexpr Colour magenta = RGB(128, 0, 128);
constexpr Colour magenta_dull = RGB(60, 15, 60);
constexpr Colour pink_full = RGB(255, 128, 128);
constexpr Colour pink = RGB(255, 44, 50);
constexpr Colour amber = RGB(255, 48, 0);

// These colours are used globally by the deluge
constexpr Colour disabled = colours::red;
constexpr Colour group_enabled = colours::green;
constexpr Colour enabled = RGB(0, 255, 6);
constexpr Colour muted = colours::yellow_orange;
constexpr Colour midi_command = RGB(255, 80, 120);
constexpr Colour midi_no_command = RGB::monochrome(60);
constexpr Colour selected_drum = RGB(30, 30, 10);

namespace pastel {
constexpr Colour orange = RGB(221, 72, 13);
constexpr Colour yellow = RGB(170, 182, 0);
constexpr Colour green = RGB(85, 182, 72);
constexpr Colour blue = RGB(51, 109, 145);
constexpr Colour pink = RGB(144, 72, 91);

// Custom Tail colours for Performance View by @seangoodvibes
constexpr Colour orangeTail = RGB(46, 16, 2);
constexpr Colour pinkTail = RGB(37, 15, 37);
} // namespace pastel

/**
 * @brief These are from "Twenty-two Colours of Maximum Contrast" by Kelly
 * @see http://www.iscc-archive.org/pdf/PC54_1724_001.pdf
 *
 */
namespace kelly {
constexpr Colour vivid_yellow = RGB(255, 179, 0);
constexpr Colour strong_purple = RGB(128, 62, 117);
constexpr Colour vivid_orange = RGB(255, 104, 0);
constexpr Colour very_light_blue = RGB(166, 189, 215);
constexpr Colour vivid_red = RGB(193, 0, 32);
constexpr Colour grayish_yellow = RGB(206, 162, 98);
constexpr Colour medium_gray = RGB(129, 112, 102);

// these aren't good for people with defective colour vision:
constexpr Colour vivid_green = RGB(0, 125, 52);
constexpr Colour strong_purplish_pink = RGB(246, 118, 142);
constexpr Colour strong_blue = RGB(0, 83, 138);
constexpr Colour strong_yellowish_pink = RGB(255, 122, 92);
constexpr Colour strong_violet = RGB(83, 55, 122);
constexpr Colour vivid_orange_yellow = RGB(255, 142, 0);
constexpr Colour strong_purplish_red = RGB(179, 40, 81);
constexpr Colour vivid_greenish_yellow = RGB(244, 200, 0);
constexpr Colour strong_reddish_brown = RGB(127, 24, 13);
constexpr Colour vivid_yellowish_green = RGB(147, 170, 0);
constexpr Colour deep_yellowish_brown = RGB(89, 51, 21);
constexpr Colour vivid_reddish_orange = RGB(241, 58, 19);
constexpr Colour dark_olive_green = RGB(35, 44, 22);
} // namespace kelly

/**
 * @brief These are from the "WAD" colour palette
 * @see https://alumni.media.mit.edu/~wad/colour/palette.html
 *
 */
namespace wad {
constexpr Colour black = RGB(0, 0, 0);
constexpr Colour dark_gray = RGB(87, 87, 87);
constexpr Colour red = RGB(173, 35, 35);
constexpr Colour blue = RGB(42, 75, 215);
constexpr Colour green = RGB(29, 105, 20);
constexpr Colour brown = RGB(129, 74, 25);
constexpr Colour purple = RGB(129, 38, 192);
constexpr Colour light_gray = RGB(160, 160, 160);
constexpr Colour light_green = RGB(129, 197, 122);
constexpr Colour light_blue = RGB(157, 175, 255);
constexpr Colour cyan = RGB(41, 208, 208);
constexpr Colour orange = RGB(255, 146, 51);
constexpr Colour yellow = RGB(255, 238, 51);
constexpr Colour tan = RGB(233, 222, 187);
constexpr Colour pink = RGB(255, 205, 243);
constexpr Colour white = RGB(255, 255, 255);

} // namespace wad

} // namespace deluge::gui::colours
