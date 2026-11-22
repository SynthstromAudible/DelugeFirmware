#pragma once

/// @file
/// @brief The fixtures of the per-device MIDI page: two cables the page can be pointed at, the whole observable state
///        of both as one string, and the asymmetric baseline every example starts from. The spec and the menu oracle
///        freezer share them.

#include "gui/ui/menu_view_of.h"
#include "scenes/menu/context.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/midi_device.h"
#include "scenes/menu/nodes/nodes.h"

#include "gui/l10n/l10n.h"
#include "notify/notice_queue.h"

#include "io/midi/midi_device.h"
#include "model/song/song.h"

#include "support/menu_context.h"
#include "support/song_fixture.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace deluge::scenes::spec::mididev {

namespace nodes = deluge::scenes::menu::nodes;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;

/// @brief A cable the fixture owns outright.
///
/// `MIDICable` is abstract (six pure virtuals) and none of the shipped cable types can be built in a host spec without
/// a USB or DIN backend behind it. This one additionally **records what was sent**, which is how the member-channel
/// write's MCM tail is checked rather than assumed.
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
/// Five per-cable settings and both `MIDIPort`s' two member-channel numbers, for cable A **and** cable B. A hook wired
/// to the wrong field, the wrong port or the wrong cable moves this string; a hook that merely round-trips through
/// itself does not.
struct CableState {
	static std::string of(::MIDICable& c) {
		std::string s{c.getDisplayName()};
		s += " vel=" + std::to_string(c.defaultVelocityToLevel);
		s += " sendClock=" + std::to_string(int(c.sendClock));
		s += " recvClock=" + std::to_string(int(c.receiveClock));
		s += " relative=" + std::to_string(int(c.is_relative));
		s += " thru=" + std::to_string(int(c.midi_thru));
		for (int32_t p = 0; p < 2; p++) {
			s += (p == 0 ? " in[" : " out[");
			s += std::to_string(c.ports[p].mpeLowerZoneLastMemberChannel) + ","
			     + std::to_string(c.ports[p].mpeUpperZoneLastMemberChannel) + "]";
		}
		return s;
	}
	static std::string capture(::MIDICable& a, ::MIDICable& b) { return of(a) + " | " + of(b); }
};

/// @brief Put both cables into a known, **asymmetric** baseline: every field differs from every other and A differs
///        from B, so any mis-wiring lands on a value the correct wiring would not have produced.
inline void baseline(SpecCable& a, SpecCable& b) {
	a.defaultVelocityToLevel = 0;
	a.sendClock = true;
	a.receiveClock = false;
	a.is_relative = false;
	a.midi_thru = true;
	a.ports[0].mpeLowerZoneLastMemberChannel = 3;
	a.ports[0].mpeUpperZoneLastMemberChannel = 12;
	a.ports[1].mpeLowerZoneLastMemberChannel = 5;
	a.ports[1].mpeUpperZoneLastMemberChannel = 10;
	a.sent.clear();

	b.defaultVelocityToLevel = 1073741824;
	b.sendClock = false;
	b.receiveClock = true;
	b.is_relative = true;
	b.midi_thru = false;
	b.ports[0].mpeLowerZoneLastMemberChannel = 1;
	b.ports[0].mpeUpperZoneLastMemberChannel = 14;
	b.ports[1].mpeLowerZoneLastMemberChannel = 2;
	b.ports[1].mpeUpperZoneLastMemberChannel = 13;
	b.sent.clear();
}

/// @brief The two cables, and the settings-root context that points at the first of them.
///
/// Built once (`song_fixture.h`'s ownership note) and shared by every example; `baseline()` is what each example
/// resets.
struct Fixture {
	spec::SongFixture song;
	std::unique_ptr<SpecCable> a;
	std::unique_ptr<SpecCable> b;
	MenuContext ctx{};
};

inline Fixture& fixture() {
	static Fixture f = [] {
		deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
		deluge::notify::Notice discard;
		while (deluge::notify::notices().pop(discard)) {}
		deluge::notify::dirty().drain();
		Fixture k;
		k.song = spec::make_synth_song();
		k.a = std::make_unique<SpecCable>("CABLE A");
		k.b = std::make_unique<SpecCable>("CABLE B");
		// The settings root's own context: no clip, not a session root. `context_for` never assigns `midi_device` --
		// the device list publishes it -- so each example sets it, as `MenuScene::after_render` would.
		k.ctx = spec::ctx_of(*k.song.song, deluge::gui::menuViewOf(nullptr), 0, false, false, false, false);
		return k;
	}();
	return f;
}

/// @brief A node pointer as a name the failure message can print: the pages the device list and the MPE arm chain
///        onto.
inline std::string chain_name(const Node* n) {
	if (n == nullptr) {
		return "<ascend>";
	}
	if (n == &nodes::midi_device) {
		return "device";
	}
	if (n == &nodes::mpe_zone) {
		return "mpe_zone";
	}
	if (n == &nodes::mpe_zone_num_member_channels) {
		return "mpe_members";
	}
	return "UNMAPPED";
}

} // namespace deluge::scenes::spec::mididev
