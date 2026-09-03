/// @file
/// @brief The per-device MIDI page (`nodes/midi_device.h`) against the legacy items it ports:
///        `midi::Device midiDeviceMenu`'s five children (`gui/ui/menus.cpp:1444-1453`) and the
///        three-page MPE arm under it (`gui/menu_item/mpe/`).
///
/// Both sides are reachable and both sides are **driven**: the port through `bindings::read`/`write`
/// and a real `Shell` over the real input path, legacy through `readCurrentValue()` /
/// `writeCurrentValue()` / `selectButtonPress()` on the shipped singletons.
///
/// ## The trap this spec is shaped against
///
/// Five near-identical per-cable leaves is exactly the shape where a positional test pins nothing:
/// `sendClock`, `receiveClock` and `is_relative` are three `bool`s on the same object, and a
/// `get`/`set` pair wired to the wrong one of them reads back perfectly from its own hook. So:
///
///   * **the mapping is proved by mutation, and asserted by identity.** Every comparison captures
///     the *whole* observable state of *both* cables as one string (`CableState::describe`) -- all
///     four per-cable fields and all four MPE member-channel numbers, on both -- so a swapped pair
///     of hooks changes the string even though every individual read still round-trips.
///   * **the fixture has two cables**, and every write is checked to leave the other one exactly
///     where it was. `MenuContext::midi_cable` is the first binding input that is a *selection*
///     rather than a global, and a hook that reached for `MIDIDeviceManager::dinMIDIPorts` (or for
///     the wrong `MIDIPort` of the right cable) would pass a one-cable spec.
///   * **each side is driven from its own sentinel.** Legacy is reset to the same baseline, driven
///     with the same value, and its resulting state captured with the same `describe()` -- the two
///     strings are compared, rather than the port being compared against a restatement of its own
///     rule.
///
/// @note The SPEC may include `gui/menu_item/`; the library may not
///       (`scripts/check_layer_rules.py`'s `scenes-includes-outside`). Same rule every other
///       `tables_*_spec.cpp` here runs under.

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/midi_device.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/nodes/settings.h"
#include "scenes/menu/validate.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/integer.h"
#include "gui/menu_item/midi/default_velocity_to_level.h"
#include "gui/menu_item/midi/device.h"
#include "gui/menu_item/midi/device_is_relative.h"
#include "gui/menu_item/midi/device_receive_clock.h"
#include "gui/menu_item/midi/device_send_clock.h"
#include "gui/menu_item/midi/devices.h"
#include "gui/menu_item/mpe/direction_selector.h"
#include "gui/menu_item/mpe/zone_num_member_channels.h"
#include "gui/menu_item/mpe/zone_selector.h"
#include "gui/menu_item/selection.h"
#include "gui/menu_item/submenu.h"
#include "gui/menu_item/toggle.h"
#include "gui/ui/sound_editor.h"

#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include "model/song/song.h"

#include "support/song_fixture.h"

#include "spec/gfx/support/golden.h"
#include "spec/gfx/support/pbm.h"
#include "spec/shell/support/fake_surfaces.h"

#include "cppspec.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "spec/support/spec_main.h"

namespace legacy = deluge::gui::menu_item;

/// @name The legacy globals this spec drives.
///
/// `midiDeviceMenu` is the page itself (`gui/ui/menus.cpp:1444`) and the four non-MPE children are
/// its leaves (`:1440-1443`); none is declared in `gui/ui/menus.h`, which is the same situation
/// `tables_3_spec.cpp:130-142` records for the CV/gate chaining group. The three MPE items *are*
/// declared, by their own headers.
/// @{
extern legacy::midi::Device midiDeviceMenu;
extern legacy::midi::DefaultVelocityToLevel defaultVelocityToLevelMenu;
extern legacy::midi::DeviceSendClock device_send_clock_menu;
extern legacy::midi::DeviceReceiveClock device_receive_clock_menu;
extern legacy::midi::DeviceIsRelative device_is_relative_menu;
/// @}

using namespace deluge::scenes;
using deluge::l10n::String;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

namespace {

namespace nodes = deluge::scenes::menu::nodes;
namespace bindings = deluge::scenes::menu::bindings;

void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::notify::Notice discard;
	while (deluge::notify::notices().pop(discard)) {}
	deluge::notify::dirty().drain();
}

/// Board encoder index and the select-encoder button's matrix position (`shell/profiles/deluge.cpp`).
constexpr uint8_t kSelectEncoder = 3;
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;

/// @brief Read a `Submenu`'s protected child list -- `tables_3_spec.cpp:150-152`'s `SubmenuPeek`,
///        verbatim and for the same [class.protected] reason.
struct SubmenuPeek : legacy::Submenu {
	static const deluge::vector<::MenuItem*>& items_of(legacy::Submenu& menu) { return menu.*(&SubmenuPeek::items); }
};

