/// @file
/// @brief A global MIDI command's learn item (`menu/nodes/midi_command.cpp`) over the learn and MIDI devices
///        fakes: what it learns, from which device, and what it refuses.

#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/nodes/midi_command.h"

#include "midi/device_id.h"
#include "project/device_settings.h"
#include "project/midi_command.h"

#include "fakes.h"

#include "cppspec.hpp"
#include "support/printers/learned_binding_printer.h"

#include <cstdint>
#include <optional>

#include "support/spec_main.h"

using deluge::project::GlobalCommand;
using deluge::ui::scenes::bind::LearnedBinding;
using deluge::ui::scenes::bind::test::FakeHost;
using deluge::ui::scenes::bind::test::RecordingLearnSink;
using deluge::ui::scenes::menu::MenuContext;
using Call = RecordingLearnSink::Call;

namespace nodes = deluge::ui::scenes::menu::nodes;

namespace {

/// A hosted USB device the MIDI devices fake knows, so a learn from it is kept.
constexpr deluge::midi::DeviceId kHosted = deluge::midi::kFirstHosted;

/// @brief A context binding only @p host's ports, as the settings root binds them with no song.
MenuContext context_over(FakeHost& host) {
	MenuContext ctx{};
	ctx.ports = host.ports();
	return ctx;
}

} // namespace

// clang-format off
describe menu_midi_command("scenes::menu::nodes::MidiCommandItem -- a global command's learn", ${
	it("learns a note-on onto its command with the device, the channel and the note it came with", _ {
		FakeHost host;
		host.midi.connect(kHosted, "HOSTED");
		const MenuContext ctx = context_over(host);
		expect(nodes::kMidiCommandPlayItem.learn_note_on(ctx, kHosted, 3, 60)).to_be_true();
		const LearnedBinding want{.device = kHosted, .channel_or_zone = 3, .note_or_cc = 60};
		expect(host.learn.calls.size()).to_equal(size_t{1});
		if (host.learn.calls.size() == 1) {
			expect(host.learn.calls[0].kind).to_equal(Call::Kind::LEARN_GLOBAL);
			expect(host.learn.calls[0].global).to_equal(GlobalCommand::PLAY);
			expect(host.learn.calls[0].binding).to_equal(std::optional{want});
		}
		expect(host.midi.global_command(GlobalCommand::PLAY)).to_equal(std::optional{want});
	});

	it("learns nothing from a device the MIDI devices port does not know", _ {
		FakeHost host;
		const MenuContext ctx = context_over(host);
		expect(nodes::kMidiCommandPlayItem.learn_note_on(ctx, kHosted, 3, 60)).to_be_false();
		expect(host.learn.calls.size()).to_equal(size_t{0});
	});

	it("learns a CC in the CC band, and nothing from its zero-valued release", _ {
		FakeHost host;
		const MenuContext ctx = context_over(host);
		expect(nodes::kMidiCommandRecordItem.learn_cc(ctx, deluge::midi::kDin, 2, 20, 0)).to_be_false();
		expect(host.learn.calls.size()).to_equal(size_t{0});
		expect(nodes::kMidiCommandRecordItem.learn_cc(ctx, deluge::midi::kDin, 2, 20, 127)).to_be_true();
		const LearnedBinding cc{.device = deluge::midi::kDin,
		                        .channel_or_zone = static_cast<uint8_t>(deluge::project::kFirstCcChannel + 2),
		                        .note_or_cc = 20};
		expect(host.midi.global_command(GlobalCommand::RECORD)).to_equal(std::optional{cc});
	});

	it("refuses a program change on FILL alone", _ {
		FakeHost host;
		const MenuContext ctx = context_over(host);
		expect(nodes::kMidiCommandFillItem.learn_program_change(ctx, deluge::midi::kDin, 0, 5)).to_be_false();
		expect(host.learn.calls.size()).to_equal(size_t{0});
		expect(nodes::kMidiCommandTapItem.learn_program_change(ctx, deluge::midi::kDin, 0, 5)).to_be_true();
		const LearnedBinding pc{
		    .device = deluge::midi::kDin, .channel_or_zone = deluge::project::kFirstProgramChangeChannel, .note_or_cc = 5};
		expect(host.midi.global_command(GlobalCommand::TAP)).to_equal(std::optional{pc});
	});

	it("unlearns its command as a learn of nothing", _ {
		FakeHost host;
		const MenuContext ctx = context_over(host);
		expect(nodes::kMidiCommandUndoItem.learn_note_on(ctx, deluge::midi::kDin, 1, 40)).to_be_true();
		expect(nodes::kMidiCommandUndoItem.unlearn(ctx)).to_be_true();
		expect(host.learn.calls.size()).to_equal(size_t{2});
		if (host.learn.calls.size() == 2) {
			expect(host.learn.calls[1].kind).to_equal(Call::Kind::LEARN_GLOBAL);
			expect(host.learn.calls[1].global).to_equal(GlobalCommand::UNDO);
			expect(host.learn.calls[1].binding).not_().to_have_value();
		}
		expect(host.midi.global_command(GlobalCommand::UNDO)).not_().to_have_value();
	});
});
// clang-format on

DELUGE_SPEC(menu_midi_command_spec, menu_midi_command)
