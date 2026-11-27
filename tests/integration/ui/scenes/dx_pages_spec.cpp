// The DX7 menu pages against main's: the parameter tables and step rules main's `DxParam`, `DxGlobalParams` and
// `DxOperatorParams` answer with (held in `tests/golden/ui/menu_oracle/dx_pages.tsv` and its panels), and the DX patch
// write through the app's sound port.

#include "gui/ui/menu_view_of.h"
#include "gui/ui/shell_host/host_ports.h"
#include "support/sound_fixtures.h"
#include "ui/scenes/l10n_bridge.h"
#include "ui/scenes/menu/action.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/dx.h"
#include "ui/scenes/menu/nodes/dx_tables.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/menu/shortcut_blink.h"
#include "ui/scenes/menu/shortcut_overlay.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/registry.h"

#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"

#include "base/notify/notice_queue.h"
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "hid/buttons.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/settings/runtime_feature_settings.h"
#include "processing/dx7/dx_engine.h"
#include "processing/dx7/dx_patch.h"
#include "processing/sound/sound.h"
#include "processing/sound/source_resources.h"

#include "fakes.h"
#include "support/dx_pages_fixtures.h"
#include "support/dx_rig.h"
#include "support/mocks/fake_surfaces.h"
#include "support/oracle_table.h"
#include "support/printers/dx_pad_printer.h"
#include "support/printers/rgb_printer.h"
#include "support/printers/shortcut_blink_printer.h"
#include "support/printers/sound_record_printer.h"
#include "support/song_fixture.h"
#include "unit/ui/gfx/support/pbm.h"
#include "unit/ui/widgets/support/frames.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace deluge::ui::scenes;
using deluge::ui::scenes::menu::MenuAction;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::menu::Node;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
namespace dx = deluge::ui::scenes::menu::dx;
namespace nodes = deluge::ui::scenes::menu::nodes;

namespace {

using namespace deluge::ui::scenes::spec::dx_rig;
using namespace deluge::ui::scenes::spec::dxpages;

spec::OracleTable& table() {
	return spec::OracleTable::named("dx_pages");
}

/// @brief The reference value of (@p path, @p column) as a number, or -9999 where there is no such row.
int32_t frozen_number(std::string_view path, std::string_view column) {
	const std::vector<std::string>* v = table().find(path, column);
	return v == nullptr || v->empty() ? -9999 : std::stoi(v->front());
}

/// @brief A "x,y" pad as the rows hold it.
std::string pad_text(const std::optional<dx::Pad>& pad) {
	return pad ? std::to_string(pad->x) + "," + std::to_string(pad->y) : std::string{"-"};
}

} // namespace