/// @brief Call a legacy item's protected `writeCurrentValue()` -- `tables_3_spec.cpp:166-168`.
template <typename T>
struct ValuePeek : legacy::Value<T> {
	static void write(legacy::Value<T>& value) { (value.*(&ValuePeek::writeCurrentValue))(); }
};

/// @brief A cable this spec owns outright.
///
/// `MIDICable` is abstract (six pure virtuals) and none of the shipped cable types can be built in
/// a host spec without a USB or DIN backend behind it -- `tables_3_spec.cpp:793-802`'s `FakeCable`
/// makes the same observation. This one additionally **records what was sent**, which is how
/// `ZoneNumMemberChannels::writeCurrentValue`'s MCM tail (`mpe/zone_num_member_channels.cpp:78-84`)
/// is proved rather than assumed.
struct SpecCable final : ::MIDICable {
	explicit SpecCable(const char* n) : display{n} {}
	void writeToFlash(uint8_t*) override {}
	char const* getDisplayName() override { return display; }
	void sendMessage(MIDIMessage message) override {
		sent.push_back(std::to_string(message.channel) + ":" + std::to_string(message.data1) + "="
		               + std::to_string(message.data2));
	}
	void sendSysex(const uint8_t*, int32_t) override {}
	[[nodiscard]] size_t sendBufferSpace() const override { return 0; }

	const char* display;
	std::vector<std::string> sent;

protected:
	void writeReferenceAttributesToFile(Serializer&) override {}
};

/// @brief Every field the page can touch, on both cables, as one string.
///
/// The identity table this spec turns on: four per-cable settings and both `MIDIPort`s' two
/// member-channel numbers, for cable A **and** cable B. A hook wired to the wrong field, the wrong
/// port or the wrong cable moves this string; a hook that merely round-trips through itself does
/// not.
struct CableState {
	std::string text;
	static std::string of(::MIDICable& c) {
		std::string s{c.getDisplayName()};
		s += " vel=" + std::to_string(c.defaultVelocityToLevel);
		s += " sendClock=" + std::to_string(int(c.sendClock));
		s += " recvClock=" + std::to_string(int(c.receiveClock));
		s += " relative=" + std::to_string(int(c.is_relative));
		for (int32_t p = 0; p < 2; p++) {
			s += (p == 0 ? " in[" : " out[");
			s += std::to_string(c.ports[p].mpeLowerZoneLastMemberChannel) + ","
			     + std::to_string(c.ports[p].mpeUpperZoneLastMemberChannel) + "]";
		}
		return s;
	}
	static std::string capture(::MIDICable& a, ::MIDICable& b) { return of(a) + " | " + of(b); }
};

/// @brief Put both cables into a known, **asymmetric** baseline.
///
/// Asymmetric on purpose: every field differs from every other and A differs from B, so any
/// mis-wiring lands on a value the correct wiring would not have produced.
void baseline(SpecCable& a, SpecCable& b) {
	a.defaultVelocityToLevel = 0;
	a.sendClock = true;
	a.receiveClock = false;
	a.is_relative = false;
	a.ports[0].mpeLowerZoneLastMemberChannel = 3;
	a.ports[0].mpeUpperZoneLastMemberChannel = 12;
	a.ports[1].mpeLowerZoneLastMemberChannel = 5;
	a.ports[1].mpeUpperZoneLastMemberChannel = 10;
	a.sent.clear();

	b.defaultVelocityToLevel = 1073741824;
	b.sendClock = false;
	b.receiveClock = true;
	b.is_relative = true;
	b.ports[0].mpeLowerZoneLastMemberChannel = 1;
	b.ports[0].mpeUpperZoneLastMemberChannel = 14;
	b.ports[1].mpeLowerZoneLastMemberChannel = 2;
	b.ports[1].mpeUpperZoneLastMemberChannel = 13;
	b.sent.clear();
}

/// @brief The two cables, and the settings-root context that points at the first of them.
///
/// Built once (`song_fixture.h`'s ownership note) and shared by every example; `baseline()` is what
/// each example resets.
struct Fixture {
	spec::SongFixture song;
	std::unique_ptr<SpecCable> a;
	std::unique_ptr<SpecCable> b;
	MenuContext ctx{};
};

Fixture& fixture() {
	static Fixture f = [] {
		select_language();
		Fixture k;
		k.song = spec::make_synth_song();
		k.a = std::make_unique<SpecCable>("CABLE A");
		k.b = std::make_unique<SpecCable>("CABLE B");
		// The settings root's own context (`tables_3_spec.cpp`'s "settings" fixture): no clip, not a
		// session root. `context_for` never assigns `midi_cable` -- the device list publishes it --
		// so this spec sets it, exactly as `MenuScene::after_render` would.
		k.ctx = menu::context_for(*k.song.song, nullptr, 0, false, false, false, false);
		return k;
	}();
	return f;
}

