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
#include "gui/ui/menus.h"
#include "gui/ui/sound_editor.h"
#include "hid/buttons.h"
#include "hid/display/oled.h"
#include "model/action/action_logger.h"
#include "model/project_bridge/source_bridge.h"
#include "notify/notice_queue.h"
#include "processing/dx7/dx_engine.h"
#include "processing/dx7/dx_patch.h"
#include "processing/sound/sound.h"
#include "processing/source.h"

#include "spec/gfx/support/pbm.h"
#include "spec/shell/support/fake_surfaces.h"
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

/// @brief Legacy's held-Shift state, which `Buttons::isShiftButtonPressed` reads and `hid/buttons.h` does not declare.
namespace Buttons {
extern bool shiftCurrentlyPressed;
extern bool shiftCurrentlyStuck;
} // namespace Buttons

namespace {

/// @brief A synth whose first oscillator is DX7, set up for legacy's DX7 items.
///
/// Legacy's items read `soundEditor.currentSound`; `setOscType` gives the source its patch, as it does on the device.
struct DxRig {
	spec::SongFixture song = spec::make_synth_song();
	Song* saved = currentSong;
	Sound* sound = nullptr;

	DxRig() {
		currentSong = song.song;
		sound = static_cast<Sound*>(static_cast<ModControllableAudio*>(song.instrument->toModControllable()));
		sound->sources[0].setOscType(OscType::DX7);
		soundEditor.currentSound = sound;
		soundEditor.currentSource = &sound->sources[0];
		soundEditor.currentSourceIndex = 0;
	}
	~DxRig() {
		set_shift(false);
		currentSong = saved;
	}
	DxRig(const DxRig&) = delete;
	DxRig& operator=(const DxRig&) = delete;

	DxPatch& patch() { return *sound->sources[0].ensureDxPatch(); }

	deluge::project::SlotAddress slot() {
		return *menu::context_for(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false)
		            .addresses.slot;
	}

	/// @brief Hold or release Shift on legacy's side, which is all `isShiftButtonPressed` reads.
	static void set_shift(bool on) {
		::Buttons::shiftCurrentlyPressed = on;
		::Buttons::shiftCurrentlyStuck = false;
	}
};

/// @brief Legacy's param page on @p param, as `readValueAgain` leaves it.
void legacy_on(int16_t param) {
	legacy::dxParam.param = param;
	legacy::dxParam.readValueAgain();
}

/// @brief The language the port's and legacy's text come from, and no notice left over from another example.
void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::notify::Notice discard;
	while (deluge::notify::notices().pop(discard)) {}
}

/// @brief `OLED::main`, into which every legacy `renderOLED()` draws, as a PBM.
std::string legacy_panel_pbm() {
	const auto& canvas = deluge::hid::display::OLED::main;
	return page_major_to_pbm(canvas.bytes().data(), OLED_MAIN_WIDTH_PIXELS, OLED_MAIN_HEIGHT_PIXELS);
}

/// @brief @p pbm without the five rows above the 48-row panel's visible top, which the port's root frame cannot ink.
std::string below_bezel(const std::string& pbm) {
	size_t pos = 0;
	for (int newlines = 0; pos < pbm.size() && newlines < 2; pos++) {
		newlines += pbm[pos] == '\n' ? 1 : 0;
	}
	std::string out = pbm.substr(0, pos);
	for (int i = 0; i < 5 && pos < pbm.size(); i++) {
		const size_t next = pbm.find('\n', pos);
		pos = next == std::string::npos ? pbm.size() : next + 1;
	}
	return out + pbm.substr(pos);
}

/// Matrix coordinates and encoder indices (`kMatrix`, `kDelugeEncoders`, `shell/profiles/deluge.cpp`).
constexpr uint8_t kSelectEncoder = 3;
constexpr uint8_t kHorizontalEncoder = 1;
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;
constexpr uint8_t kBackX = 7, kBackY = 1;
constexpr uint8_t kShiftX = 8, kShiftY = 0;

constexpr const Node* kRoots[] = {&nodes::sound_root, &nodes::source0, &nodes::osc0_type, &nodes::osc1_type,
                                  &nodes::dx_menu};

/// @brief The DX7 synth with the ported menu over it, on a `Shell` over the fake surfaces.
struct MenuRig : DxRig {
	const BoardProfile& profile = deluge_profile();
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake{deluge::ui::shell::kDelugeBoard, deluge_profile()};
	Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, ShellConfig{}};
	uint32_t now = 0;

	MenuRig() = default;
	~MenuRig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
		menu_scene().reset_process_state();
	}
	MenuRig(const MenuRig&) = delete;
	MenuRig& operator=(const MenuRig&) = delete;

	MenuContext menu_context() {
		return menu::context_for(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false);
	}
	/// @brief Open the menu on @p root, writing to @p sink (the app's when null), and draw it once.
	void open(const Node& root, bind::WriteSink* sink = nullptr) {
		MenuContext ctx = menu_context();
		if (sink != nullptr) {
			ctx.sink = sink;
		}
		(void)menu::assign_node_ids(kRoots);
		while (shell.stack().depth() > 0) { // a fresh push, so the first frame is drawn whole
			shell.stack().pop(SceneKind::Null);
		}
		menu_scene().open(root, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(root)});
		tick();
	}
	void tick() { shell.tick(now += 10); }
	void button(uint8_t x, uint8_t y, bool on) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)},
		                     now += 10);
	}
	void select() {
		button(kSelectEncButtonX, kSelectEncButtonY, true);
		button(kSelectEncButtonX, kSelectEncButtonY, false);
		tick();
	}
	void back() {
		button(kBackX, kBackY, true);
		button(kBackX, kBackY, false);
		tick();
	}
	/// @brief @p detents clicks of a detented encoder: two A-pin edges each.
	void turn(int32_t detents, uint8_t encoder = kSelectEncoder) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, encoder, 0, static_cast<int16_t>(detents * 2)},
		                     now += 10);
		tick();
	}
	const Node* focused() const { return menu_scene().focused(); }
	const menu::MenuState& state() const { return menu_scene().state(); }
	int16_t cursor() const { return state().list[state().depth].cursor; }
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

	it("goes up from DX7 source's browse row and files the browser's action, opening nothing", _ {
		MenuRig r;
		r.open(nodes::dx_menu);
		expect(static_cast<int32_t>(r.cursor())).to_equal(0);
		r.select();
		expect(r.focused() == &nodes::dx_menu).to_be_true();
		expect(menu_scene().take_action().action == MenuAction::OpenDxBrowser).to_be_true();
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
});
// clang-format on

CPPSPEC_SPEC(dx_pages)