// clang-format off
describe dx_pages("the DX7 menu pages", ${
	it("give every param main's upper limit and title", _ {
		select_language();
		for (int16_t p = dx::kRandomDetune; p <= dx::kLastParam; p++) {
			const std::string path = "dx/" + std::to_string(p);
			expect(path + " " + std::to_string(dx::upper_limit(p)))
			    .to_equal(path + " " + table().value(path, "max"));
			std::array<char, 32> buf{};
			const size_t n = dx::title(p, buf);
			expect(path + " " + std::string{buf.data(), n}).to_equal(path + " " + table().value(path, "title"));
		}
	});

	it("send each row of DX7 parameters where main's select does", _ {
		for (int32_t row = 0; row < dx::kGlobalRows; row++) {
			const int16_t mine = dx::global_row_param(row);
			const std::string path = "dx/row/" + std::to_string(row);
			expect(path + " " + (mine >= 0 ? "param " + std::to_string(mine) : "op " + std::to_string(6 + mine)))
			    .to_equal(path + " " + table().value(path, "to"));
		}
	});

	it("send each row of every DX7 operator where main's select does", _ {
		for (int32_t op = 0; op < 6; op++) {
			for (int32_t row = 0; row < dx::kOperatorRows; row++) {
				const std::string path = "dx/op/" + std::to_string(op) + "/row/" + std::to_string(row);
				expect(path + " param " + std::to_string(dx::operator_row_param(op, row)))
				    .to_equal(path + " " + table().value(path, "to"));
			}
		}
	});

	it("step a horizontal turn as main does, wrapping between the random detune and the last param", _ {
		for (int16_t p = dx::kRandomDetune; p <= dx::kLastParam; p++) {
			for (const int32_t offset : {-1, 1}) {
				const std::string path = "dx/h/" + std::to_string(p) + "/" + std::to_string(offset);
				expect(path + " " + std::to_string(dx::step_param(p, offset)))
				    .to_equal(path + " " + table().value(path, "to"));
			}
		}
	});

	it("step Shift + a horizontal turn to the next switched-on operator as main does", _ {
		DxRig r;
		for (int32_t mask = 0; mask < 64; mask++) {
			r.patch().params[dx::kOperatorSwitch] = static_cast<uint8_t>(mask);
			for (int16_t p = dx::kRandomDetune; p <= dx::kLastParam; p++) {
				if (mask == 0 && p == dx::kFirstGlobal) {
					continue; // main's search never ends here; the next example covers it
				}
				const std::string path = "dx/shift_h/" + std::to_string(p) + "/" + std::to_string(mask);
				const std::vector<std::string>* frozen = table().find(path, "to");
				expect(frozen).not_().to_be_null();
				if (frozen != nullptr) {
					expect(frozen->size()).to_equal(2);
				}
				for (size_t k = 0; frozen != nullptr && frozen->size() == 2 && k < 2; k++) {
					const int32_t offset = k == 0 ? -1 : 1;
					const std::optional<int16_t> mine = dx::step_operator(p, offset, r.patch().params);
					expect(path + " " + std::to_string(offset) + " " + std::to_string(mine.value_or(p)))
					    .to_equal(path + " " + std::to_string(offset) + " " + (*frozen)[k]);
				}
			}
		}
	});

	it("leave the pitch envelope's first rate alone under Shift with every operator off, where main hangs", _ {
		DxRig r;
		r.patch().params[dx::kOperatorSwitch] = 0;
		expect(dx::step_operator(dx::kFirstGlobal, 1, r.patch().params).has_value()).to_be_false();
		expect(dx::step_operator(dx::kFirstGlobal, -1, r.patch().params).has_value()).to_be_false();
		r.patch().params[dx::kOperatorSwitch] = 0b000100;
		expect(static_cast<int32_t>(dx::step_operator(dx::kFirstGlobal, 1, r.patch().params).value_or(-2))).to_equal(2 * 21);
	});

	it("name the scaling curves and LFO waveforms as main's page does, past the end as its last", _ {
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
		deluge::instruments::classic::DxPatchState want = r.sound->dx7Patch.state().value();
		want.params[134] = 17;
		want.params[0] = 3;
		want.engine_mode = 2;
		want.random_detune = 42;
		expect(deluge::gui::shell_host::host_ports().sound_writes->write_dx_patch(r.slot(), want)).to_be_true();
		expect(r.sound->dx7Patch.state()).to_equal(std::optional{want});
		expect(static_cast<int32_t>(r.patch().engineMode)).to_equal(2);
		expect(r.patch().random_detune).to_equal(42);
		expect(actionLogger.firstAction[BEFORE]).to_be_null();
	});

	// main's page leaves the engine an auto patch had when its algorithm changes, so an auto patch on algorithm 4 or 6
	// with feedback renders without the feedback loop only MkI implements; here the engine follows the bytes.
	it("chooses the engine again for an auto patch whose new algorithm needs MkI", _ {
		DxRig r;
		deluge::instruments::classic::DxPatchState want = r.sound->dx7Patch.state().value();
		expect(static_cast<int32_t>(want.engine_mode)).to_equal(0);
		expect(r.patch().engine).to_equal(deluge::dsp::fixed::dx7::Engine::MODERN);
		want.params[134] = 3; // algorithm 4
		want.params[135] = 5; // with feedback
		expect(deluge::gui::shell_host::host_ports().sound_writes->write_dx_patch(r.slot(), want)).to_be_true();
		expect(r.patch().engine).to_equal(deluge::dsp::fixed::dx7::Engine::MKI);
	});

	it("refuses a sound in another mode, and a slot that does not resolve", _ {
		DxRig r;
		const deluge::instruments::classic::DxPatchState want = r.sound->dx7Patch.state().value();
		deluge::project::SlotAddress nowhere = r.slot();
		nowhere.id += 1000;
		expect(deluge::gui::shell_host::host_ports().sound_writes->write_dx_patch(nowhere, want)).to_be_false();
		deluge::test::setSynthMode(*r.sound, deluge::instruments::classic::SynthMode::SUBTRACTIVE);
		expect(deluge::gui::shell_host::host_ports().sound_writes->write_dx_patch(r.slot(), want)).to_be_false();
	});

	it("enters DX7 source on SELECT from the synth mode set to DX7, on its row and on the master page", _ {
		{
			MenuRig r;
			r.open(nodes::synth_mode);
			r.select();
			expect(r.focused()).to_equal(&nodes::dx_menu);
		}
		{
			MenuRig r;
			r.open(nodes::sound_master); // the page opens on its first column, the mode
			r.select();
			expect(r.focused()).to_equal(&nodes::dx_menu);
		}
	});

	it("goes up on SELECT from any other synth mode, as main's mode answers nothing", _ {
		MenuRig r;
		deluge::test::setSynthMode(*r.sound, deluge::instruments::classic::SynthMode::SUBTRACTIVE);
		r.open(nodes::sound_master);
		r.select();
		expect(r.focused()).not_().to_equal(&nodes::dx_menu);
		expect(nodes::mode_chain(r.menu_context(), r.state())).to_be_null();
	});

	it("puts the DX7's page in OSC1's place in DX7 mode, its columns alone, and hides OSC2's page and the mixer", _ {
		MenuRig r;
		const MenuContext ctx = r.menu_context();
		expect(menu::bindings::relevant(nodes::source0, ctx)).to_be_true();
		expect(menu::bindings::relevant(nodes::source1, ctx)).to_be_false();
		expect(menu::bindings::relevant(nodes::osc_mixer, ctx)).to_be_false();
		std::array<const Node*, 32> listed{};
		const size_t n = menu::bindings::listed_children(nodes::source0, ctx, listed, menu::bindings::ListFor::Columns);
		const std::vector<const Node*> columns(listed.begin(), listed.begin() + static_cast<std::ptrdiff_t>(n));
		expect(columns).to_equal(std::vector<const Node*>{&nodes::dx7_engine, &nodes::dx7_transpose, &nodes::dx7_amp_mod,
		                                                  &nodes::dx7_phase, &nodes::dx7_level});
		deluge::test::setSynthMode(*r.sound, deluge::instruments::classic::SynthMode::SUBTRACTIVE);
		const MenuContext subtractive = r.menu_context();
		expect(menu::bindings::relevant(nodes::source1, subtractive)).to_be_true();
		const size_t m =
		    menu::bindings::listed_children(nodes::source0, subtractive, listed, menu::bindings::ListFor::Columns);
		expect(std::count(listed.begin(), listed.begin() + static_cast<std::ptrdiff_t>(m), &nodes::dx7_engine))
		    .to_equal(std::ptrdiff_t{0});
	});

	it("enters DX7 source on SELECT from the DX7 page's first column, as from main's DX7 oscillator type", _ {
		MenuRig r;
		r.open(nodes::source0); // the page opens on its first listed column, the DX7's
		r.select();
		expect(r.focused()).to_equal(&nodes::dx_menu);
	});

	it("a re-read of the engine version takes the patch's mode again", _ {
		// `DxEngineSelect::readValueAgain`: `currentValue` is the patch's `engineMode`, read afresh.
		MenuRig r;
		r.open(nodes::dx_engine_select);
		expect(static_cast<int32_t>(r.cursor())).to_equal(0);
		deluge::instruments::classic::DxPatchState vintage = r.sound->dx7Patch.state().value();
		vintage.engine_mode = 2;
		expect(deluge::gui::shell_host::host_ports().sound_writes->write_dx_patch(r.slot(), vintage)).to_be_true();
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

	it("lists DX7 source's three rows as main's dxMenu does", _ {
		expect(static_cast<int32_t>(std::size(nodes::kDxMenuChildren))).to_equal(frozen_number("dx/source", "kids"));
		for (size_t i = 0; i < std::size(nodes::kDxMenuChildren); i++) {
			const std::string mine{text_lookup(nodes::kDxMenuChildren[i]->name)};
			expect(mine).to_equal(table().value("dx/source/" + std::to_string(i), "name"));
		}
		expect(std::string{text_lookup(nodes::dx_menu.title)}).to_equal(table().value("dx/source", "title"));
	});

	it("puts the engine version's cursor on the patch's mode and writes a turn's mode, stopping at either end", _ {
		MenuRig r;
		deluge::instruments::classic::DxPatchState modern = r.sound->dx7Patch.state().value();
		modern.engine_mode = 1;
		expect(deluge::gui::shell_host::host_ports().sound_writes->write_dx_patch(r.slot(), modern)).to_be_true();
		bind::test::RecordingSoundWrites writes;
		r.open(nodes::dx_engine_select, &writes);
		expect(static_cast<int32_t>(r.cursor())).to_equal(1);
		r.turn(1);
		expect(static_cast<int32_t>(r.cursor())).to_equal(2);
		expect(writes.calls.size()).to_equal(size_t{1});
		expect(writes.calls.back().kind).to_equal(bind::test::RecordingSoundWrites::Call::Kind::DX_PATCH);
		expect(static_cast<int32_t>(writes.calls.back().dx_patch->engine_mode)).to_equal(2);
		expect(writes.calls.back().dx_patch->params).to_equal(r.patch().params);
		r.turn(1);
		expect(static_cast<int32_t>(r.cursor())).to_equal(2);
		expect(writes.calls.size()).to_equal(size_t{1});
		r.turn(-3);
		expect(static_cast<int32_t>(r.cursor())).to_equal(2); // main drops a turn that would leave the modes
		expect(writes.calls.size()).to_equal(size_t{1});
	});

	it("turns the engine version's list five steps under Shift, and drops a turn that would leave the modes", _ {
		// `SoundEditor::selectEncoderAction` hands `DxEngineSelect` the offset times 5; it does nothing out of range.
		MenuRig r;
		bind::test::RecordingSoundWrites writes;
		r.open(nodes::dx_engine_select, &writes);
		r.button(kShiftX, kShiftY, true);
		r.turn(1);
		expect(static_cast<int32_t>(r.cursor())).to_equal(0);
		expect(writes.calls.empty()).to_be_true();
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
		deluge::instruments::classic::DxPatchState patch = r.sound->dx7Patch.state().value();
		patch.params[134] = 3;
		expect(deluge::gui::shell_host::host_ports().sound_writes->write_dx_patch(r.slot(), patch)).to_be_true();
		bind::test::RecordingSoundWrites writes;
		menu_scene().land_dx(menu::DxState{.param = 134});
		r.open(nodes::dx_param, &writes);
		r.button(kShiftX, kShiftY, true);
		r.turn(1);
		expect(static_cast<int32_t>(writes.calls.back().dx_patch->params[134])).to_equal(8);
		r.turn(10);
		expect(static_cast<int32_t>(writes.calls.back().dx_patch->params[134])).to_equal(31);
		r.button(kShiftX, kShiftY, false);
	});

	it("opens the parameter page or an operator's list from DX7 parameters, as main's rows do", _ {
		MenuRig r;
		r.open(nodes::dx_global_params);
		r.select();
		expect(r.focused()).to_equal(&nodes::dx_param);
		expect(static_cast<int32_t>(r.state().dx.param)).to_equal(134);
		r.back();
		expect(r.focused()).to_equal(&nodes::dx_global_params);
		r.turn(4); // "operator 2", which is operator index 4: the DX7 counts its operators from the other end
		r.select();
		expect(r.focused()).to_equal(&nodes::dx_operator_params);
		expect(static_cast<int32_t>(r.state().dx.op)).to_equal(4);
		r.turn(1); // "level"
		r.select();
		expect(r.focused()).to_equal(&nodes::dx_param);
		expect(static_cast<int32_t>(r.state().dx.param)).to_equal(4 * 21 + 16);
	});

	it("draws every parameter layout as main's DxParam::renderOLED does", _ {
		select_language();
		MenuRig r;
		const auto shot = [&](const std::string& name, int16_t param) {
			const std::string frozen = frozen_panel("dx_" + name);
			expect(frozen.empty()).to_be_false();
			menu_scene().land_dx(menu::DxState{.param = param});
			r.open(nodes::dx_param);
			r.tick();
			expect(static_cast<int32_t>(r.state().dx.param)).to_equal(static_cast<int32_t>(param));
			const std::string tag = "param " + std::to_string(param) + " " + name + "\n";
			expect(tag + below_bezel(r.fake.pbm(0))).to_equal(tag + below_bezel(frozen));
		};
		deluge::instruments::classic::DxPatchState patch = r.sound->dx7Patch.state().value();
		for_each_layout(
		    patch,
		    [&] { expect(deluge::gui::shell_host::host_ports().sound_writes->write_dx_patch(r.slot(), patch)).to_be_true(); },
		    shot);
	});

	it("draws DX7 source, both lists and the engine version as main's items do", _ {
		select_language();
		MenuRig r;
		const auto port_panel = [&](const Node& node) {
			r.open(node);
			r.tick();
			return below_bezel(r.fake.pbm(0));
		};
		const auto frozen_page = [&](const std::string& name) {
			const std::string frozen = frozen_panel("dx_source_" + name);
			expect(frozen.empty()).to_be_false();
			return below_bezel(frozen);
		};
		expect(port_panel(nodes::dx_menu)).to_equal(frozen_page("menu"));
		for (int32_t row = 0; row < dx::kGlobalRows; row++) {
			menu_scene().land_dx(menu::DxState{.global_cursor = static_cast<int16_t>(row)});
			expect(port_panel(nodes::dx_global_params)).to_equal(frozen_page("global_" + std::to_string(row)));
		}
		for (int32_t op = 0; op < 6; op++) {
			for (int32_t row = 0; row < dx::kOperatorRows; row++) {
				menu_scene().land_dx(menu::DxState{.op = static_cast<int8_t>(op), .operator_cursor = static_cast<int16_t>(row)});
				expect(port_panel(nodes::dx_operator_params))
				    .to_equal(frozen_page("operator_" + std::to_string(op) + "_" + std::to_string(row)));
			}
		}
		for (uint8_t mode = 0; mode < 3; mode++) {
			deluge::instruments::classic::DxPatchState patch = r.sound->dx7Patch.state().value();
			patch.engine_mode = mode;
			expect(deluge::gui::shell_host::host_ports().sound_writes->write_dx_patch(r.slot(), patch)).to_be_true();
			expect(port_panel(nodes::dx_engine_select)).to_equal(frozen_page("engine_" + std::to_string(mode)));
		}
	});

	it("moves the value within its limits on a select turn and writes oscillator 1's patch", _ {
		MenuRig r;
		deluge::instruments::classic::DxPatchState patch = r.sound->dx7Patch.state().value();
		patch.params[134] = 29;
		patch.random_detune = 3;
		expect(deluge::gui::shell_host::host_ports().sound_writes->write_dx_patch(r.slot(), patch)).to_be_true();
		bind::test::RecordingSoundWrites writes;
		menu_scene().land_dx(menu::DxState{.param = 134});
		r.open(nodes::dx_param, &writes);
		r.turn(5);
		expect(writes.calls.size()).to_equal(size_t{1});
		expect(writes.calls.back().source).to_equal(0);
		expect(static_cast<int32_t>(writes.calls.back().dx_patch->params[134])).to_equal(31); // the algorithm's limit
		menu_scene().land_dx(menu::DxState{.param = dx::kRandomDetune});
		r.open(nodes::dx_param, &writes);
		r.turn(-5);
		expect(writes.calls.back().dx_patch->random_detune).to_equal(0);
		r.turn(2);
		expect(writes.calls.back().dx_patch->random_detune).to_equal(5);
	});

	it("steps the param on a horizontal turn, wrapping, and the operator under Shift, staying put where main hangs", _ {
		MenuRig r;
		menu_scene().land_dx(menu::DxState{.param = dx::kLastParam});
		r.open(nodes::dx_param);
		r.turn(1, kHorizontalEncoder);
		expect(static_cast<int32_t>(r.state().dx.param)).to_equal(-1);
		r.turn(-1, kHorizontalEncoder);
		expect(static_cast<int32_t>(r.state().dx.param)).to_equal(143);

		deluge::instruments::classic::DxPatchState patch = r.sound->dx7Patch.state().value();
		patch.params[dx::kOperatorSwitch] = 0b100101; // operators 0, 2 and 5 on
		expect(deluge::gui::shell_host::host_ports().sound_writes->write_dx_patch(r.slot(), patch)).to_be_true();
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
		expect(deluge::gui::shell_host::host_ports().sound_writes->write_dx_patch(r.slot(), patch)).to_be_true();
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
			expect(menu::plan_blinks(r.menu_context(), r.state(), nodes::dx_param)).to_equal(menu::BlinkPlan{});
		}
	});

	it("opens the parameter page from the modulator's shortcut inside the menu, on a DX7 sound only", _ {
		MenuRig r;
		menu_scene().land_dx(menu::DxState{.param = 40});
		r.open(nodes::dx_global_params);
		expect(nodes::dx_shortcut_fallback(nodes::modulator0_volume, r.menu_context(), 4)).to_equal(&nodes::dx_param);
		expect(nodes::dx_shortcut_fallback(nodes::modulator0_volume, r.menu_context(), 5)).to_be_null();
		expect(nodes::dx_shortcut_fallback(nodes::modulator1_volume, r.menu_context(), 4)).to_be_null();
		r.button(kShiftX, kShiftY, true);
		r.shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, 4, 0, 64}, r.now += 10);
		r.tick();
		r.shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, 4, 0, 0}, r.now += 10);
		r.button(kShiftX, kShiftY, false);
		r.tick();
		expect(r.focused()).to_equal(&nodes::dx_param);
		expect(static_cast<int32_t>(r.state().dx.param)).to_equal(40);
		deluge::test::setSynthMode(*r.sound, deluge::instruments::classic::SynthMode::SUBTRACTIVE);
		expect(nodes::dx_shortcut_fallback(nodes::modulator0_volume, r.menu_context(), 4)).to_be_null();
	});

	it("lands oscillator 1's type shortcut on the DX7's column on a DX7 sound, as main's type showed its DX7", _ {
		MenuRig r;
		expect(nodes::dx_shortcut_fallback(nodes::osc0_type, r.menu_context(), 2)).to_equal(&nodes::dx7_engine);
		expect(nodes::dx_shortcut_fallback(nodes::osc1_type, r.menu_context(), 3)).to_be_null();
		deluge::test::setSynthMode(*r.sound, deluge::instruments::classic::SynthMode::SUBTRACTIVE);
		expect(nodes::dx_shortcut_fallback(nodes::osc0_type, r.menu_context(), 2)).to_be_null();
	});

	it("goes up from the parameter page on SELECT and on BACK", _ {
		MenuRig r;
		r.open(nodes::dx_global_params);
		r.select();
		expect(r.focused()).to_equal(&nodes::dx_param);
		r.select();
		expect(r.focused()).to_equal(&nodes::dx_global_params);
		r.select();
		r.back();
		expect(r.focused()).to_equal(&nodes::dx_global_params);
	});

	it("blinks the pad main's page blinks for every param, every other cycle, and none for the scaling params", _ {
		MenuRig r;
		const MenuContext ctx = r.menu_context();
		for (int16_t param = dx::kRandomDetune; param <= dx::kLastParam; param++) {
			menu::MenuState state{};
			state.dx.param = param;
			state.dx.pad_blink = true;
			const menu::BlinkPlan plan = menu::plan_blinks(ctx, state, nodes::dx_param);
			const std::optional<dx::Pad> pad = dx::param_pad(param);
			const std::string path = "dx/" + std::to_string(param);
			const std::vector<std::string>* frozen = table().find(path, "blink");
			expect(frozen).not_().to_be_null();
			if (frozen != nullptr) {
				expect(frozen->empty()).to_be_false();
			}
			if (frozen == nullptr || frozen->empty()) {
				continue;
			}
			expect(path + " " + pad_text(pad)).to_equal(path + " " + frozen->front());
			if (!pad) {
				expect(plan).to_equal(menu::BlinkPlan{});
			}
			else {
				expect(frozen->size()).to_equal(size_t{2});
				expect(frozen->size() == 2 ? (*frozen)[1] : std::string{}).to_equal(std::string{"1"});
				expect(plan).to_equal(menu::exclusive_blink(pad->x, pad->y));
			}
		}
	});

	it("opens with no pad blinking, as main's screen entry unsets the page's blink, and blinks after a step", _ {
		MenuRig r;
		menu_scene().land_dx(menu::DxState{.param = 2 * 21 + 3, .pad_blink = true});
		r.open(nodes::dx_global_params);
		r.select(); // "algorithm"
		expect(r.focused()).to_equal(&nodes::dx_param);
		expect(menu::plan_blinks(r.menu_context(), r.state(), nodes::dx_param)).to_equal(menu::BlinkPlan{});
		r.turn(1, kHorizontalEncoder);
		expect(menu::plan_blinks(r.menu_context(), r.state(), nodes::dx_param))
		    .to_equal(menu::exclusive_blink(9, 1)); // feedback's pad
		r.turn(1); // an edit leaves the blink as it is
		expect(menu::plan_blinks(r.menu_context(), r.state(), nodes::dx_param)).to_equal(menu::exclusive_blink(9, 1));
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
		r.open(nodes::dx_global_params); // a new menu, as main's singletons outlive the editor
		expect(static_cast<int32_t>(r.cursor())).to_equal(5);
		r.open(nodes::dx_operator_params);
		expect(static_cast<int32_t>(r.cursor())).to_equal(3);
	});
	it("picks the param main's DX7 pad layout picks for every main pad, the inverse of the blink's pad", _ {
		for (uint8_t x = 0; x < 16; x++) {
			for (uint8_t y = 0; y < 8; y++) {
				const std::optional<int16_t> mine = dx::pad_param(x, y);
				const std::string path = "dx/pad/" + std::to_string(x) + "," + std::to_string(y);
				expect(path + " " + std::to_string(mine.value_or(kUntouched)))
				    .to_equal(path + " " + table().value(path, "param"));
				if (mine) {
					if (const std::optional<dx::Pad> back = dx::param_pad(*mine)) {
						expect(*back).to_equal(dx::Pad{x, y});
					}
				}
			}
		}
		expect(dx::pad_param(16, 4).has_value()).to_be_false(); // the sidebar
	});

	it("takes every Shift + pad on the parameter page: a pad picks its param and blinks", _ {
		select_language();
		MenuRig r;
		menu_scene().land_dx(menu::DxState{.param = 16});
		r.open(nodes::dx_param);
		r.shift_pad(3, 4); // operator index 3's rate 4
		expect(r.focused()).to_equal(&nodes::dx_param);
		expect(static_cast<int32_t>(r.state().dx.param)).to_equal(3 * 21 + 3);
		expect(r.state().dx.pad_blink).to_be_true();
		r.shift_pad(5, 1); // the pitch envelope's level 2
		expect(static_cast<int32_t>(r.state().dx.param)).to_equal(131);
		r.shift_pad(7, 0); // random detune
		expect(static_cast<int32_t>(r.state().dx.param)).to_equal(-1);
		r.shift_pad(12, 0); // no param: swallowed, the page stays on the param
		expect(r.focused()).to_equal(&nodes::dx_param);
		expect(static_cast<int32_t>(r.state().dx.param)).to_equal(-1);
	});

	it("paints main's DX7 colour map as the overlay on the parameter page, and the ordinary one elsewhere", _ {
		select_language();
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
		std::array<std::array<RGB, 18>, 8> want{}; // the overlay paints the main pads only
		for (uint8_t y = 0; y < 8; y++) {
			for (uint8_t x = 0; x < 16; x++) {
				const std::vector<std::string>* rgb = table().find(
				    "dx/overlay/" + std::to_string(x) + "," + std::to_string(y), "rgb");
				expect(rgb).not_().to_be_null();
				if (rgb != nullptr) {
					expect(rgb->size()).to_equal(3);
				}
				if (rgb != nullptr && rgb->size() == 3) {
					want[y][x] = RGB{static_cast<uint8_t>(std::stoi((*rgb)[0])), static_cast<uint8_t>(std::stoi((*rgb)[1])),
					                 static_cast<uint8_t>(std::stoi((*rgb)[2]))};
				}
			}
		}
		const auto dx_map = painted(nodes::dx_param);
		expect(dx_map).to_equal(want);
		expect(painted(nodes::dx_global_params)).not_().to_equal(dx_map);
	});

	it("shows the DX7 colour map under Shift on the parameter page", _ {
		select_language();
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
		expect(on_page).not_().to_equal(elsewhere);
		// The LFO row's first pad: pastel blue in the DX7 map, dimmed as every overlay pad is.
		expect(on_page[0][0]).not_().to_equal(elsewhere[0][0]);
	});
	it("moves the param as main's openForOpOrGlobal does for every DX column pad, on the page and off it", _ {
		for (int32_t op = 0; op < 8; op++) {
			// Off the page, main's `setup()` opens it and the param is the operator's level, or the algorithm.
			const std::string off = "dx/column/" + std::to_string(op);
			expect(off + " " + std::to_string(dx::column_param(5, op, false)))
			    .to_equal(off + " " + table().value(off, "off"));
			for (int16_t p = dx::kRandomDetune; p <= dx::kLastParam; p++) {
				const std::string path = "dx/column/" + std::to_string(op) + "," + std::to_string(p) + "/on";
				expect(path + " " + std::to_string(dx::column_param(p, op, true)))
				    .to_equal(path + " " + table().value(path, "param"));
			}
		}
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
		expect(r.focused()).to_equal(&nodes::dx_param);
		expect(static_cast<int32_t>(r.state().dx.param)).to_equal(4 * 21 + 16);
		expect(r.state().dx.pad_blink).to_be_true();
	});
	it("flashes the row main's readValueAgain flashes for every param", _ {
		for (int16_t p = dx::kRandomDetune; p <= dx::kLastParam; p++) {
			const std::string path = "dx/" + std::to_string(p);
			expect(path + " " + std::to_string(dx::flash_row(p)))
			    .to_equal(path + " " + table().value(path, "flash_row"));
		}
	});

	it("blinks the breakpoint note's pad where main's renderScaling does, over every layout of the keyboard", _ {
		for (const int32_t scroll : kScrolls) {
			for (int32_t interval = kIntervalFirst; interval <= kIntervalLast; interval++) {
				for (int32_t breakpoint = 0; breakpoint < kBreakpointEnd; breakpoint += kBreakpointStep) {
					const std::optional<dx::Pad> mine = dx::breakpoint_pad(static_cast<uint8_t>(breakpoint), scroll,
					                                                       interval);
					const std::string path = "dx/breakpoint/" + std::to_string(scroll) + "/" + std::to_string(interval)
					                         + "/" + std::to_string(breakpoint);
					const std::vector<std::string>* frozen = table().find(path, "pad");
					expect(frozen).not_().to_be_null();
					if (frozen != nullptr) {
						expect(frozen->empty()).to_be_false();
					}
					if (frozen == nullptr || frozen->empty()) {
						continue;
					}
					expect(path + " " + pad_text(mine)).to_equal(path + " " + frozen->front());
					if (mine) {
						expect(frozen->size() == 2 ? (*frozen)[1] : std::string{}).to_equal(std::string{"2"});
					}
				}
			}
		}
	});

	it("blinks the breakpoint's pad on a keyboard scaling param over the isomorphic keyboard, at main's frequency", _ {
		select_language();
		MenuRig r;
		IsomorphicRoot root{r.song.clip};
		r.song.clip->keyboardState.isomorphic.scrollOffset = 40;
		r.song.clip->keyboardState.isomorphic.rowInterval = 5;
		r.patch().params[1 * 21 + 8] = 45;
		deluge::instruments::classic::DxPatchState patch = r.sound->dx7Patch.state().value();
		expect(deluge::gui::shell_host::host_ports().sound_writes->write_dx_patch(r.slot(), patch)).to_be_true();
		menu_scene().land_dx(menu::DxState{.param = 1 * 21 + 10});
		r.open(nodes::dx_param);
		const std::optional<menu::ItemBlink> blink = nodes::kDxParam.own_blink(r.menu_context(), r.state());
		const std::optional<dx::Pad> want = dx::breakpoint_pad(45, 40, 5);
		expect(want.has_value()).to_be_true();
		expect(blink.has_value()).to_be_true();
		if (blink.has_value()) {
			expect(blink->valid).to_be_true();
		}
		expect(blink->x).to_equal(want->x);
		expect(blink->y).to_equal(want->y);
		expect(static_cast<int32_t>(blink->freq_mask)).to_equal(2);
		// Off the scaling params the page's own rule holds.
		menu_scene().land_dx(menu::DxState{.param = 1 * 21 + 3});
		r.open(nodes::dx_param);
		const std::optional<menu::ItemBlink> other = nodes::kDxParam.own_blink(r.menu_context(), r.state());
		expect(other.has_value()).to_be_true();
		if (other.has_value()) {
			expect(other->valid).to_be_false(); // the page opens with no blink
		}
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
		expect(*r.state().dx.flash_anchor_ms).to_be_greater_than(anchor);
		const uint32_t again = *r.state().dx.flash_anchor_ms;
		expect(menu_scene().dx_column_flash(again).value_or(-1)).to_equal(5);
		// Not over another root.
		r.open(nodes::dx_param);
		expect(menu_scene().dx_column_flash(*r.state().dx.flash_anchor_ms).has_value()).to_be_false();
	});
});

describe dx_pages_reference_rows("the DX7 menu pages: the reference rows", ${
	it("parses dx_pages.tsv clean and asks every row of it", _ {
		spec::OracleTable& t = table();
		expect(t.problems().size()).to_equal(size_t{0});
		expect(t.size()).to_be_greater_than(size_t{10000});
		expect(t.unasked()).to_equal(std::string{});
	});
});
// clang-format on

DELUGE_SPEC(dx_pages_spec, dx_pages, dx_pages_reference_rows)
