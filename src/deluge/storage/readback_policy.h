#pragma once

#include <cstdint>

namespace deluge::storage {

/// @brief What a document on the card must match after it has been written.
///
/// The check the streaming writer did by reopening the file it had just closed. Building a document
/// in memory skips it, because `FileWriter::closeAfterWriting` returns before verifying anything when
/// the writer is memory-based -- so a staged save has to ask for it explicitly.
struct ReadBackExpectation {
	const uint8_t* staged; ///< The bytes that were written. Borrowed; must outlive the check.
	uint32_t length;       ///< Their count.
	const char* head;      ///< Required first bytes. Never null.
	const char* tail;      ///< Required last bytes, or nullptr for no tail comparison.
};

/// @brief Which part of the read-back runs next.
enum class ReadBackStage : uint8_t {
	Length, ///< The file's size on the card.
	Head,   ///< Its first `strlen(head)` bytes.
	Tail,   ///< Its last `strlen(tail)` bytes. Skipped when there is no tail.
	Passed, ///< Every stage matched.
};

/// @brief The stage after `stage` has passed. PURE.
/// @param stage  The stage that just matched.
/// @param expect What the document must match; a null `tail` skips `Tail`.
/// @return The next stage, or `Passed` after the last one.
[[nodiscard]] ReadBackStage nextReadBackStage(ReadBackStage stage, const ReadBackExpectation& expect);

/// @brief Where to read from for a `Head` or `Tail` stage. PURE.
/// @return 0 for `Head`; `length - strlen(tail)` for `Tail`, clamped to 0 rather than wrapping when the
///         tail is longer than the document. 0 for any other stage.
[[nodiscard]] uint32_t readBackOffset(ReadBackStage stage, const ReadBackExpectation& expect);

/// @brief How many bytes to read for a `Head` or `Tail` stage. PURE.
/// @return `strlen(head)` or `strlen(tail)`; 0 for any other stage, or for `Tail` with no tail.
[[nodiscard]] uint32_t readBackWant(ReadBackStage stage, const ReadBackExpectation& expect);

/// @brief Whether the file's size on the card is the size that was written. PURE.
/// @param onCard The size the card reports, in bytes.
[[nodiscard]] bool readBackLengthMatches(uint32_t onCard, const ReadBackExpectation& expect);

/// @brief Whether bytes read for a `Head` or `Tail` stage match. PURE.
///
/// Requires BOTH the required string AND the staged buffer at the same offset. The string says the
/// document is the right shape; the buffer says the card holds what was actually built. Either alone
/// is weaker: a string-only check passes a different document of the same kind, and a buffer-only
/// check passes a document that was built wrong in the first place.
///
/// @param got    The bytes read.
/// @param gotLen How many were read. A short read is a mismatch, never a partial match.
/// @return false for `Length` and `Passed`, which carry no bytes.
[[nodiscard]] bool readBackBytesMatch(ReadBackStage stage, const ReadBackExpectation& expect, const char* got,
                                      uint32_t gotLen);

} // namespace deluge::storage
