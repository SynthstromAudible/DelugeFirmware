#include "deluge-project/song.h"

#include <cppspec.hpp>

#include <memory>
#include <new>

// clang-format off
describe song_identity("a song's identity", ${
	it("is new for every song, even one made where another was", _ {
		alignas(deluge::project::Song) std::byte memory[sizeof(deluge::project::Song)];
		auto* first = new (memory) deluge::project::Song();
		const uint64_t first_identity = first->identity();
		std::destroy_at(first);
		auto* second = new (memory) deluge::project::Song();
		expect(second->identity() != first_identity).to_be_true();
		std::destroy_at(second);
	});
});

CPPSPEC_SPEC(song_identity)
