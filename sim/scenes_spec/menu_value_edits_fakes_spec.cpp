/// @file
/// @brief The menu's value edits over the ports' fakes, with a model song of the spec's own and no legacy song behind
///        the menu: an int, a choice, a range, a toggle and a cable leaf each reach the recorder their port names, with
///        the target and value the menu meant, and a refused write leaves the shown value as it was.

#include "ui/scenes/bind/target.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/menu/nodes/settings.h"

#include "base/notify/dirty_set.h"
#include "project/device_settings.h"

#include "support/fake_menu_shell.h"

#include "cppspec.hpp"

#include <cstdint>
#include <optional>
#include <string>

#include "support/spec_main.h"

using namespace deluge::ui::scenes;
using deluge::ui::scenes::bind::Target;
using deluge::ui::scenes::bind::test::RecordingControlsSink;
using deluge::ui::scenes::bind::test::RecordingSoundWrites;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::spec::FakeHostRig;
using deluge::ui::scenes::spec::FakeMenuShell;
using device_path = deluge::project::device_path;

namespace {

namespace nodes = deluge::ui::scenes::menu::nodes;

/// MASTER's columns are SYNTH MODE, VOLUME, PAN, TRANSPOSE and VIBRATO.
constexpr int8_t kVibratoColumn = 4;

/// @brief Did the last write ask for the OLED to be redrawn?
bool repainted() {
	return deluge::base::notify::dirty().drain().has(deluge::base::notify::Dirty::Oled);
}

} // namespace

