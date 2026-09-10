// tests/spec_io/readback_policy_spec.cpp
#include "storage/readback_policy.h"

#include "cppspec.hpp"

#include <cstring>

using deluge::storage::nextReadBackStage;
using deluge::storage::readBackBytesMatch;
using deluge::storage::ReadBackExpectation;
using deluge::storage::readBackLengthMatches;
using deluge::storage::readBackOffset;
using deluge::storage::ReadBackStage;
using deluge::storage::readBackWant;

namespace {
// A miniature song: the head the song save verifies, a body, and the closing tag WITH its trailing
// newline -- which is what a real song document ends with.
constexpr const char* kDoc = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<song\n body \n</song>\n";
constexpr const char* kHead = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<song\n";
constexpr const char* kTail = "\n</song>\n";

ReadBackExpectation song(const char* tail = kTail) {
	return {.staged = reinterpret_cast<const uint8_t*>(kDoc),
	        .length = static_cast<uint32_t>(strlen(kDoc)),
	        .head = kHead,
	        .tail = tail};
}
const char* tailBytes(const ReadBackExpectation& e) {
	return kDoc + readBackOffset(ReadBackStage::Tail, e);
}
} // namespace

// clang-format off
describe readback_policy("a written document is read back and must match what was built", $ {

	it("checks length, then head, then tail", _ {
		const auto e = song();
		expect(nextReadBackStage(ReadBackStage::Length, e) == ReadBackStage::Head).to_equal(true);
		expect(nextReadBackStage(ReadBackStage::Head, e) == ReadBackStage::Tail).to_equal(true);
		expect(nextReadBackStage(ReadBackStage::Tail, e) == ReadBackStage::Passed).to_equal(true);
	});

	// JSON documents have no closing string to compare, so the tail stage must not run at all.
	it("skips the tail when there is none", _ {
		const auto e = song(nullptr);
		expect(nextReadBackStage(ReadBackStage::Head, e) == ReadBackStage::Passed).to_equal(true);
		expect(readBackWant(ReadBackStage::Tail, e)).to_equal(0u);
	});

	it("accepts only the exact length", _ {
		const auto e = song();
		expect(readBackLengthMatches(e.length, e)).to_equal(true);
		expect(readBackLengthMatches(e.length - 1, e)).to_equal(false);
		expect(readBackLengthMatches(e.length + 1, e)).to_equal(false);
	});

	it("reads the head from the start and the tail from the end", _ {
		const auto e = song();
		expect(readBackOffset(ReadBackStage::Head, e)).to_equal(0u);
		expect(readBackWant(ReadBackStage::Head, e)).to_equal(static_cast<uint32_t>(strlen(kHead)));
		expect(readBackOffset(ReadBackStage::Tail, e)).to_equal(e.length - static_cast<uint32_t>(strlen(kTail)));
		expect(readBackWant(ReadBackStage::Tail, e)).to_equal(static_cast<uint32_t>(strlen(kTail)));
	});

	it("matches a head and tail that agree with both the string and the staged bytes", _ {
		const auto e = song();
		expect(readBackBytesMatch(ReadBackStage::Head, e, kDoc, readBackWant(ReadBackStage::Head, e))).to_equal(true);
		expect(readBackBytesMatch(ReadBackStage::Tail, e, tailBytes(e), readBackWant(ReadBackStage::Tail, e)))
		    .to_equal(true);
	});

	// The card holds a document of the right KIND that is not the one that was built.
	it("rejects bytes that match the string but not the staged buffer", _ {
		const auto e = song();
		char staged[128];
		std::memcpy(staged, kDoc, e.length);
		staged[0] = 'X';
		ReadBackExpectation altered = e;
		altered.staged = reinterpret_cast<const uint8_t*>(staged);
		expect(readBackBytesMatch(ReadBackStage::Head, altered, kDoc, readBackWant(ReadBackStage::Head, e)))
		    .to_equal(false);
	});

	it("rejects bytes that match the staged buffer but not the required string", _ {
		ReadBackExpectation e = song();
		e.head = "<?xml version=\"1.0\"?>\n";
		expect(readBackBytesMatch(ReadBackStage::Head, e, kDoc, readBackWant(ReadBackStage::Head, e))).to_equal(false);
	});

	// The trap the song save shipped with for one run: a document's last byte is a newline, so
	// comparing its tail against a bare "</song>" reads seven bytes that end one short.
	it("rejects a tail string that omits the document's trailing newline", _ {
		const auto bare = song("</song>");
		expect(readBackBytesMatch(ReadBackStage::Tail, bare, tailBytes(bare), readBackWant(ReadBackStage::Tail, bare)))
		    .to_equal(false);
	});

	it("rejects a short read as a mismatch", _ {
		const auto e = song();
		expect(readBackBytesMatch(ReadBackStage::Head, e, kDoc, readBackWant(ReadBackStage::Head, e) - 1)).to_equal(false);
	});

	it("never wraps when the tail is longer than the document", _ {
		ReadBackExpectation e = song();
		e.length = 3;
		expect(readBackOffset(ReadBackStage::Tail, e)).to_equal(0u);
		expect(readBackBytesMatch(ReadBackStage::Tail, e, kDoc, readBackWant(ReadBackStage::Tail, e))).to_equal(false);
	});

	it("carries no bytes for the length and passed stages", _ {
		const auto e = song();
		expect(readBackBytesMatch(ReadBackStage::Length, e, kDoc, 0)).to_equal(false);
		expect(readBackBytesMatch(ReadBackStage::Passed, e, kDoc, 0)).to_equal(false);
	});
});
// clang-format on

CPPSPEC_SPEC(readback_policy)
