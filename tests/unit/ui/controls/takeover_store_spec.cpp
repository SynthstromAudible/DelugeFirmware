// clang-format off
#include "ui/controls/takeover.h"
#include <cppspec.hpp>
#include <cstdint>

using namespace deluge;
using namespace deluge::ui::controls;

namespace {
using project::ControlsAddress;
using project::MidiCommand;
using project::OwnerParamTarget;

constexpr ControlsAddress kSynth{ControlsAddress::Kind::OUTPUT, 7};
constexpr ControlsAddress kClone{ControlsAddress::Kind::OUTPUT, 8};
constexpr ControlsAddress kRow{ControlsAddress::Kind::DRUM, 7};

OwnerParamTarget knob(ControlsAddress owner, uint32_t param) {
	return OwnerParamTarget{.owner = owner, .param = project::engine_target(param)};
}
MidiCommand cc(uint8_t number) {
	return MidiCommand{.channel_or_zone = 18, .note_or_cc = number};
}
TakeoverState saw(int32_t position) {
	return TakeoverState{.previous = position};
}
} // namespace

describe takeover_store("TakeoverStore", ${
	it("gives a knob it has not seen an empty state, and keeps what is written to it", _{
		TakeoverStore store;
		expect(store.at(knob(kSynth, 1), cc(10))).to_equal(TakeoverState{});
		store.at(knob(kSynth, 1), cc(10)) = saw(33);
		expect(store.at(knob(kSynth, 1), cc(10))).to_equal(saw(33));
		expect(store.size()).to_equal(size_t{1});
	});
	it("keeps a state apart by owner, param and source", _{
		TakeoverStore store;
		store.at(knob(kSynth, 1), cc(10)) = saw(1);
		store.at(knob(kSynth, 2), cc(10)) = saw(2);
		store.at(knob(kSynth, 1), cc(11)) = saw(3);
		store.at(knob(kRow, 1), cc(10)) = saw(4);
		expect(store.at(knob(kSynth, 1), cc(10))).to_equal(saw(1));
		expect(store.at(knob(kSynth, 2), cc(10))).to_equal(saw(2));
		expect(store.at(knob(kSynth, 1), cc(11))).to_equal(saw(3));
		expect(store.at(knob(kRow, 1), cc(10))).to_equal(saw(4));
	});
	it("keeps a state while other knobs are learned and forgotten around it", _{
		TakeoverStore store;
		store.at(knob(kSynth, 1), cc(10)) = saw(5);
		store.at(knob(kSynth, 0), cc(9)) = saw(6); // a knob learned before it in the table
		store.forget(knob(kSynth, 0), cc(9));
		expect(store.at(knob(kSynth, 1), cc(10))).to_equal(saw(5));
	});
	it("moves a state to the source a knob is relearned from", _{
		TakeoverStore store;
		store.at(knob(kSynth, 1), cc(10)) = saw(9);
		store.relearn(knob(kSynth, 1), cc(10), cc(12));
		expect(store.size()).to_equal(size_t{1});
		expect(store.at(knob(kSynth, 1), cc(12))).to_equal(saw(9));
	});
	it("replaces the state of a knob a relearn collapses onto, and moves nothing for a knob with none", _{
		TakeoverStore store;
		store.at(knob(kSynth, 1), cc(10)) = saw(1);
		store.at(knob(kSynth, 1), cc(12)) = saw(2);
		store.relearn(knob(kSynth, 1), cc(10), cc(12));
		expect(store.size()).to_equal(size_t{1});
		expect(store.at(knob(kSynth, 1), cc(12))).to_equal(saw(1));
		store.relearn(knob(kSynth, 1), cc(40), cc(41));
		expect(store.size()).to_equal(size_t{1});
	});
	it("starts the surviving knob over when the knob relearned onto it had no state", _{
		TakeoverStore store;
		store.at(knob(kSynth, 1), cc(12)) = saw(2);
		store.relearn(knob(kSynth, 1), cc(10), cc(12));
		expect(store.find(knob(kSynth, 1), cc(12))).to_be_null();
		expect(store.size()).to_equal(size_t{0});
	});
	it("copies an owner's states to a clone, replacing the clone's own", _{
		TakeoverStore store;
		store.at(knob(kSynth, 1), cc(10)) = saw(1);
		store.at(knob(kSynth, 2), cc(11)) = saw(2);
		store.at(knob(kClone, 9), cc(5)) = saw(3);
		store.clone_owner(kSynth, kClone);
		expect(store.size()).to_equal(size_t{4});
		expect(store.at(knob(kClone, 1), cc(10))).to_equal(saw(1));
		expect(store.at(knob(kClone, 2), cc(11))).to_equal(saw(2));
		expect(store.at(knob(kSynth, 1), cc(10))).to_equal(saw(1));
		expect(store.at(knob(kClone, 9), cc(5))).to_equal(TakeoverState{});
	});
	it("keeps a clone's state its own once cloned", _{
		TakeoverStore store;
		store.at(knob(kSynth, 1), cc(10)) = saw(1);
		store.clone_owner(kSynth, kClone);
		store.at(knob(kClone, 1), cc(10)) = saw(50);
		expect(store.at(knob(kSynth, 1), cc(10))).to_equal(saw(1));
	});
	it("drops one owner's states and no other's", _{
		TakeoverStore store;
		store.at(knob(kSynth, 1), cc(10)) = saw(1);
		store.at(knob(kSynth, 2), cc(10)) = saw(2);
		store.at(knob(kRow, 1), cc(10)) = saw(3);
		store.forget_owner(kSynth);
		expect(store.size()).to_equal(size_t{1});
		expect(store.at(knob(kRow, 1), cc(10))).to_equal(saw(3));
	});
	it("drops one knob's state and no other's", _{
		TakeoverStore store;
		store.at(knob(kSynth, 1), cc(10)) = saw(1);
		store.at(knob(kSynth, 1), cc(11)) = saw(2);
		store.forget(knob(kSynth, 1), cc(10));
		expect(store.size()).to_equal(size_t{1});
		expect(store.at(knob(kSynth, 1), cc(11))).to_equal(saw(2));
	});
	it("drops everything on a clear", _{
		TakeoverStore store;
		store.at(knob(kSynth, 1), cc(10)) = saw(1);
		store.at(knob(kRow, 1), cc(10)) = saw(3);
		store.clear();
		expect(store.size()).to_equal(size_t{0});
		expect(store.at(knob(kSynth, 1), cc(10))).to_equal(TakeoverState{});
	});
});
// clang-format on

CPPSPEC_SPEC(takeover_store)