/// @brief Point the `soundEditor` globals the legacy items read at the same cable and song.
///
/// Only the two members these items actually touch: `currentMIDICable` (all five leaves and both
/// MPE selectors) and `currentSong` (`DefaultVelocityToLevel::writeCurrentValue`'s repatch tail).
void install_legacy(::MIDICable* cable, Song* song) {
	currentSong = song;
	soundEditor.currentMIDICable = cable;
}

/// @brief A 128x64 single-panel board (`menu_scene_spec.cpp:293-314`).
constexpr DelugeDisplayDesc kDisplays64[] = {{128, 64, 0}};

DelugeBoard board_64() {
	DelugeBoard b = deluge::ui::shell::kDelugeBoard;
	b.display_height = 64;
	b.display_count = 1;
	b.displays = kDisplays64;
	return b;
}

const BoardProfile& profile_64() {
	static const BoardProfile p = [] {
		static constexpr const deluge::ui::Style* kStyles[] = {&deluge::ui::kDelugeStyle64};
		BoardProfile q = deluge_profile();
		q.style_by_index = kStyles;
		return q;
	}();
	return p;
}

struct Rig {
	const BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, ShellConfig{}};

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard, const BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p) {}

	void open(const Node& root, const MenuContext& ctx) {
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(root, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(root)});
	}
	void turn(int32_t detents, uint32_t at_ms) {
		shell.on_board_event(
		    DelugeInputEvent{DELUGE_EVENT_ENCODER, kSelectEncoder, 0, static_cast<int16_t>(detents * 2)}, at_ms);
	}
	void button(uint8_t x, uint8_t y, bool on, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)}, at_ms);
	}
	/// @brief One complete select press: down, tick, up.
	void press(uint32_t& at) {
		button(kSelectEncButtonX, kSelectEncButtonY, true, at);
		shell.tick(at + 5);
		at += 10;
		button(kSelectEncButtonX, kSelectEncButtonY, false, at);
		at += 10;
	}
	/// @brief Walk the cursor @p rows down through the real input path, one detent per tick.
	void walk(int32_t rows, uint32_t& at) {
		for (int32_t i = 0; i < rows; i++) {
			turn(1, at);
			shell.tick(at + 5);
			at += 10;
		}
	}
};

std::string text_of(uint16_t id) {
	deluge::ui::shell::TextScratch scratch;
	return std::string{deluge_profile().text_or_digits(id, scratch)};
}

} // namespace

