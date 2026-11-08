// The DX7 menu pages against legacy's: the parameter tables and step rules legacy's `DxParam`, `DxGlobalParams` and
// `DxOperatorParams` answer with, and the DX patch write through the app's sink.
//
// The spec may include `gui/menu_item/`; the library may not (`scripts/check_layer_rules.py`).

#include "gui/ui/menu_view_of.h"
#include "scenes/bind/write_sink.h"
#include "scenes/l10n_bridge.h"
#include "scenes/menu/action.h"
#include "scenes/menu/bind_host.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/dx.h"
#include "scenes/menu/nodes/dx_tables.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/shortcut_blink.h"
#include "scenes/menu/shortcut_overlay.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/dx/browse.h"
#include "gui/menu_item/dx/engine_select.h"
#include "gui/menu_item/dx/global_params.h"
#include "gui/menu_item/dx/operator_params.h"
#include "gui/menu_item/dx/param.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/menus.h"
#include "gui/ui/root_ui.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "hid/buttons.h"
#include "hid/display/oled.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/project_bridge/source_bridge.h"
#include "model/settings/runtime_feature_settings.h"
#include "notify/notice_queue.h"
#include "processing/dx7/dx_engine.h"
#include "processing/dx7/dx_patch.h"
#include "processing/sound/sound.h"
#include "processing/source.h"

#include "spec/gfx/support/pbm.h"
#include "spec/shell/support/fake_surfaces.h"
#include "spec/ui/support/frames.h"
#include "support/dx_rig.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <array>
#include <optional>
#include <string>

using namespace deluge::scenes;
using deluge::scenes::menu::MenuAction;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
namespace dx = deluge::scenes::menu::dx;
namespace nodes = deluge::scenes::menu::nodes;
namespace legacy = deluge::gui::menu_item;

/// @brief How many UIs legacy's stack holds (`gui/ui/ui.cpp`), which no header declares.
extern int32_t numUIsOpen;

namespace {

using namespace deluge::scenes::spec::dx_rig;

/// @brief Legacy's param page on @p param, as `readValueAgain` leaves it.
void legacy_on(int16_t param) {
	legacy::dxParam.param = param;
	legacy::dxParam.readValueAgain();
}

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

/// @brief The DX7 engine feature on, which gives the parameter page its own pads.
struct DxEngineOn : Feature {
	DxEngineOn() : Feature(RuntimeFeatureSettingType::EnableDX7Engine, RuntimeFeatureStateToggle::On) {}
};

/// @brief Legacy's `renderMainShortcutsOnly` with `soundEditor` the current UI on @p item: the overlay `main` paints.
std::array<std::array<RGB, 18>, 8> legacy_overlay(::MenuItem& item) {
	RootUI* const root = getRootUI();
	const int32_t open = numUIsOpen;
	setRootUILowLevel(&soundEditor);
	const uint8_t depth = soundEditor.navigationDepth;
	::MenuItem* const was = soundEditor.menuItemNavigationRecord[depth];
	soundEditor.menuItemNavigationRecord[depth] = &item;
	RGB image[8][18]{};
	uint8_t occupancy[8][18]{};
	SoundEditor::renderMainShortcutsOnly(nullptr, image, occupancy, false);
	soundEditor.menuItemNavigationRecord[depth] = was;
	if (root != nullptr) {
		setRootUILowLevel(root);
	}
	numUIsOpen = open;
	std::array<std::array<RGB, 18>, 8> out{};
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < 18; x++) {
			out[y][x] = image[y][x];
		}
	}
	return out;
}

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

} // namespace

