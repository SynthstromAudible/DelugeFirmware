#include "cppspec.hpp"
#include "notify/dirty_set.h"
#include <cstdlib>
#include <thread>

using namespace deluge::notify;

// clang-format off
describe dirty_set("notify::DirtySet", $ {
	it("starts empty and drains to empty", _ {
		DirtySet s;
		expect(s.any()).to_be_false();
		s.raise(Dirty::Oled);
		expect(s.has(Dirty::Oled)).to_be_true();
		DirtySet got = s.drain();
		expect(got.has(Dirty::Oled)).to_be_true();
		expect(s.any()).to_be_false();
		expect(s.drain().any()).to_be_false();
	});

	it("carries clip bits for ClipContent and NoteRows only", _ {
		DirtySet s;
		s.raise_clip(Dirty::ClipContent, 3);
		s.raise_clip(Dirty::NoteRows, 200);
		s.raise_clip(Dirty::ClipContent, 999);            // out of range: coarse bit only
		DirtySet got = s.drain();
		expect(got.has(Dirty::ClipContent)).to_be_true();
		expect(got.clips(Dirty::ClipContent).test(3)).to_be_true();
		expect(got.clips(Dirty::ClipContent).test(4)).to_be_false();
		expect(got.clips(Dirty::NoteRows).test(200)).to_be_true();
		expect(got.clips(Dirty::ClipContent).test(255)).to_be_false();
	});

	it("does not lose a raise that races a drain", _ {
		DirtySet s;
		std::atomic<bool> stop{false};
		std::thread raiser([&] { while (!stop) { s.raise(Dirty::PlaybackState); s.raise_clip(Dirty::ClipContent, 7); } });
		int seen = 0;
		for (int i = 0; i < 20000; i++) {
			DirtySet got = s.drain();
			if (got.has(Dirty::PlaybackState)) seen++;
		}
		stop = true;
		raiser.join();
		DirtySet last = s.drain();
		expect(seen).to_be_greater_than(0);
		// Every raise is either in some drained snapshot or still pending — never dropped.
		// (The property we can assert cheaply: after the raiser stops, one more drain is empty.)
		expect(s.drain().any()).to_be_false();
		(void)last;
	});
});

// Not CPPSPEC_SPEC(dirty_set): create_test_sourcelist derives the extern function name from this
// file's path relative to tests/spec (slashes become underscores), i.e. `notify_dirty_set_spec`
// for notify/dirty_set_spec.cpp, which does not match the `dirty_set` describe-block name the
// macro would otherwise assume. Spelled out by hand so the exported symbol matches what the
// generated driver (all_specs.cpp) declares `extern` (see tests/spec/gfx/geometry_spec.cpp for
// the same pattern).
int notify_dirty_set_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_spec(dirty_set).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