// clang-format off
describe midi_device_shape("scenes::menu E3 task 8: the per-device MIDI page's shape and chain", $ {

	it("declares midiDeviceMenu's five children, in the literal's own order and with its names", _ {
		// `midi::Device midiDeviceMenu{EMPTY_STRING, {...}}` (`gui/ui/menus.cpp:1444-1453`), read off
		// the live legacy object rather than restated here.
		select_language();
		const auto& legacy_items = SubmenuPeek::items_of(midiDeviceMenu);
		expect(legacy_items.size()).to_equal(size_t(5));
		expect(nodes::midi_device.children.size()).to_equal(legacy_items.size());
		expect(nodes::midi_device.kind == menu::Kind::Submenu).to_be_true();

		for (size_t i = 0; i < legacy_items.size() && i < nodes::midi_device.children.size(); i++) {
			const Node* n = nodes::midi_device.children[i];
			expect(std::to_string(i) + ": " + text_of(n->name))
			    .to_equal(std::to_string(i) + ": " + std::string{legacy_items[i]->getName()});
		}
	});

	it("points each of the five at the item it is, and at no other -- by identity", _ {
		// The positional check above would pass with two leaves swapped, because a swap moves both
		// names together. This names each node's own identity: which `CustomItem` (or which pair of
		// hooks) it carries. A hook table shuffled between two of the three `Kind::Toggle` leaves is
		// caught here and again, behaviourally, in the write comparison below.
		expect(nodes::mpe_direction.custom == &nodes::kMpeDirectionItem).to_be_true();
		expect(nodes::mpe_zone.custom == &nodes::kMpeZoneItem).to_be_true();
		expect(nodes::mpe_zone_num_member_channels.custom == &nodes::kMpeZoneChannelsItem).to_be_true();
		expect(nodes::kMpeDirectionItem.is_placeholder()).to_be_false();
		expect(nodes::kMpeZoneItem.is_placeholder()).to_be_false();
		expect(nodes::kMpeZoneChannelsItem.is_placeholder()).to_be_false();

		expect(nodes::device_default_velocity_to_level.bind.get == &nodes::get_device_velocity_to_level).to_be_true();
		expect(nodes::device_default_velocity_to_level.bind.set == &nodes::set_device_velocity_to_level).to_be_true();
		expect(nodes::device_send_clock.bind.get == &nodes::get_device_send_clock).to_be_true();
		expect(nodes::device_send_clock.bind.set == &nodes::set_device_send_clock).to_be_true();
		expect(nodes::device_receive_clock.bind.get == &nodes::get_device_receive_clock).to_be_true();
		expect(nodes::device_receive_clock.bind.set == &nodes::set_device_receive_clock).to_be_true();
		expect(nodes::device_is_relative.bind.get == &nodes::get_device_is_relative).to_be_true();
		expect(nodes::device_is_relative.bind.set == &nodes::set_device_is_relative).to_be_true();

		// The three toggles are `Kind::Toggle` because their legacy items are `Toggle`s
		// (`midi/device_send_clock.h:24`, `device_receive_clock.h:24`, `device_is_relative.h:24`);
		// the velocity one is an `IntegerWithOff` (`default_velocity_to_level.h:24`), so it is
		// `Kind::Int` with `off_at_zero` and `kMaxMenuValue` bounds (`:28`).
		expect(nodes::device_send_clock.kind == menu::Kind::Toggle).to_be_true();
		expect(nodes::device_receive_clock.kind == menu::Kind::Toggle).to_be_true();
		expect(nodes::device_is_relative.kind == menu::Kind::Toggle).to_be_true();
		expect(nodes::device_default_velocity_to_level.kind == menu::Kind::Int).to_be_true();
		expect(nodes::device_default_velocity_to_level.bind.style.off_at_zero).to_be_true();
		expect(bindings::range(nodes::device_default_velocity_to_level, fixture().ctx).max).to_equal(kMaxMenuValue);
	});

	it("chains from the device list onto the page, where legacy chains, driven through the scene", _ {
		// `Devices::selectButtonPress` returns `&midiDeviceMenu` (`gui/menu_item/midi/devices.cpp:154-156`),
		// unconditionally -- checked against the live legacy item, then driven through the real
		// scene and the real input path.
		select_language();
		Fixture& f = fixture();
		install_legacy(f.a.get(), f.song.song);
		expect(legacy::midi::devicesMenu.selectButtonPress() == &midiDeviceMenu).to_be_true();

		MenuContext ctx = f.ctx;
		ctx.midi_cable = f.a.get();
		Rig r;
		r.open(nodes::midi_devices, ctx);
		r.shell.tick(0);
		uint32_t at = 10;
		r.press(at);
		expect(menu_scene().focused() == &nodes::midi_device).to_be_true();
	});

	it("chains onward through the MPE arm exactly as the two legacy selectors do", _ {
		// `DirectionSelector::selectButtonPress` -> `&zoneSelectorMenu` (`mpe/direction_selector.cpp:31-33`);
		// `ZoneSelector::selectButtonPress` -> `&zoneNumMemberChannelsMenu` (`mpe/zone_selector.cpp:32-34`).
		select_language();
		expect(legacy::mpe::directionSelectorMenu.selectButtonPress() == &legacy::mpe::zoneSelectorMenu).to_be_true();
		expect(legacy::mpe::zoneSelectorMenu.selectButtonPress() == &legacy::mpe::zoneNumMemberChannelsMenu)
		    .to_be_true();

		Fixture& f = fixture();
		baseline(*f.a, *f.b);
		MenuContext ctx = f.ctx;
		ctx.midi_cable = f.a.get();
		Rig r;
		r.open(nodes::midi_device, ctx);
		r.shell.tick(0);
		uint32_t at = 10;
		r.press(at); // row 0 is `mpe_direction`
		expect(menu_scene().focused() == &nodes::mpe_direction).to_be_true();
		r.press(at);
		expect(menu_scene().focused() == &nodes::mpe_zone).to_be_true();
		r.press(at);
		expect(menu_scene().focused() == &nodes::mpe_zone_num_member_channels).to_be_true();
	});

	it("declares the page and its three chain-only MPE nodes with ids, and validates them clean", _ {
		// The reachability half: `assign_node_ids` and `validate_tree` follow `chain_targets`, so a
		// page reachable only through a chain is still numbered (addressable by `SceneId::arg`) and
		// still checked. Without `midi_devices`' `select_chain` none of these eight had an id at all.
		select_language();
		(void)menu::assign_node_ids(nodes::kAllRoots);
		for (const Node* n : {&nodes::midi_device, &nodes::mpe_direction, &nodes::mpe_zone,
		                      &nodes::mpe_zone_num_member_channels, &nodes::device_default_velocity_to_level,
		                      &nodes::device_send_clock, &nodes::device_receive_clock, &nodes::device_is_relative}) {
			expect(text_of(n->name) + " id != 0").to_equal(text_of(n->name) + (menu::node_id(*n) != 0 ? " id != 0"
			                                                                                          : " UNNUMBERED"));
		}

		const Node* subtree[] = {&nodes::midi_devices};
		const MenuContext contexts[] = {fixture().ctx};
		const auto issues = menu::validate_tree(subtree, contexts, {}, {},
		                                        menu::ValidateOptions{.skip_shortcut_sweep = true});
		expect(issues.size()).to_equal(size_t(0));
	});
});