// clang-format off
describe menu_value_edits_fakes("the menu's value edits over the ports' fakes and a model song", ${
	it("writes an int param as one request on the edit sink, at its menu value", _ {
		FakeMenuShell r;
		const MenuContext ctx = r.fakes.synth_context();
		(void)deluge::base::notify::dirty().drain();
		expect(menu::bindings::write(nodes::lpf_freq, ctx, 30)).to_be_true();
		const auto& requests = r.fakes.host.writes.requests;
		expect(requests.size()).to_equal(size_t{1});
		if (!requests.empty()) {
			expect(requests[0].target.kind).to_equal(Target::Kind::PARAM);
			expect(requests[0].target.param).to_equal(nodes::lpf_freq.bind.target.param);
			expect(requests[0].where.slot).to_equal(std::optional{FakeHostRig::kSynthSlot});
			expect(requests[0].menu_value).to_equal(30);
		}
		expect(repainted()).to_be_true();
		expect(r.fakes.host.sound_writes.calls.empty()).to_be_true();
	});

	it("writes a source's transpose, an int its own hook owns, as one request on the sound port", _ {
		FakeMenuShell r;
		const MenuContext ctx = r.fakes.synth_context();
		menu::bindings::write(nodes::source0_transpose, ctx, 250);
		const auto& calls = r.fakes.host.sound_writes.calls;
		expect(calls.size()).to_equal(size_t{1});
		if (!calls.empty()) {
			expect(calls[0].kind).to_equal(RecordingSoundWrites::Call::Kind::TRANSPOSE);
			expect(calls[0].source).to_equal(uint8_t{0});
			expect(calls[0].where.slot).to_equal(std::optional{FakeHostRig::kSynthSlot});
			expect(calls[0].value).to_equal(int64_t{250});
		}
		expect(r.fakes.host.writes.requests.empty()).to_be_true();
	});

	it("writes a choice as one request naming its effect row and the choice's index", _ {
		FakeMenuShell r;
		const MenuContext ctx = r.fakes.synth_context();
		menu::bindings::write(nodes::lpf_mode, ctx, 1);
		const auto& requests = r.fakes.host.writes.requests;
		expect(requests.size()).to_equal(size_t{1});
		if (!requests.empty()) {
			expect(requests[0].target.kind).to_equal(Target::Kind::EFFECT);
			expect(std::string{requests[0].target.path}).to_equal(std::string{"lpfMode"});
			expect(requests[0].menu_value).to_equal(1);
		}
	});

	it("writes a toggle as one request naming its sound row, on and then off", _ {
		FakeMenuShell r;
		const MenuContext ctx = r.fakes.synth_context();
		menu::bindings::write(nodes::osc_sync, ctx, 1);
		menu::bindings::write(nodes::osc_sync, ctx, 0);
		const auto& requests = r.fakes.host.writes.requests;
		expect(requests.size()).to_equal(size_t{2});
		if (requests.size() == 2) {
			expect(requests[0].target.kind).to_equal(Target::Kind::SOUND);
			expect(std::string{requests[0].target.path}).to_equal(std::string{"osc2/oscillatorSync"});
			expect(requests[0].menu_value).to_equal(1);
			expect(requests[1].menu_value).to_equal(0);
		}
	});

	it("stores each end of a range as one write on the device port, and the range reads them back", _ {
		FakeMenuShell r;
		const MenuContext ctx = r.fakes.settings_context(/*with_model=*/false);
		menu::bindings::write(nodes::default_tempo, ctx, 100);
		menu::bindings::write_upper(nodes::default_tempo, ctx, 150);
		const auto& stores = r.fakes.host.device.stores;
		expect(stores.size()).to_equal(size_t{2});
		if (stores.size() == 2) {
			expect(stores[0].first).to_equal(std::string{device_path::kTempoLower});
			expect(stores[0].second.str()).to_equal(std::string{"100"});
			expect(stores[1].first).to_equal(std::string{device_path::kTempoUpper});
			expect(stores[1].second.str()).to_equal(std::string{"150"});
		}
		expect(menu::bindings::read(nodes::default_tempo, ctx)).to_equal(100);
		expect(menu::bindings::read_upper(nodes::default_tempo, ctx)).to_equal(150);
		expect(r.fakes.host.writes.requests.empty()).to_be_true();
	});

	it("writes a select turn on the VIBRATO column as one cable request, and marks the sound edited", _ {
		FakeMenuShell r;
		const MenuContext ctx = r.open_synth(nodes::sound_master);
		menu_scene().state_for_spec().columns[0].focused = kVibratoColumn;
		r.shell.tick(100);
		const int32_t before = menu::bindings::read(nodes::vibrato, ctx);
		r.turn(1, 1000);
		const auto& requests = r.fakes.host.writes.requests;
		expect(requests.size()).to_equal(size_t{1});
		if (!requests.empty()) {
			expect(requests[0].target.kind).to_equal(Target::Kind::CABLE);
			expect(requests[0].where.slot).to_equal(std::optional{FakeHostRig::kSynthSlot});
			expect(requests[0].menu_value).to_equal(before + 100);
		}
		// The knobs' sink hears the turn edited the synth, as `markInstrumentAsEdited` does.
		const auto& gestures = r.fakes.host.controls_sink.gestures;
		expect(gestures.size()).to_equal(size_t{1});
		if (!gestures.empty()) {
			expect(gestures[0].kind).to_equal(RecordingControlsSink::Gesture::Kind::INSTRUMENT_EDITED);
		}
	});

	it("asks for no redraw when the edit sink refuses a write", _ {
		FakeMenuShell r;
		const MenuContext ctx = r.fakes.synth_context();
		r.fakes.host.writes.accept = false;
		(void)deluge::base::notify::dirty().drain();
		expect(menu::bindings::write(nodes::osc_sync, ctx, 1)).to_be_false();
		// The request reached the sink, which refused it: nothing the screen shows has changed.
		expect(r.fakes.host.writes.requests.size()).to_equal(size_t{1});
		expect(repainted()).to_be_false();
	});

	it("leaves a range's stored ends as they were when the device port refuses a store", _ {
		FakeMenuShell r;
		const MenuContext ctx = r.fakes.settings_context(/*with_model=*/false);
		const int32_t lower = menu::bindings::read(nodes::default_tempo, ctx);
		r.fakes.host.device.refuse.insert(std::string{device_path::kTempoLower});
		(void)deluge::base::notify::dirty().drain();
		expect(menu::bindings::write(nodes::default_tempo, ctx, lower + 10)).to_be_false();
		expect(r.fakes.host.device.stores.size()).to_equal(size_t{1});
		expect(menu::bindings::read(nodes::default_tempo, ctx)).to_equal(lower);
		expect(repainted()).to_be_false();
	});

	it("keeps the open toggle page's drawn value when the edit sink refuses the turn's write", _ {
		FakeMenuShell r;
		const MenuContext ctx = r.open_synth(nodes::osc_sync);
		expect(menu_scene().focused()).to_equal(&nodes::osc_sync);
		r.fakes.host.writes.accept = false;
		r.shell.tick(100);
		const std::string before = r.oled();
		r.turn(1, 1000);
		// The turn's write reached the sink, which refused it; the page still draws the stored value.
		expect(r.fakes.host.writes.requests.size()).to_equal(size_t{1});
		expect(r.oled()).to_equal(before);
		expect(static_cast<int32_t>(menu_scene().state().edit_bool)).to_equal(menu::bindings::read(nodes::osc_sync, ctx));
	});

	it("keeps the open range page's drawn ends when the device port refuses the turn's store", _ {
		FakeMenuShell r;
		const MenuContext ctx = r.open_settings(nodes::default_tempo);
		expect(menu_scene().focused()).to_equal(&nodes::default_tempo);
		r.fakes.host.device.refuse.insert(std::string{device_path::kTempoLower});
		r.fakes.host.device.refuse.insert(std::string{device_path::kTempoUpper});
		r.shell.tick(100);
		const std::string before = r.oled();
		const int32_t lower = menu::bindings::read(nodes::default_tempo, ctx);
		r.turn(1, 1000);
		// Both ends' stores reached the port, which kept neither; the page still draws the stored ends.
		expect(r.fakes.host.device.stores.size()).to_equal(size_t{2});
		expect(menu::bindings::read(nodes::default_tempo, ctx)).to_equal(lower);
		expect(r.oled()).to_equal(before);
		expect(menu_scene().state().edit_int).to_equal(lower);
		expect(menu_scene().state().edit_upper).to_equal(menu::bindings::read_upper(nodes::default_tempo, ctx));
	});
});
// clang-format on

DELUGE_SPEC(menu_value_edits_fakes_spec, menu_value_edits_fakes)
