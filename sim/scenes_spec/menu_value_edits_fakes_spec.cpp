/// @file
/// @brief The menu's value edits over the ports' fakes, with a model song of the spec's own and no legacy song behind
///        the menu: an int, a choice, a range, a toggle and a cable leaf each reach the recorder their port names, with
///        the target and value the menu meant, and a refused write leaves the shown value as it was.

#include "scenes/bind/ports/ports.h"
#include "scenes/bind/ports/write_sink.h"
#include "scenes/bind/target.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/nodes/settings.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "deluge-project/device_settings.h"
#include "gui/l10n/l10n.h"
#include "notify/dirty_set.h"
#include "notify/notice_queue.h"
#include "serialization/value.h"
#include "shell/profiles/deluge.h"
#include "shell/scene.h"
#include "shell/shell.h"

#include "support/fake_host_rig.h"
#include "support/mocks/fake_surfaces.h"

#include "cppspec.hpp"

#include <cstdint>
#include <string>

#include "support/spec_main.h"

using namespace deluge::scenes;
using deluge::scenes::bind::Target;
using deluge::scenes::bind::test::RecordingControlsSink;
using deluge::scenes::bind::test::RecordingSoundWrites;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
using device_path = deluge::project::device_path;

namespace {

namespace nodes = deluge::scenes::menu::nodes;

constexpr uint8_t kSelectEncoder = 3;

/// @brief A `Shell` over fake surfaces with the app's scene registry, and the menu's contexts over the ports' fakes.
struct Rig {
	spec::FakeHostRig fakes;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet surfaces{deluge::ui::shell::kDelugeBoard, deluge_profile()};
	Shell shell{surfaces, deluge_profile(), reg, deluge::ui::shell::Services{}, ShellConfig{}};

	Rig() {
		deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
		deluge::notify::Notice discard;
		while (deluge::notify::notices().pop(discard)) {}
		(void)deluge::notify::dirty().drain();
		menu_scene().reset_process_state();
	}
	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
	/// @brief Every scene popped, so the next example's menu starts as a boot leaves it.
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
		menu_scene().reset_process_state();
	}

	/// @brief Open @p page on the synth clip's context, and render it once.
	MenuContext open(const Node& page) {
		const MenuContext ctx = fakes.synth_context();
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(page, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(page)});
		shell.tick(0);
		return ctx;
	}
	/// @brief @p detents clicks of the select encoder: two edges a click.
	void turn(int32_t detents, uint32_t at_ms) {
		shell.on_board_event(
		    DelugeInputEvent{DELUGE_EVENT_ENCODER, kSelectEncoder, 0, static_cast<int16_t>(detents * 2)}, at_ms);
		shell.tick(at_ms + 5);
		shell.tick(at_ms + 10);
	}
};

/// @brief Did the last write ask for the OLED to be redrawn?
bool repainted() {
	return deluge::notify::dirty().drain().has(deluge::notify::Dirty::Oled);
}

} // namespace

// clang-format off
describe menu_value_edits_fakes("the menu's value edits over the ports' fakes and a model song", ${
	it("writes an int param as one request on the edit sink, at its menu value", _ {
		Rig r;
		const MenuContext ctx = r.fakes.synth_context();
		(void)deluge::notify::dirty().drain();
		menu::bindings::write(nodes::lpf_freq, ctx, 30);
		const auto& requests = r.fakes.host.writes.requests;
		expect(requests.size()).to_equal(size_t{1});
		if (!requests.empty()) {
			expect(requests[0].target.kind).to_equal(Target::Kind::PARAM);
			expect(requests[0].target.param).to_equal(nodes::lpf_freq.bind.target.param);
			expect(requests[0].where.slot).to_equal(std::optional{spec::FakeHostRig::kSynthSlot});
			expect(requests[0].menu_value).to_equal(30);
		}
		expect(repainted()).to_be_true();
		expect(r.fakes.host.sound_writes.calls.empty()).to_be_true();
	});

	it("writes a source's transpose, an int its own hook owns, as one request on the sound port", _ {
		Rig r;
		const MenuContext ctx = r.fakes.synth_context();
		menu::bindings::write(nodes::source0_transpose, ctx, 250);
		const auto& calls = r.fakes.host.sound_writes.calls;
		expect(calls.size()).to_equal(size_t{1});
		if (!calls.empty()) {
			expect(calls[0].kind).to_equal(RecordingSoundWrites::Call::Kind::TRANSPOSE);
			expect(calls[0].source).to_equal(uint8_t{0});
			expect(calls[0].where.slot).to_equal(std::optional{spec::FakeHostRig::kSynthSlot});
			expect(calls[0].value).to_equal(int64_t{250});
		}
		expect(r.fakes.host.writes.requests.empty()).to_be_true();
	});

	it("writes a choice as one request naming its effect row and the choice's index", _ {
		Rig r;
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
		Rig r;
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
		Rig r;
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
		Rig r;
		const MenuContext ctx = r.open(nodes::sound_master);
		menu_scene().state_for_spec().columns[0].focused = 4; // SYNTH MODE, VOLUME, PAN, TRANSPOSE, VIBRATO
		r.shell.tick(100);
		const int32_t before = menu::bindings::read(nodes::vibrato, ctx);
		r.turn(1, 1000);
		const auto& requests = r.fakes.host.writes.requests;
		expect(requests.size()).to_equal(size_t{1});
		if (!requests.empty()) {
			expect(requests[0].target.kind).to_equal(Target::Kind::CABLE);
			expect(requests[0].where.slot).to_equal(std::optional{spec::FakeHostRig::kSynthSlot});
			expect(requests[0].menu_value).to_equal(before + 100);
		}
		// The knobs' sink hears the turn edited the synth, as `markInstrumentAsEdited` does.
		const auto& gestures = r.fakes.host.controls_sink.gestures;
		expect(gestures.size()).to_equal(size_t{1});
		if (!gestures.empty()) {
			expect(gestures[0].kind).to_equal(RecordingControlsSink::Gesture::Kind::INSTRUMENT_EDITED);
		}
	});

	it("leaves the shown value and the screen as they were when the edit sink refuses a write", _ {
		Rig r;
		const MenuContext ctx = r.fakes.synth_context();
		r.fakes.host.writes.accept = false;
		const int32_t before = menu::bindings::read(nodes::osc_sync, ctx);
		(void)deluge::notify::dirty().drain();
		menu::bindings::write(nodes::osc_sync, ctx, before == 0 ? 1 : 0);
		// The request reached the sink, which refused it.
		expect(r.fakes.host.writes.requests.size()).to_equal(size_t{1});
		expect(menu::bindings::read(nodes::osc_sync, ctx)).to_equal(before);
		expect(repainted()).to_be_false();
	});

	it("leaves a range's shown ends as they were when the device port refuses a store", _ {
		Rig r;
		const MenuContext ctx = r.fakes.settings_context(/*with_model=*/false);
		const int32_t lower = menu::bindings::read(nodes::default_tempo, ctx);
		r.fakes.host.device.refuse.insert(std::string{device_path::kTempoLower});
		(void)deluge::notify::dirty().drain();
		menu::bindings::write(nodes::default_tempo, ctx, lower + 10);
		expect(r.fakes.host.device.stores.size()).to_equal(size_t{1});
		expect(menu::bindings::read(nodes::default_tempo, ctx)).to_equal(lower);
		expect(repainted()).to_be_false();
	});
});
// clang-format on

DELUGE_SPEC(menu_value_edits_fakes_spec, menu_value_edits_fakes)
