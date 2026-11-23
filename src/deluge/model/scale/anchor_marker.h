#pragma once

#include <cstdint>

/** What a Note remembers about its "anchor": the in-key pitch an accidental (a note outside the key) hangs off, and
 * follows through scale changes (see model/scale/accidentals.h).
 *
 * Usually nothing needs remembering, because the anchor follows from the note's pitch and the key. A marker is only
 * stored when a scale change left the note somewhere that doesn't say where it came from. It is one of:
 *
 * - derived (the zero value): no marker. An in-key note is its own anchor; an accidental hangs off the row it is
 *   displayed on.
 * - offset(n): the anchor is n semitones from the note's pitch.
 * - self(): the note is its own anchor, although the key would derive another one for its pitch.
 * - tagged(letter, n): the anchor is n semitones from the note's pitch (n == 0: the note itself), and was the note of
 *   degree letter 0..6 (see model/scale/scale_degrees.h) in a scale whose successor has no such degree. The anchor
 *   kept its pitch when that degree went away, so it may now sit on another degree's pitch or outside the key; the
 *   letter lets it find its degree again.
 *
 * One byte, trivially copyable, and all-zero means derived, so Notes in zeroed raw memory are unmarked. raw() is what
 * song files store.
 */
class AnchorMarker {
public:
	/// Largest distance (either way) a tagged marker can hold
	static constexpr int32_t kMaxTaggedOffset = 4;

	constexpr AnchorMarker() = default;

	static constexpr AnchorMarker derived() { return AnchorMarker{}; }
	static constexpr AnchorMarker self() { return AnchorMarker{kSelf}; }
	/// derived() if semitones is 0 or too large to store
	static constexpr AnchorMarker offset(int32_t semitones) {
		return (semitones < kLowestPlainOffset || semitones > INT8_MAX) ? derived()
		                                                                : AnchorMarker{static_cast<int8_t>(semitones)};
	}
	/// derived() if semitones is further than kMaxTaggedOffset
	static constexpr AnchorMarker tagged(int32_t letter, int32_t semitones) {
		return (semitones < -kMaxTaggedOffset || semitones > kMaxTaggedOffset)
		           ? derived()
		           : AnchorMarker{static_cast<int8_t>(kTagBase + letter * kTagSpan + semitones + kMaxTaggedOffset)};
	}
	static constexpr AnchorMarker fromRaw(int8_t raw) { return AnchorMarker{raw}; }

	[[nodiscard]] constexpr int8_t raw() const { return raw_; }
	[[nodiscard]] constexpr bool isDerived() const { return raw_ == 0; }
	[[nodiscard]] constexpr bool isSelf() const { return raw_ == kSelf; }
	[[nodiscard]] constexpr bool isTagged() const { return raw_ >= kTagBase && raw_ < kLowestPlainOffset; }

	/// The degree letter 0..6 a tagged marker remembers
	[[nodiscard]] constexpr int32_t letter() const { return (raw_ - kTagBase) / kTagSpan; }
	/// Semitones from the note's pitch to its anchor. Meaningless for derived(), which stores no anchor.
	[[nodiscard]] constexpr int32_t semitonesToAnchor() const {
		if (isSelf()) {
			return 0;
		}
		if (isTagged()) {
			return (raw_ - kTagBase) % kTagSpan - kMaxTaggedOffset;
		}
		return raw_;
	}

	constexpr bool operator==(const AnchorMarker&) const = default;

private:
	// Byte layout: kSelf, then 7 letters x kTagSpan tagged values, then plain offsets up to INT8_MAX (0 = derived)
	static constexpr int8_t kSelf = INT8_MIN;
	static constexpr int8_t kTagBase = INT8_MIN + 1;
	static constexpr int32_t kTagSpan = 2 * kMaxTaggedOffset + 1;
	static constexpr int8_t kLowestPlainOffset = kTagBase + 7 * kTagSpan;

	constexpr explicit AnchorMarker(int8_t raw) : raw_(raw) {}

	int8_t raw_ = 0;
};

static_assert(sizeof(AnchorMarker) == 1);
