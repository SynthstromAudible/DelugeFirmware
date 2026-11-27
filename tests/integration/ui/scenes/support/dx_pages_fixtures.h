#pragma once

/// @file
/// @brief The fixtures of the DX7 menu pages: a runtime feature held for one example, the keyboard screen as the root
///        on the isomorphic layout, the patches the parameter page is drawn over and the keyboard layouts the
///        breakpoint pad is asked over. The spec and the menu oracle freezer share them.

#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/root_ui.h"
#include "model/clip/instrument_clip.h"
#include "model/settings/runtime_feature_settings.h"
#include "ui/scenes/menu/nodes/dx_tables.h"

#include "model/song/song.h"
#include "processing/dx7/dx_patch.h"

#include <cstdint>
#include <string>

namespace deluge::ui::scenes::spec::dxpages {

namespace dx = deluge::ui::scenes::menu::dx;

/// @brief A runtime feature set for one example, put back as it was after.
struct Feature {
	RuntimeFeatureSettingType type;
	uint32_t saved;
	Feature(RuntimeFeatureSettingType t, uint32_t value) : type(t), saved(runtimeFeatureSettings.get(t)) {
		runtimeFeatureSettings.set(t, value);
	}
	~Feature() { runtimeFeatureSettings.set(type, saved); }
	Feature(const Feature&) = delete;
	Feature& operator=(const Feature&) = delete;
};

/// @brief The keyboard screen as the root UI on the isomorphic layout, over the rig's clip, put back after.
struct IsomorphicRoot {
	RootUI* saved = getRootUI();
	InstrumentClip* clip;
	deluge::gui::ui::keyboard::KeyboardState saved_state;
	explicit IsomorphicRoot(InstrumentClip* c) : clip(c), saved_state(c->keyboardState) {
		currentSong->setCurrentClip(clip);
		setRootUILowLevel(&keyboardScreen);
		clip->keyboardState.currentLayout = KeyboardLayoutType::KeyboardLayoutTypeIsomorphic;
	}
	~IsomorphicRoot() {
		clip->keyboardState = saved_state;
		if (saved != nullptr) {
			setRootUILowLevel(saved);
		}
	}
	IsomorphicRoot(const IsomorphicRoot&) = delete;
	IsomorphicRoot& operator=(const IsomorphicRoot&) = delete;
};

/// @brief The parameter page's layouts, one panel each: the patch is filled with a repeatable spread, then @p write
///        is called whenever @p patch changed and @p shot once per panel, with its name and the param it shows.
/// @param patch The patch to change; @p write puts it where the page reads it.
template <typename Write, typename Shot>
void for_each_layout(deluge::instruments::classic::DxPatchState& patch, Write&& write, Shot&& shot) {
	for (size_t b = 0; b < 155; b++) {
		patch.params[b] = static_cast<uint8_t>((b * 7 + 3) % (dx::upper_limit(static_cast<int16_t>(b)) + 1));
	}
	patch.random_detune = 5; // one digit, which the page pads to two
	write();
	for (const int16_t param :
	     {-1,          135,         136,         3 * 21 + 2,  3 * 21 + 5,  126,         129,         0 * 21 + 13,
	      0 * 21 + 14, 2 * 21 + 15, 5 * 21 + 16, 5 * 21 + 17, 5 * 21 + 18, 5 * 21 + 19, 5 * 21 + 20, 134,
	      137,         138,         139,         140,         141,         142,         143}) {
		shot("param_" + std::to_string(param), param);
	}
	for (int16_t idx = 8; idx <= 12; idx++) {
		for (uint8_t curve = 0; curve < 6; curve++) {
			patch.params[1 * 21 + 11] = curve;
			patch.params[1 * 21 + 12] = static_cast<uint8_t>(5 - curve);
			write();
			shot("scaling_" + std::to_string(idx) + "_curve_" + std::to_string(curve),
			     static_cast<int16_t>(1 * 21 + idx));
		}
	}
	for (const uint8_t mode : {0, 1}) {
		patch.params[4 * 21 + 17] = mode;
		write();
		shot("operator_mode_" + std::to_string(mode), 4 * 21 + 18);
	}
	for (uint8_t algorithm = 0; algorithm < 32; algorithm++) {
		patch.params[134] = algorithm;
		write();
		shot("algorithm_" + std::to_string(algorithm), 134);
	}
	for (uint8_t shape = 0; shape < 7; shape++) {
		patch.params[142] = shape;
		patch.params[141] = shape & 1;
		write();
		shot("lfo_shape_" + std::to_string(shape), 142);
	}
}

/// @brief The keyboard scrolls and row intervals the breakpoint pad is asked over, and the breakpoint notes.
inline constexpr int32_t kScrolls[] = {0, 12, 40, 60};
inline constexpr int32_t kIntervalFirst = 1;
inline constexpr int32_t kIntervalLast = 16;
inline constexpr int32_t kBreakpointStep = 3;
inline constexpr int32_t kBreakpointEnd = 100;

/// @brief A scaling param no pad picks: where a pad press is asked from.
inline constexpr int16_t kUntouched = 2 * 21 + 8;

} // namespace deluge::ui::scenes::spec::dxpages