describe midi_device_per_cable("scenes::menu E3 task 8: the per-cable binding family", $ {
	// Each example: baseline both cables, drive the PORT hook, capture both cables; baseline again,
	// drive the LEGACY item from its own value, capture again; compare the two strings. The capture
	// is every field on both cables, so "wrote the right field on the right cable" and "left
	// everything else alone" are the same assertion.

	it("writes what DefaultVelocityToLevel writes, on the selected cable and no other", _ {
		select_language();
		Fixture& f = fixture();
		MenuContext ctx = f.ctx;
		ctx.midi_cable = f.a.get();

		for (const int32_t value : {0, 1, 25, kMaxMenuValue}) {
			baseline(*f.a, *f.b);
			bindings::write(nodes::device_default_velocity_to_level, ctx, value);
			const std::string got = CableState::capture(*f.a, *f.b);

			baseline(*f.a, *f.b);
			install_legacy(f.a.get(), f.song.song);
			defaultVelocityToLevelMenu.setValue(value);
			ValuePeek<int32_t>::write(defaultVelocityToLevelMenu);
			expect(std::to_string(value) + ": " + got).to_equal(std::to_string(value) + ": "
			                                                    + CableState::capture(*f.a, *f.b));

			// ... and the port reads back what legacy reads back, from the same state.
			defaultVelocityToLevelMenu.readCurrentValue();
			expect(bindings::read(nodes::device_default_velocity_to_level, ctx))
			    .to_equal(defaultVelocityToLevelMenu.getValue());
		}
	});

	it("writes each of the three per-device clock/relative toggles to its own field, and only it", _ {
		// The mutation the identity table exists for: three `bool`s on the same object. A `set` hook
		// pointed at the wrong one round-trips perfectly through its own `get`, and is caught here
		// only because the capture covers all three on both cables.
		select_language();
		Fixture& f = fixture();
		MenuContext ctx = f.ctx;
		ctx.midi_cable = f.a.get();

		struct Row {
			const Node* node;
			legacy::Toggle* item;
			const char* what;
		};
		const Row rows[] = {
		    {&nodes::device_send_clock, &device_send_clock_menu, "sendClock"},
		    {&nodes::device_receive_clock, &device_receive_clock_menu, "receiveClock"},
		    {&nodes::device_is_relative, &device_is_relative_menu, "is_relative"},
		};

		for (const Row& row : rows) {
			for (const int32_t value : {0, 1}) {
				baseline(*f.a, *f.b);
				bindings::write(*row.node, ctx, value);
				const std::string got = CableState::capture(*f.a, *f.b);

				baseline(*f.a, *f.b);
				install_legacy(f.a.get(), f.song.song);
				row.item->setValue(value != 0);
				ValuePeek<bool>::write(*row.item);
				const std::string label = std::string{row.what} + "=" + std::to_string(value) + ": ";
				expect(label + got).to_equal(label + CableState::capture(*f.a, *f.b));

				row.item->readCurrentValue();
				expect(label + std::to_string(bindings::read(*row.node, ctx)))
				    .to_equal(label + std::to_string(row.item->getValue() ? 1 : 0));
			}
		}
	});

	it("follows the selection: the same node writes cable B once the context names cable B", _ {
		// The wrong-cable catch, stated directly. Nothing about the node changes between the two
		// halves -- only `MenuContext::midi_cable` -- so a hook that reached for anything other than
		// that field (a global, the first hosted device, the DIN ports) fails here.
		select_language();
		Fixture& f = fixture();
		MenuContext on_a = f.ctx;
		on_a.midi_cable = f.a.get();
		MenuContext on_b = f.ctx;
		on_b.midi_cable = f.b.get();

		// Each write is deliberately a **no-op on the cable it addresses** and would be a *change*
		// on the other one, because `baseline()` gives the two cables opposite values (fix round 1,
		// minor 4: the first cut wrote the value the neighbour already held, so half of it passed
		// whichever cable the hook reached). The four assertions below can therefore only hold if
		// the write landed where it was addressed.
		baseline(*f.a, *f.b);
		bindings::write(nodes::device_is_relative, on_a, 0); // A is already false, B is true
		bindings::write(nodes::device_send_clock, on_b, 0);  // B is already false, A is true
		expect(f.a->is_relative).to_be_false();
		expect(f.b->is_relative).to_be_true(); // a write that reached B would have cleared it
		expect(f.b->sendClock).to_be_false();
		expect(f.a->sendClock).to_be_true(); // a write that reached A would have cleared it

		baseline(*f.a, *f.b);
		bindings::write(nodes::device_send_clock, on_b, 1);
		expect(f.b->sendClock).to_be_true();
		expect(f.a->sendClock).to_be_true();     // A's baseline is true and stays true ...
		expect(f.a->receiveClock).to_be_false(); // ... and nothing else on A moved either
		expect(f.a->is_relative).to_be_false();

		// And reads follow the selection too, on a field whose two cables differ.
		baseline(*f.a, *f.b);
		expect(bindings::read(nodes::device_is_relative, on_a)).to_equal(0);
		expect(bindings::read(nodes::device_is_relative, on_b)).to_equal(1);
	});

	it("raises MIDIDeviceManager::anyChangesToSave on every one of the four, as legacy does", _ {
		// Assert-then-clear, not defensive-clear: the flag is a process global shared with every
		// other example in this binary, so each iteration asserts the state it needs *before*
		// setting up, rather than quietly forcing it.
		select_language();
		Fixture& f = fixture();
		MenuContext ctx = f.ctx;
		ctx.midi_cable = f.a.get();
		const bool saved = MIDIDeviceManager::anyChangesToSave;

		for (const Node* n : {&nodes::device_default_velocity_to_level, &nodes::device_send_clock,
		                      &nodes::device_receive_clock, &nodes::device_is_relative}) {
			baseline(*f.a, *f.b);
			MIDIDeviceManager::anyChangesToSave = false;
			expect(MIDIDeviceManager::anyChangesToSave).to_be_false();
			bindings::write(*n, ctx, 1);
			expect(text_of(n->name) + " marked dirty")
			    .to_equal(text_of(n->name) + (MIDIDeviceManager::anyChangesToSave ? " marked dirty" : " LEFT CLEAN"));
		}
		MIDIDeviceManager::anyChangesToSave = saved;
	});

	it("reads 0 and writes nothing when the context names no cable", _ {
		// The port's own guard (`nodes/midi_device.h`): legacy cannot reach the page without
		// `Devices::beginSession` having assigned the pointer, this port can be pointed straight at
		// the node by a `SceneId` deep link.
		select_language();
		Fixture& f = fixture();
		MenuContext none = f.ctx;
		none.midi_cable = nullptr;
		baseline(*f.a, *f.b);
		const std::string before = CableState::capture(*f.a, *f.b);
		for (const Node* n : {&nodes::device_default_velocity_to_level, &nodes::device_send_clock,
		                      &nodes::device_receive_clock, &nodes::device_is_relative}) {
			expect(bindings::read(*n, none)).to_equal(0);
			bindings::write(*n, none, 1);
		}
		expect(CableState::capture(*f.a, *f.b)).to_equal(before);
	});
});