// clang-format off
describe dx_pages("the DX7 menu pages", ${
	it("give every param legacy's upper limit and title", _ {
		DxRig r;
		for (int16_t p = dx::kRandomDetune; p <= dx::kLastParam; p++) {
			legacy_on(p);
			expect(static_cast<int32_t>(dx::upper_limit(p))).to_equal(legacy::dxParam.upper_limit);
			std::array<char, 32> buf{};
			const size_t n = dx::title(p, buf);
			expect(std::string{buf.data(), n}).to_equal(std::string{legacy::dxParam.getTitle()});
		}
	});

	it("send each row of DX7 parameters where legacy's select does", _ {
		DxRig r;
		for (int32_t row = 0; row < dx::kGlobalRows; row++) {
			legacy::dxGlobalParams.currentValue = row;
			const ::MenuItem* next = legacy::dxGlobalParams.selectButtonPress();
			const int16_t mine = dx::global_row_param(row);
			if (mine >= 0) {
				expect(next == &legacy::dxParam).to_be_true();
				expect(legacy::dxParam.param).to_equal(static_cast<int>(mine));
			}
			else {
				expect(next == &legacy::dxOperatorParams).to_be_true();
				expect(legacy::dxOperatorParams.op).to_equal(6 + mine);
			}
		}
	});

	it("send each row of every DX7 operator where legacy's select does", _ {
		DxRig r;
		for (int32_t op = 0; op < 6; op++) {
			for (int32_t row = 0; row < dx::kOperatorRows; row++) {
				legacy::dxOperatorParams.op = op;
				legacy::dxOperatorParams.currentValue = row;
				expect(legacy::dxOperatorParams.selectButtonPress() == &legacy::dxParam).to_be_true();
				expect(legacy::dxParam.param).to_equal(static_cast<int>(dx::operator_row_param(op, row)));
			}
		}
	});

	it("step a horizontal turn as legacy does, wrapping between the random detune and the last param", _ {
		DxRig r;
		for (int16_t p = dx::kRandomDetune; p <= dx::kLastParam; p++) {
			for (const int32_t offset : {-1, 1}) {
				legacy_on(p);
				legacy::dxParam.horizontalEncoderAction(offset);
				expect(static_cast<int32_t>(dx::step_param(p, offset))).to_equal(legacy::dxParam.param);
			}
		}
	});

	it("step Shift + a horizontal turn to the next switched-on operator as legacy does", _ {
		DxRig r;
		for (int32_t mask = 0; mask < 64; mask++) {
			r.patch().params[dx::kOperatorSwitch] = static_cast<uint8_t>(mask);
			for (int16_t p = dx::kRandomDetune; p <= dx::kLastParam; p++) {
				if (mask == 0 && p == dx::kFirstGlobal) {
					continue; // legacy's search never ends here; the next example covers it
				}
				for (const int32_t offset : {-1, 1}) {
					legacy_on(p);
					DxRig::set_shift(true);
					legacy::dxParam.horizontalEncoderAction(offset);
					DxRig::set_shift(false);
					const std::optional<int16_t> mine = dx::step_operator(p, offset, r.patch().params);
					expect(static_cast<int32_t>(mine.value_or(p))).to_equal(legacy::dxParam.param);
				}
			}
		}
	});

	it("leave the pitch envelope's first rate alone under Shift with every operator off, where legacy hangs", _ {
		DxRig r;
		r.patch().params[dx::kOperatorSwitch] = 0;
		expect(dx::step_operator(dx::kFirstGlobal, 1, r.patch().params).has_value()).to_be_false();
		expect(dx::step_operator(dx::kFirstGlobal, -1, r.patch().params).has_value()).to_be_false();
		r.patch().params[dx::kOperatorSwitch] = 0b000100;
		expect(static_cast<int32_t>(dx::step_operator(dx::kFirstGlobal, 1, r.patch().params).value_or(-2))).to_equal(2 * 21);
	});

	it("name the scaling curves and LFO waveforms as legacy's page does, past the end as its last", _ {
		expect(std::string{dx::curve_name(0)}).to_equal(std::string{"lin-"});
		expect(std::string{dx::curve_name(3)}).to_equal(std::string{"lin+"});
		expect(std::string{dx::curve_name(9)}).to_equal(std::string{"????"});
		expect(std::string{dx::lfo_shape(1)}).to_equal(std::string{"saw down"});
		expect(std::string{dx::lfo_shape(5)}).to_equal(std::string{"s-hold"});
		expect(std::string{dx::lfo_shape(200)}).to_equal(std::string{"s-hold"});
	});

	it("makes the source's patch the one written, with no undo record", _ {
		DxRig r;
		actionLogger.deleteAllLogs();
		deluge::instrument::DxPatchState want = deluge::bridge::dxPatchOf(r.sound->sources[0]).value();
		want.params[134] = 17;
		want.params[0] = 3;
		want.engine_mode = 2;
		want.random_detune = 42;
		expect(menu::host_write_sink().write_dx_patch(r.slot(), 0, want)).to_be_true();
		expect(deluge::bridge::dxPatchOf(r.sound->sources[0]) == std::optional{want}).to_be_true();
		expect(static_cast<int32_t>(r.patch().engineMode)).to_equal(2);
		expect(r.patch().random_detune).to_equal(42);
		expect(actionLogger.firstAction[BEFORE] == nullptr).to_be_true();
	});

	it("leaves the engine the auto mode chose, as main's page does, where the new algorithm would choose another", _ {
		DxRig r;
		deluge::instrument::DxPatchState want = deluge::bridge::dxPatchOf(r.sound->sources[0]).value();
		expect(static_cast<int32_t>(want.engine_mode)).to_equal(0);
		const auto before = r.patch().engine;
		want.params[134] = 3; // algorithm 4 with feedback: `updateEngineMode` would choose MkI
		want.params[135] = 5;
		expect(menu::host_write_sink().write_dx_patch(r.slot(), 0, want)).to_be_true();
		expect(r.patch().engine == before).to_be_true();
	});

	it("refuses a source that is not a DX7 one", _ {
		DxRig r;
		const deluge::instrument::DxPatchState want = deluge::bridge::dxPatchOf(r.sound->sources[0]).value();
		expect(menu::host_write_sink().write_dx_patch(r.slot(), 1, want)).to_be_false();
	expect(menu::host_write_sink().write_dx_patch(r.slot(), 2, want)).to_be_false(); // past the two sources
	deluge::project::SlotAddress nowhere = r.slot();
	nowhere.id += 1000;
	expect(menu::host_write_sink().write_dx_patch(nowhere, 0, want)).to_be_false();
	});

	it("enters DX7 source on SELECT from oscillator 1's type set to DX7, on its page and on the source page", _ {
		{
			MenuRig r;
			r.open(nodes::osc0_type);
			r.select();
			expect(r.focused() == &nodes::dx_menu).to_be_true();
		}
		{
			MenuRig r;
			r.open(nodes::source0); // the page opens on its first column, the type
			r.select();
			expect(r.focused() == &nodes::dx_menu).to_be_true();
		}
	});

	it("goes up on SELECT from any other oscillator type, as legacy's type answers nothing", _ {
		MenuRig r;
		r.sound->sources[0].setOscType(OscType::SAW);
		r.open(nodes::source0);
		r.select();
		expect(r.focused() != &nodes::dx_menu).to_be_true();
		expect(nodes::osc1_type_chain(r.menu_context(), r.state()) == nullptr).to_be_true();
	});

	it("a re-read of the engine version takes the patch's mode again", _ {
		// `DxEngineSelect::readValueAgain`: `currentValue` is the patch's `engineMode`, read afresh.
		MenuRig r;
		r.open(nodes::dx_engine_select);
		expect(static_cast<int32_t>(r.cursor())).to_equal(0);
		deluge::instrument::DxPatchState vintage = deluge::bridge::dxPatchOf(r.sound->sources[0]).value();
		vintage.engine_mode = 2;
		expect(menu::host_write_sink().write_dx_patch(r.slot(), 0, vintage)).to_be_true();
		menu_scene().read_value_again();
		expect(static_cast<int32_t>(r.cursor())).to_equal(2);
	});

	it("a re-read of the parameter page sets its pad blink up and restarts the side column, as DxParam::readValueAgain", _ {
		MenuRig r;
		r.open(nodes::dx_param);
		menu_scene().state_for_spec().dx.pad_blink = false;
		menu_scene().state_for_spec().dx.flash_restart = false;
		menu_scene().read_value_again();
		expect(r.state().dx.pad_blink).to_be_true();
		expect(r.state().dx.flash_restart).to_be_true();
	});

	it("lists DX7 source's three rows as legacy's dxMenu does", _ {
		const ::MenuItem* const legacy_rows[] = {&legacy::dxBrowseMenu, &legacy::dxGlobalParams, &legacy::dxEngineSelect};
		expect(std::size(nodes::kDxMenuChildren)).to_equal(std::size(legacy_rows));
		for (size_t i = 0; i < std::size(legacy_rows); i++) {
			const std::string mine{text_lookup(nodes::kDxMenuChildren[i]->name)};
			expect(mine).to_equal(std::string{legacy_rows[i]->getName()});
		}
		expect(std::string{text_lookup(nodes::dx_menu.title)}).to_equal(std::string{::dxMenu.getTitle()});
	});

	it("puts the engine version's cursor on the patch's mode and writes a turn's mode, stopping at either end", _ {
		MenuRig r;
		deluge::instrument::DxPatchState modern = deluge::bridge::dxPatchOf(r.sound->sources[0]).value();
		modern.engine_mode = 1;
		expect(menu::host_write_sink().write_dx_patch(r.slot(), 0, modern)).to_be_true();
		bind::RecordingSink sink;
		r.open(nodes::dx_engine_select, &sink);
		expect(static_cast<int32_t>(r.cursor())).to_equal(1);
		r.turn(1);
		expect(static_cast<int32_t>(r.cursor())).to_equal(2);
		expect(sink.gestures.size()).to_equal(size_t{1});
		expect(sink.gestures.back().kind == bind::GestureRequest::Kind::DX_PATCH).to_be_true();
		expect(static_cast<int32_t>(sink.gestures.back().dx_patch->engine_mode)).to_equal(2);
		expect(sink.gestures.back().dx_patch->params == r.patch().params).to_be_true();
		r.turn(1);
		expect(static_cast<int32_t>(r.cursor())).to_equal(2);
		expect(sink.gestures.size()).to_equal(size_t{1});
		r.turn(-3);
		expect(static_cast<int32_t>(r.cursor())).to_equal(2); // legacy drops a turn that would leave the modes
		expect(sink.gestures.size()).to_equal(size_t{1});
	});

	it("turns the engine version's list five steps under Shift, and drops a turn that would leave the modes", _ {
		// `SoundEditor::selectEncoderAction` hands `DxEngineSelect` the offset times 5; it does nothing out of range.
		MenuRig r;
		bind::RecordingSink sink;
		r.open(nodes::dx_engine_select, &sink);
		r.button(kShiftX, kShiftY, true);
		r.turn(1);
		expect(static_cast<int32_t>(r.cursor())).to_equal(0);
		expect(sink.gestures.empty()).to_be_true();
		r.button(kShiftX, kShiftY, false);
	});

	it("turns the DX7 parameter and operator lists five rows under Shift, as their clamp reads it", _ {
		// `DxGlobalParams` and `DxOperatorParams` clamp `currentValue + offset`, on the scaled offset.
		for (const Node* page : {&nodes::dx_global_params, &nodes::dx_operator_params}) {
			MenuRig r;
			r.open(*page);
			r.button(kShiftX, kShiftY, true);
			r.turn(1);
			expect(static_cast<int32_t>(r.cursor())).to_equal(5);
			r.button(kShiftX, kShiftY, false);
		}
	});

	it("turns the DX7 value five steps under Shift, then clamps to its limit", _ {
		// `DxParam::selectEncoderAction` adds the scaled offset to the value and clamps it to `upper_limit`.
		MenuRig r;
		deluge::instrument::DxPatchState patch = deluge::bridge::dxPatchOf(r.sound->sources[0]).value();
		patch.params[134] = 3;
		expect(menu::host_write_sink().write_dx_patch(r.slot(), 0, patch)).to_be_true();
		bind::RecordingSink sink;
		menu_scene().land_dx(menu::DxState{.param = 134});
		r.open(nodes::dx_param, &sink);
		r.button(kShiftX, kShiftY, true);
		r.turn(1);
		expect(static_cast<int32_t>(sink.gestures.back().dx_patch->params[134])).to_equal(8);
		r.turn(10);
		expect(static_cast<int32_t>(sink.gestures.back().dx_patch->params[134])).to_equal(31);
		r.button(kShiftX, kShiftY, false);
	});

	it("opens the parameter page or an operator's list from DX7 parameters, as legacy's rows do", _ {
		MenuRig r;
		r.open(nodes::dx_global_params);
		r.select();
		expect(r.focused() == &nodes::dx_param).to_be_true();
		expect(static_cast<int32_t>(r.state().dx.param)).to_equal(134);
		r.back();
		expect(r.focused() == &nodes::dx_global_params).to_be_true();
		r.turn(4); // "operator 2", which is operator index 4: the DX7 counts its operators from the other end
		r.select();
		expect(r.focused() == &nodes::dx_operator_params).to_be_true();
		expect(static_cast<int32_t>(r.state().dx.op)).to_equal(4);
		r.turn(1); // "level"
		r.select();
		expect(r.focused() == &nodes::dx_param).to_be_true();
		expect(static_cast<int32_t>(r.state().dx.param)).to_equal(4 * 21 + 16);
	});

	it("draws every parameter layout as legacy's DxParam::renderOLED does", _ {
		select_language();
		MenuRig r;
		const auto shot = [&](int16_t param) {
			legacy_on(param);
			deluge::hid::display::OLED::main.clear();
			legacy::dxParam.renderOLED();
			const std::string legacy_panel = below_bezel(legacy_panel_pbm());
			menu_scene().land_dx(menu::DxState{.param = param});
			r.open(nodes::dx_param);
			r.tick();
			expect(static_cast<int32_t>(r.state().dx.param)).to_equal(static_cast<int32_t>(param));
			const std::string tag = "param " + std::to_string(param) + "\n";
			expect(tag + below_bezel(r.fake.pbm(0))).to_equal(tag + legacy_panel);
		};
		deluge::instrument::DxPatchState patch = deluge::bridge::dxPatchOf(r.sound->sources[0]).value();
		for (size_t b = 0; b < 155; b++) {
			patch.params[b] = static_cast<uint8_t>((b * 7 + 3) % (dx::upper_limit(static_cast<int16_t>(b)) + 1));
		}
		patch.random_detune = 5; // one digit, which legacy pads to two
		const auto write = [&] { expect(menu::host_write_sink().write_dx_patch(r.slot(), 0, patch)).to_be_true(); };
		write();
		for (const int16_t param : {-1, 135, 136, 3 * 21 + 2, 3 * 21 + 5, 126, 129, 0 * 21 + 13, 0 * 21 + 14,
		                            2 * 21 + 15, 5 * 21 + 16, 5 * 21 + 17, 5 * 21 + 18, 5 * 21 + 19, 5 * 21 + 20, 134,
		                            137, 138, 139, 140, 141, 142, 143}) {
			shot(param);
		}
		for (int16_t idx = 8; idx <= 12; idx++) {
			for (uint8_t curve = 0; curve < 6; curve++) {
				patch.params[1 * 21 + 11] = curve;
				patch.params[1 * 21 + 12] = static_cast<uint8_t>(5 - curve);
				write();
				shot(static_cast<int16_t>(1 * 21 + idx));
			}
		}
		for (const uint8_t mode : {0, 1}) {
			patch.params[4 * 21 + 17] = mode;
			write();
			shot(4 * 21 + 18);
		}
		for (uint8_t algorithm = 0; algorithm < 32; algorithm++) {
			patch.params[134] = algorithm;
			write();
			shot(134);
		}
		for (uint8_t shape = 0; shape < 7; shape++) {
			patch.params[142] = shape;
			patch.params[141] = shape & 1;
			write();
			shot(142);
		}
	});

	it("draws DX7 source, both lists and the engine version as legacy's items do", _ {
		select_language();
		MenuRig r;
		const auto legacy_panel = [](::MenuItem& item) {
			deluge::hid::display::OLED::main.clear();
			item.renderOLED();
			return below_bezel(legacy_panel_pbm());
		};
		const auto port_panel = [&](const Node& node) {
			r.open(node);
			r.tick();
			return below_bezel(r.fake.pbm(0));
		};
		::dxMenu.beginSession(nullptr); // its current row, as entering it on the device sets
		expect(port_panel(nodes::dx_menu)).to_equal(legacy_panel(::dxMenu));
		for (int32_t row = 0; row < dx::kGlobalRows; row++) {
			legacy::dxGlobalParams.currentValue = row;
			legacy::dxGlobalParams.scrollPos = std::clamp(row - 1, 0, dx::kGlobalRows - 3);
			menu_scene().land_dx(menu::DxState{.global_cursor = static_cast<int16_t>(row)});
			expect(port_panel(nodes::dx_global_params)).to_equal(legacy_panel(legacy::dxGlobalParams));
		}
		for (int32_t op = 0; op < 6; op++) {
			for (int32_t row = 0; row < dx::kOperatorRows; row++) {
				legacy::dxOperatorParams.op = op;
				legacy::dxOperatorParams.format(6 - op);
				legacy::dxOperatorParams.currentValue = row;
				legacy::dxOperatorParams.scrollPos = std::clamp(row - 1, 0, dx::kOperatorRows - 3);
				menu_scene().land_dx(menu::DxState{.op = static_cast<int8_t>(op), .operator_cursor = static_cast<int16_t>(row)});
				expect(port_panel(nodes::dx_operator_params)).to_equal(legacy_panel(legacy::dxOperatorParams));
			}
		}
		for (uint8_t mode = 0; mode < 3; mode++) {
			deluge::instrument::DxPatchState patch = deluge::bridge::dxPatchOf(r.sound->sources[0]).value();
			patch.engine_mode = mode;
			expect(menu::host_write_sink().write_dx_patch(r.slot(), 0, patch)).to_be_true();
			legacy::dxEngineSelect.readValueAgain();
			expect(port_panel(nodes::dx_engine_select)).to_equal(legacy_panel(legacy::dxEngineSelect));
		}
	});

	it("moves the value within its limits on a select turn and writes oscillator 1's patch", _ {
		MenuRig r;
		deluge::instrument::DxPatchState patch = deluge::bridge::dxPatchOf(r.sound->sources[0]).value();
		patch.params[134] = 29;
		patch.random_detune = 3;
		expect(menu::host_write_sink().write_dx_patch(r.slot(), 0, patch)).to_be_true();
		bind::RecordingSink sink;
		menu_scene().land_dx(menu::DxState{.param = 134});
		r.open(nodes::dx_param, &sink);
		r.turn(5);
		expect(sink.gestures.size()).to_equal(size_t{1});
		expect(sink.gestures.back().source == 0).to_be_true();
		expect(static_cast<int32_t>(sink.gestures.back().dx_patch->params[134])).to_equal(31); // the algorithm's limit
		menu_scene().land_dx(menu::DxState{.param = dx::kRandomDetune});
		r.open(nodes::dx_param, &sink);
		r.turn(-5);
		expect(sink.gestures.back().dx_patch->random_detune).to_equal(0);
		r.turn(2);
		expect(sink.gestures.back().dx_patch->random_detune).to_equal(5);
	});

	it("steps the param on a horizontal turn, wrapping, and the operator under Shift, staying put where legacy hangs", _ {
		MenuRig r;
		menu_scene().land_dx(menu::DxState{.param = dx::kLastParam});
		r.open(nodes::dx_param);
		r.turn(1, kHorizontalEncoder);
		expect(static_cast<int32_t>(r.state().dx.param)).to_equal(-1);
		r.turn(-1, kHorizontalEncoder);
		expect(static_cast<int32_t>(r.state().dx.param)).to_equal(143);

		deluge::instrument::DxPatchState patch = deluge::bridge::dxPatchOf(r.sound->sources[0]).value();
		patch.params[dx::kOperatorSwitch] = 0b100101; // operators 0, 2 and 5 on
		expect(menu::host_write_sink().write_dx_patch(r.slot(), 0, patch)).to_be_true();
		menu_scene().land_dx(menu::DxState{.param = 0 * 21 + 18});
		r.open(nodes::dx_param);
		r.button(kShiftX, kShiftY, true);
		r.turn(1, kHorizontalEncoder);
		expect(static_cast<int32_t>(r.state().dx.param)).to_equal(2 * 21 + 18);
		r.turn(-1, kHorizontalEncoder);
		r.turn(-1, kHorizontalEncoder);
		expect(static_cast<int32_t>(r.state().dx.param)).to_equal(5 * 21 + 18);
		r.button(kShiftX, kShiftY, false);

		patch.params[dx::kOperatorSwitch] = 0;
		expect(menu::host_write_sink().write_dx_patch(r.slot(), 0, patch)).to_be_true();
		menu_scene().land_dx(menu::DxState{.param = dx::kFirstGlobal});
		r.open(nodes::dx_param);
		r.button(kShiftX, kShiftY, true);
		r.turn(1, kHorizontalEncoder);
		r.button(kShiftX, kShiftY, false);
		expect(static_cast<int32_t>(r.state().dx.param)).to_equal(dx::kFirstGlobal);
	});

	it("leaves the param and the blink alone under Shift outside the operators and the pitch envelope's first rate", _ {
		MenuRig r;
		for (const int16_t param : {int16_t{137}, dx::kRandomDetune}) {
			menu_scene().land_dx(menu::DxState{.param = param});
			r.open(nodes::dx_param);
			r.button(kShiftX, kShiftY, true);
			r.turn(1, kHorizontalEncoder);
			r.button(kShiftX, kShiftY, false);
			expect(static_cast<int32_t>(r.state().dx.param)).to_equal(static_cast<int32_t>(param));
			expect(menu::plan_blinks(r.menu_context(), r.state(), nodes::dx_param) == menu::BlinkPlan{}).to_be_true();
		}
	});

	it("opens the parameter page from the modulator's shortcut inside the menu, on a DX7 sound only", _ {
		MenuRig r;
		menu_scene().land_dx(menu::DxState{.param = 40});
		r.open(nodes::dx_global_params);
		expect(nodes::dx_shortcut_fallback(nodes::modulator0_volume, r.menu_context(), 4) == &nodes::dx_param).to_be_true();
		expect(nodes::dx_shortcut_fallback(nodes::modulator0_volume, r.menu_context(), 5) == nullptr).to_be_true();
		expect(nodes::dx_shortcut_fallback(nodes::modulator1_volume, r.menu_context(), 4) == nullptr).to_be_true();
		r.button(kShiftX, kShiftY, true);
		r.shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, 4, 0, 64}, r.now += 10);
		r.tick();
		r.shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, 4, 0, 0}, r.now += 10);
		r.button(kShiftX, kShiftY, false);
		r.tick();
		expect(r.focused() == &nodes::dx_param).to_be_true();
		expect(static_cast<int32_t>(r.state().dx.param)).to_equal(40);
		r.sound->sources[0].setOscType(OscType::SAW);
		expect(nodes::dx_shortcut_fallback(nodes::modulator0_volume, r.menu_context(), 4) == nullptr).to_be_true();
	});

	it("goes up from the parameter page on SELECT and on BACK", _ {
		MenuRig r;
		r.open(nodes::dx_global_params);
		r.select();
		expect(r.focused() == &nodes::dx_param).to_be_true();
		r.select();
		expect(r.focused() == &nodes::dx_global_params).to_be_true();
		r.select();
		r.back();
		expect(r.focused() == &nodes::dx_global_params).to_be_true();
	});

	it("blinks the pad legacy's page blinks for every param, every other cycle, and none for the scaling params", _ {
		MenuRig r;
		const MenuContext ctx = r.menu_context();
		for (int16_t param = dx::kRandomDetune; param <= dx::kLastParam; param++) {
			soundEditor.currentParamShortcutX = 255; // legacy leaves the pad alone where none blinks
			soundEditor.currentParamShortcutY = 255;
			legacy_on(param);
			menu::MenuState state{};
			state.dx.param = param;
			state.dx.pad_blink = true;
			const menu::BlinkPlan plan = menu::plan_blinks(ctx, state, nodes::dx_param);
			const std::optional<dx::Pad> pad = dx::param_pad(param);
			if (soundEditor.currentParamShortcutX == 255) {
				expect(pad.has_value()).to_be_false();
				expect(plan == menu::BlinkPlan{}).to_be_true();
			}
			else {
				expect(pad.has_value()).to_be_true();
				expect(static_cast<int32_t>(pad.value_or(dx::Pad{}).x)).to_equal(soundEditor.currentParamShortcutX);
				expect(static_cast<int32_t>(pad.value_or(dx::Pad{}).y)).to_equal(soundEditor.currentParamShortcutY);
				expect(static_cast<int32_t>(soundEditor.paramShortcutBlinkFrequency)).to_equal(1);
				expect(plan == menu::exclusive_blink(soundEditor.currentParamShortcutX, soundEditor.currentParamShortcutY))
				    .to_be_true();
			}
		}
	});

	it("opens with no pad blinking, as legacy's screen entry unsets the page's blink, and blinks after a step", _ {
		MenuRig r;
		menu_scene().land_dx(menu::DxState{.param = 2 * 21 + 3, .pad_blink = true});
		r.open(nodes::dx_global_params);
		r.select(); // "algorithm"
		expect(r.focused() == &nodes::dx_param).to_be_true();
		expect(menu::plan_blinks(r.menu_context(), r.state(), nodes::dx_param) == menu::BlinkPlan{}).to_be_true();
		r.turn(1, kHorizontalEncoder);
		expect(menu::plan_blinks(r.menu_context(), r.state(), nodes::dx_param) == menu::exclusive_blink(9, 1))
		    .to_be_true(); // feedback's pad
		r.turn(1); // an edit leaves the blink as it is
		expect(menu::plan_blinks(r.menu_context(), r.state(), nodes::dx_param) == menu::exclusive_blink(9, 1))
		    .to_be_true();
	});

	it("keeps both lists' cursors across leaving them and across another menu, the operators sharing one", _ {
		MenuRig r;
		r.open(nodes::dx_global_params);
		r.turn(4); // "operator 2"
		r.select();
		r.turn(3); // "velocity sense"
		r.back();
		expect(static_cast<int32_t>(r.cursor())).to_equal(4);
		r.turn(1); // "operator 3"
		r.select();
		expect(static_cast<int32_t>(r.cursor())).to_equal(3); // the one cursor the six operators share
		r.open(nodes::dx_global_params); // a new menu, as legacy's singletons outlive the editor
		expect(static_cast<int32_t>(r.cursor())).to_equal(5);
		r.open(nodes::dx_operator_params);
		expect(static_cast<int32_t>(r.cursor())).to_equal(3);
	});
	it("picks the param legacy's DX7 pad layout picks for every main pad, the inverse of the blink's pad", _ {
		DxRig r;
		Popups p; // `flashParamName` asks the display whether it is a 7-segment one
		constexpr int16_t kUntouched = 2 * 21 + 8; // a scaling param: no pad picks it
		for (uint8_t x = 0; x < 16; x++) {
			for (uint8_t y = 0; y < 8; y++) {
				legacy_on(kUntouched);
				(void)legacy::dxParam.potentialShortcutPadAction(x, y, true);
				const std::optional<int16_t> mine = dx::pad_param(x, y);
				const std::string tag = "pad " + std::to_string(x) + "," + std::to_string(y) + " ";
				expect(tag + std::to_string(mine.value_or(kUntouched)))
				    .to_equal(tag + std::to_string(legacy::dxParam.param));
				if (mine) {
					if (const std::optional<dx::Pad> back = dx::param_pad(*mine)) {
						expect(*back == dx::Pad{x, y}).to_be_true();
					}
				}
			}
		}
		expect(dx::pad_param(16, 4).has_value()).to_be_false(); // the sidebar
	});

	it("takes every Shift + pad on the parameter page with the DX7 engine on: a pad picks its param and blinks", _ {
		select_language();
		DxEngineOn on;
		MenuRig r;
		menu_scene().land_dx(menu::DxState{.param = 16});
		r.open(nodes::dx_param);
		r.shift_pad(3, 4); // operator index 3's rate 4
		expect(r.focused() == &nodes::dx_param).to_be_true();
		expect(static_cast<int32_t>(r.state().dx.param)).to_equal(3 * 21 + 3);
		expect(r.state().dx.pad_blink).to_be_true();
		r.shift_pad(5, 1); // the pitch envelope's level 2
		expect(static_cast<int32_t>(r.state().dx.param)).to_equal(131);
		r.shift_pad(7, 0); // random detune
		expect(static_cast<int32_t>(r.state().dx.param)).to_equal(-1);
		r.shift_pad(12, 0); // no param: swallowed, the page stays on the param
		expect(r.focused() == &nodes::dx_param).to_be_true();
		expect(static_cast<int32_t>(r.state().dx.param)).to_equal(-1);
	});

	it("leaves the pads the menu's shortcuts on the parameter page with the DX7 engine off", _ {
		select_language();
		Feature off(RuntimeFeatureSettingType::EnableDX7Engine, RuntimeFeatureStateToggle::Off);
		MenuRig r;
		menu_scene().land_dx(menu::DxState{.param = 16});
		r.open(nodes::dx_param);
		r.shift_pad(3, 4);
		expect(r.focused() == &nodes::dx_param).to_be_false();
		expect(static_cast<int32_t>(r.state().dx.param)).to_equal(16);
	});

	it("paints main's DX7 colour map as the overlay on the parameter page, and the ordinary one elsewhere", _ {
		select_language();
		DxEngineOn on;
		MenuRig r;
		const MenuContext ctx = r.menu_context();
		const auto painted = [&](const Node& page) {
			menu::MenuState st{};
			st.path[0] = &page;
			deluge::ui::test::GridRig rig;
			menu::render_overlay(rig.frame, ctx, st, nullptr);
			std::array<std::array<RGB, 18>, 8> out{};
			for (uint8_t y = 0; y < 8; y++) {
				for (uint8_t x = 0; x < 16; x++) {
					out[y][x] = rig.frame.peek(x, y);
				}
			}
			return out;
		};
		std::array<std::array<RGB, 18>, 8> want = legacy_overlay(legacy::dxParam);
		for (auto& row : want) {
			row[16] = RGB{}; // the overlay paints the main pads only
			row[17] = RGB{};
		}
		const auto dx_map = painted(nodes::dx_param);
		expect(dx_map == want).to_be_true();
		expect(painted(nodes::dx_global_params) == dx_map).to_be_false();
	});

	it("shows the DX7 colour map under Shift on the parameter page", _ {
		select_language();
		DxEngineOn on;
		Feature overlay(RuntimeFeatureSettingType::ShortcutOverlay,
		                static_cast<uint32_t>(RuntimeFeatureStateShortcutOverlay::MenuOnly));
		MenuRig r;
		const auto shown = [&] {
			r.button(kShiftX, kShiftY, true);
			r.tick();
			r.tick();
			std::array<std::array<RGB, 16>, 8> out{};
			for (uint8_t y = 0; y < 8; y++) {
				for (uint8_t x = 0; x < 16; x++) {
					out[y][x] = r.fake.grid().grid().at(x, y);
				}
			}
			r.button(kShiftX, kShiftY, false);
			r.tick();
			return out;
		};
		menu_scene().land_dx(menu::DxState{.param = 16});
		r.open(nodes::dx_param);
		const auto on_page = shown();
		r.open(nodes::dx_global_params);
		const auto elsewhere = shown();
		expect(on_page == elsewhere).to_be_false();
		// The LFO row's first pad: pastel blue in the DX7 map, dimmed as every overlay pad is.
		expect(on_page[0][0] == elsewhere[0][0]).to_be_false();
	});
	it("moves the param as legacy's openForOpOrGlobal does for every DX column pad, on the page and off it", _ {
		DxRig r;
		RootUI* const root = getRootUI();
		const int32_t open = numUIsOpen;
		setRootUILowLevel(&soundEditor); // the editor is open...
		const uint8_t depth = soundEditor.navigationDepth;
		::MenuItem* const was = soundEditor.menuItemNavigationRecord[depth];
		soundEditor.menuItemNavigationRecord[depth] = &legacy::dxParam; // ...on the parameter page
		for (int32_t op = 0; op < 8; op++) {
			// Off the page, legacy's `setup()` opens it and the param is the operator's level, or the algorithm.
			expect(static_cast<int32_t>(dx::column_param(5, op, false))).to_equal(op < 6 ? op * 21 + 16 : 134);
			for (int16_t p = dx::kRandomDetune; p <= dx::kLastParam; p++) {
				legacy_on(p);
				legacy::dxParam.openForOpOrGlobal(op);
				const std::string tag = "op " + std::to_string(op) + " from " + std::to_string(p) + " ";
				expect(tag + std::to_string(dx::column_param(p, op, true)))
				    .to_equal(tag + std::to_string(legacy::dxParam.param));
			}
		}
		soundEditor.menuItemNavigationRecord[depth] = was;
		if (root != nullptr) {
			setRootUILowLevel(root);
		}
		numUIsOpen = open;
	});

	it("toggles on the page and moves the menu there from elsewhere on a DX column press, its pad blinking", _ {
		select_language();
		MenuRig r;
		menu_scene().land_dx(menu::DxState{.param = 16});
		r.open(nodes::dx_param);
		menu_scene().dx_column_press(0); // operator index 0's level again: its coarse tuning
		r.tick();
		expect(static_cast<int32_t>(r.state().dx.param)).to_equal(18);
		expect(r.state().dx.pad_blink).to_be_true();
		r.open(nodes::dx_global_params);
		menu_scene().dx_column_press(4); // from another page: the parameter page, on operator index 4's level
		r.tick();
		r.tick();
		expect(r.focused() == &nodes::dx_param).to_be_true();
		expect(static_cast<int32_t>(r.state().dx.param)).to_equal(4 * 21 + 16);
		expect(r.state().dx.pad_blink).to_be_true();
	});
	it("flashes the row legacy's readValueAgain flashes for every param", _ {
		DxRig r;
		for (int16_t p = dx::kRandomDetune; p <= dx::kLastParam; p++) {
			legacy_on(p);
			const std::string tag = "param " + std::to_string(p) + " ";
			expect(tag + std::to_string(dx::flash_row(p))).to_equal(tag + std::to_string(legacy::dxParam.flash_row));
		}
	});

	it("blinks the breakpoint note's pad where legacy's renderScaling does, over every layout of the keyboard", _ {
		select_language();
		DxRig r;
		IsomorphicRoot root{r.song.clip};
		for (const int32_t scroll : {0, 12, 40, 60}) {
			for (int32_t interval = 1; interval <= 16; interval++) {
				r.song.clip->keyboardState.isomorphic.scrollOffset = scroll;
				r.song.clip->keyboardState.isomorphic.rowInterval = interval;
				for (int32_t breakpoint = 0; breakpoint < 100; breakpoint += 3) {
					r.patch().params[2 * 21 + 8] = static_cast<uint8_t>(breakpoint);
					legacy_on(2 * 21 + 8);
					soundEditor.currentParamShortcutX = 255;
					uiTimerManager.setTimer(TimerName::SHORTCUT_BLINK, 1000);
					legacy::dxParam.renderOLED();
					const std::optional<dx::Pad> mine = dx::breakpoint_pad(static_cast<uint8_t>(breakpoint), scroll,
					                                                       interval);
					const std::string tag = std::to_string(scroll) + "/" + std::to_string(interval) + "/"
					                        + std::to_string(breakpoint) + " ";
					if (mine) {
						expect(tag + std::to_string(mine->x) + "," + std::to_string(mine->y))
						    .to_equal(tag + std::to_string(soundEditor.currentParamShortcutX) + ","
						              + std::to_string(soundEditor.currentParamShortcutY));
						expect(static_cast<int32_t>(soundEditor.paramShortcutBlinkFrequency)).to_equal(2);
					}
					else {
						expect(tag + std::to_string(uiTimerManager.isTimerSet(TimerName::SHORTCUT_BLINK)))
						    .to_equal(tag + "0");
					}
				}
			}
		}
	});

	it("blinks the breakpoint's pad on a keyboard scaling param over the isomorphic keyboard, at legacy's frequency", _ {
		select_language();
		MenuRig r;
		IsomorphicRoot root{r.song.clip};
		r.song.clip->keyboardState.isomorphic.scrollOffset = 40;
		r.song.clip->keyboardState.isomorphic.rowInterval = 5;
		r.patch().params[1 * 21 + 8] = 45;
		deluge::instrument::DxPatchState patch = deluge::bridge::dxPatchOf(r.sound->sources[0]).value();
		expect(menu::host_write_sink().write_dx_patch(r.slot(), 0, patch)).to_be_true();
		menu_scene().land_dx(menu::DxState{.param = 1 * 21 + 10});
		r.open(nodes::dx_param);
		const std::optional<menu::ItemBlink> blink = nodes::kDxParam.own_blink(r.menu_context(), r.state());
		const std::optional<dx::Pad> want = dx::breakpoint_pad(45, 40, 5);
		expect(want.has_value()).to_be_true();
		expect(blink.has_value() && blink->valid).to_be_true();
		expect(blink->x == want->x && blink->y == want->y).to_be_true();
		expect(static_cast<int32_t>(blink->freq_mask)).to_equal(2);
		// Off the scaling params the page's own rule holds.
		menu_scene().land_dx(menu::DxState{.param = 1 * 21 + 3});
		r.open(nodes::dx_param);
		const std::optional<menu::ItemBlink> other = nodes::kDxParam.own_blink(r.menu_context(), r.state());
		expect(other.has_value() && !other->valid).to_be_true(); // the page opens with no blink
	});

	it("flashes the edited operator's column row white for 100 ms in every 400 over the keyboard, from each change", _ {
		select_language();
		MenuRig r;
		menu_scene().land_dx(menu::DxState{.param = 2 * 21 + 16});
		r.open(nodes::dx_param, nullptr, /*keyboard_root=*/true);
		expect(r.state().dx.flash_anchor_ms.has_value()).to_be_true();
		const uint32_t anchor = *r.state().dx.flash_anchor_ms;
		const auto row = [&](uint32_t at) { return menu_scene().dx_column_flash(anchor + at).value_or(-1); };
		expect(row(0)).to_equal(5); // operator index 2's row, `7 - op`
		expect(row(99)).to_equal(5);
		expect(row(100)).to_equal(-1);
		expect(row(399)).to_equal(-1);
		expect(row(400)).to_equal(5);
		// A horizontal step restarts it, on the new param's row.
		r.now += 250;
		r.turn(1, kHorizontalEncoder);  // to the operator's mode...
		r.turn(-1, kHorizontalEncoder); // ...and back to its level
		expect(*r.state().dx.flash_anchor_ms > anchor).to_be_true();
		const uint32_t again = *r.state().dx.flash_anchor_ms;
		expect(menu_scene().dx_column_flash(again).value_or(-1)).to_equal(5);
		// Not over another root.
		r.open(nodes::dx_param);
		expect(menu_scene().dx_column_flash(*r.state().dx.flash_anchor_ms).has_value()).to_be_false();
	});
});
// clang-format on

CPPSPEC_SPEC(dx_pages)