describe midi_device_mpe("scenes::menu E3 task 8: the MPE direction, zone and member-channel pages", $ {

	it("resets the direction and the zone on a forward entry, and keeps them on the way back", _ {
		// `DirectionSelector::beginSession` / `ZoneSelector::beginSession` assign their member only
		// when `navigatedBackwardFrom == nullptr` (`mpe/direction_selector.cpp:24-29`,
		// `mpe/zone_selector.cpp:25-30`); `CustomItem::on_focus` fires only on a descent, which is
		// that condition one hop earlier.
		select_language();
		Fixture& f = fixture();
		baseline(*f.a, *f.b);
		MenuContext ctx = f.ctx;
		ctx.midi_cable = f.a.get();

		Rig r;
		r.open(nodes::midi_device, ctx);
		r.shell.tick(0);
		uint32_t at = 10;
		r.press(at); // into `mpe_direction`
		expect(int(menu_scene().state().mpe_direction)).to_equal(MIDI_DIRECTION_INPUT_TO_DELUGE);

		r.walk(1, at); // OUT
		expect(int(menu_scene().state().mpe_direction)).to_equal(MIDI_DIRECTION_OUTPUT_FROM_DELUGE);

		r.press(at); // into `mpe_zone`; the direction stays where the user left it
		expect(menu_scene().focused() == &nodes::mpe_zone).to_be_true();
		expect(int(menu_scene().state().mpe_direction)).to_equal(MIDI_DIRECTION_OUTPUT_FROM_DELUGE);
		expect(int(menu_scene().state().mpe_zone)).to_equal(MPE_ZONE_LOWER_NUMBERED_FROM_0);

		r.walk(1, at); // UPPER ZONE
		expect(int(menu_scene().state().mpe_zone)).to_equal(1);
		r.press(at); // into the member-channel page: both selections survive the descent
		expect(menu_scene().focused() == &nodes::mpe_zone_num_member_channels).to_be_true();
		expect(int(menu_scene().state().mpe_direction)).to_equal(MIDI_DIRECTION_OUTPUT_FROM_DELUGE);
		expect(int(menu_scene().state().mpe_zone)).to_equal(1);
	});

	it("reads and bounds the member-channel page exactly as the legacy item does, over all four "
	   "(direction, zone) pairs and both cables",
	   _ {
		   // `ZoneNumMemberChannels::readCurrentValue` (`mpe/zone_num_member_channels.cpp:56-64`)
		   // and `getMaxValue` (`:33-54`). Both sides are driven: legacy from its own two selector
		   // singletons, the port from `MenuState`.
		   select_language();
		   Fixture& f = fixture();
		   for (SpecCable* cable : {f.a.get(), f.b.get()}) {
			   baseline(*f.a, *f.b);
			   MenuContext ctx = f.ctx;
			   ctx.midi_cable = cable;
			   install_legacy(cable, f.song.song);

			   for (uint8_t direction = 0; direction < 2; direction++) {
				   for (uint8_t zone = 0; zone < 2; zone++) {
					   legacy::mpe::directionSelectorMenu.whichDirection = direction;
					   legacy::mpe::zoneSelectorMenu.whichZone = zone;
					   legacy::mpe::zoneNumMemberChannelsMenu.readCurrentValue();

					   // A `MenuState` of this example's own rather than the scene singleton's:
					   // `MenuScene::state()` is const, and the item reads nothing else.
					   menu::MenuState st{};
					   st.mpe_direction = direction;
					   st.mpe_zone = zone;
					   nodes::kMpeZoneChannelsItem.on_focus(ctx, st);

					   const std::string label = std::string{cable->getDisplayName()} + " d" + std::to_string(direction)
					                             + "z" + std::to_string(zone) + ": ";
					   expect(label + std::to_string(st.edit_int))
					       .to_equal(label + std::to_string(legacy::mpe::zoneNumMemberChannelsMenu.getValue()));
					   expect(label + "max " + std::to_string(nodes::mpe_zone_channels_range(ctx, st).max))
					       .to_equal(label + "max "
					                 + std::to_string(legacy::mpe::zoneNumMemberChannelsMenu.getMaxValue()));
					   expect(label + "min " + std::to_string(nodes::mpe_zone_channels_range(ctx, st).min))
					       .to_equal(label + "min 0"); // `Number::getMinValue`
				   }
			   }
		   }
	   });

	it("writes the member channels onto the port the two selectors name, and onto no other", _ {
		// The 2x2 mutation: four (direction, zone) pairs address four distinct `uint8_t`s on one
		// cable, and a port index or a zone branch taken the wrong way round still round-trips
		// through its own read. Every write is compared against the legacy item driven from the same
		// pair, with the *whole* two-cable state as the verdict.
		select_language();
		Fixture& f = fixture();
		MenuContext ctx = f.ctx;
		ctx.midi_cable = f.a.get();

		for (uint8_t direction = 0; direction < 2; direction++) {
			for (uint8_t zone = 0; zone < 2; zone++) {
				for (const int32_t value : {0, 4, 7}) {
					baseline(*f.a, *f.b);
					menu::MenuState st{};
					st.mpe_direction = direction;
					st.mpe_zone = zone;
					st.edit_int = value;
					nodes::kMpeZoneChannelsItem.commit(ctx, st);
					const std::string got = CableState::capture(*f.a, *f.b);
					const std::vector<std::string> got_sent = f.a->sent;

					baseline(*f.a, *f.b);
					install_legacy(f.a.get(), f.song.song);
					legacy::mpe::directionSelectorMenu.whichDirection = direction;
					legacy::mpe::zoneSelectorMenu.whichZone = zone;
					legacy::mpe::zoneNumMemberChannelsMenu.setValue(value);
					ValuePeek<int32_t>::write(legacy::mpe::zoneNumMemberChannelsMenu);

					const std::string label =
					    "d" + std::to_string(direction) + "z" + std::to_string(zone) + "=" + std::to_string(value) + ": ";
					expect(label + got).to_equal(label + CableState::capture(*f.a, *f.b));
					// The MCM tail: five CCs on the master channel, but only for an OUT port
					// (`mpe/zone_num_member_channels.cpp:78-84`). Compared against what the legacy
					// write actually sent through the same cable.
					expect(label + std::to_string(got_sent.size())).to_equal(label + std::to_string(f.a->sent.size()));
					for (size_t i = 0; i < got_sent.size() && i < f.a->sent.size(); i++) {
						expect(label + got_sent[i]).to_equal(label + f.a->sent[i]);
					}
					if (direction == MIDI_DIRECTION_OUTPUT_FROM_DELUGE) {
						expect(label + std::to_string(got_sent.size())).to_equal(label + "5");
					}
					else {
						expect(label + std::to_string(got_sent.size())).to_equal(label + "0");
					}
				}
			}
		}
	});

	it("titles the direction page from its own value, as DirectionSelector::getTitle does", _ {
		// `direction_selector.h:40-43` -- the runtime title. `Node::title` is one static id, which is
		// half of why this item is bespoke.
		//
		// **What this pins, and what it does not** (fix round 1, minor 3). Both sides spell the same
		// l10n mapping -- legacy's `getTitle()` picks MPE OUTPUT on a non-zero `whichDirection`, and
		// the expectation below restates that -- so this is a *transcription* check on the mapping,
		// not an independent comparison: swap the two ids inside `MpeDirectionItem::render` and this
		// example still passes. What catches that swap is the pair of goldens in
		// `midi_device_pixels` below, which render the item itself one detent apart. The same is
		// true of the options example that follows: it pins legacy's own option order, not the
		// port's copy of it.
		select_language();
		for (uint8_t direction = 0; direction < 2; direction++) {
			legacy::mpe::directionSelectorMenu.whichDirection = direction;
			const std::string want{legacy::mpe::directionSelectorMenu.getTitle()};
			const std::string got = text_of(static_cast<uint16_t>(direction != 0 ? String::STRING_FOR_MPE_OUTPUT
			                                                                    : String::STRING_FOR_MPE_INPUT));
			expect("direction " + std::to_string(direction) + ": " + got)
			    .to_equal("direction " + std::to_string(direction) + ": " + want);
		}
		// ... and the two answers really are different, so the example above is not vacuous.
		expect(text_of(static_cast<uint16_t>(String::STRING_FOR_MPE_INPUT))
		       != text_of(static_cast<uint16_t>(String::STRING_FOR_MPE_OUTPUT)))
		    .to_be_true();
		// The static fallback the dump and the parent row carry is `Node::title`.
		expect(text_of(nodes::mpe_direction.name)).to_equal(text_of(static_cast<uint16_t>(String::STRING_FOR_MPE)));
	});

	it("offers the same options the two legacy Selections do", _ {
		select_language();
		const auto directions = legacy::mpe::directionSelectorMenu.getOptions(legacy::Selection::OptType::FULL);
		expect(directions.size()).to_equal(size_t(2));
		expect(std::string{directions[0]}).to_equal(text_of(static_cast<uint16_t>(String::STRING_FOR_IN)));
		expect(std::string{directions[1]}).to_equal(text_of(static_cast<uint16_t>(String::STRING_FOR_OUT)));

		const auto zones = legacy::mpe::zoneSelectorMenu.getOptions(legacy::Selection::OptType::FULL);
		expect(zones.size()).to_equal(size_t(2));
		expect(std::string{zones[0]}).to_equal(text_of(static_cast<uint16_t>(String::STRING_FOR_LOWER_ZONE)));
		expect(std::string{zones[1]}).to_equal(text_of(static_cast<uint16_t>(String::STRING_FOR_UPPER_ZONE)));
	});
});

describe midi_device_pixels("scenes::menu E3 task 8: the per-device page's pixels", $ {
	// Both panel geometries for every screen this task adds, because a row or a value-band constant
	// that is right on one panel and wrong on the other is exactly what a single golden cannot see
	// (`tables_3_spec.cpp`'s own note on the same point). Nothing here hand-computes a rect: the two
	// selectors lay out through `ui::oled::menu_rect(f)` and the member-channel editor through
	// `f.bounds()`, like `MenuScene::render_leaf_int`.
	//
	// Every screen is reached by *driving* the scene rather than by poking `MenuState`
	// (`MenuScene::state()` is const, deliberately), so a golden here is a screen the user can
	// actually get to.

	/// @brief Open @p node, walk @p rows detents, tick, and golden both panels under @p name.
	auto shoot = [&](auto& self_ref, const Node& node, const char* name, int32_t rows) {
		Fixture& f = fixture();
		baseline(*f.a, *f.b);
		MenuContext ctx = f.ctx;
		ctx.midi_cable = f.a.get();
		{
			Rig r;
			r.open(node, ctx);
			r.shell.tick(0);
			uint32_t at = 10;
			r.walk(rows, at);
			r.shell.tick(at + 5);
			expect_golden(self_ref, std::string{"../ui/menu/"} + name + "_48.pbm", r.fake.pbm(0));
		}
		{
			Rig r{board_64(), profile_64()};
			r.open(node, ctx);
			r.shell.tick(0);
			uint32_t at = 10;
			r.walk(rows, at);
			r.shell.tick(at + 5);
			expect_golden(self_ref, std::string{"../ui/menu/"} + name + "_64.pbm", r.fake.pbm(0));
		}
	};

	it("renders the per-device page's five rows", _ { shoot(self, nodes::midi_device, "menu_midi_device_page", 0); });

	it("renders the MPE direction selector, titled MPE INPUT on IN and MPE OUTPUT on OUT", _ {
		// The pair that shows the runtime title (`DirectionSelector::getTitle`,
		// `mpe/direction_selector.h:40-43`) actually reaching the panel: same node, same context,
		// one detent apart, and the two goldens differ in the title band as well as in the cursor.
		shoot(self, nodes::mpe_direction, "menu_mpe_direction_in", 0);
		shoot(self, nodes::mpe_direction, "menu_mpe_direction_out", 1);
	});

	it("renders the MPE zone selector", _ { shoot(self, nodes::mpe_zone, "menu_mpe_zone", 0); });

	it("renders the member-channel editor over the baseline cable's IN port lower zone", _ {
		// `baseline()` puts 3 there, so the huge digit is a value this example chose rather than a
		// default -- and `off_at_zero` is not what is being drawn.
		shoot(self, nodes::mpe_zone_num_member_channels, "menu_mpe_member_channels", 0);
	});
});
// clang-format on

/// Four `describe` groups in one TU, so `DELUGE_SPEC` is spelled out here with `add_specs`.
int midi_device_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(midi_device_shape, midi_device_per_cable, midi_device_mpe, midi_device_pixels)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
