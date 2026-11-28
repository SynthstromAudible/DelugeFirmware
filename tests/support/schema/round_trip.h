#pragma once

/// @file
/// @brief The property every schema keeps: a document it accepts decodes, encodes through JSON text and decodes again
/// to the same record, and a second encode writes the same text.

#include "serialization/parse_json.h"
#include "serialization/schema/codec.h"
#include "serialization/write_json.h"

#include <expected>
#include <string>

namespace deluge::serialization::schema::test {

/// @brief What a round trip allows its decodes to note. The first decode reads the generated document; the second
/// reads what the first record encodes to, in which a clamp or an unknown word no longer shows.
struct RoundTrip {
	bool unknown_keys{false}; ///< unknown keys kept or dropped, in either decode
	bool plugin_kept{false};  ///< plugin nodes kept, in either decode
	bool out_of_range{false}; ///< values clamped and words unknown, in the first decode
};

/// @brief Check the round trip of @p document through @p schema.
/// @tparam R The record type.
/// @param schema   Its schema.
/// @param document A document @p schema accepts.
/// @param user     The caller's data for plugin nodes.
/// @param mode     What the decodes may note.
/// @param onto     The values a decode starts from.
/// @return Nothing, or what failed.
template <typename R>
[[nodiscard]] std::expected<void, std::string> round_trip(Schema<R> schema, const Value& document,
                                                          const void* user = nullptr, RoundTrip mode = {},
                                                          const R& onto = R{}) {
	const auto noted = [&](const LoadReport& report, bool first) -> std::expected<void, std::string> {
		for (const LoadNote& note : report.notes) {
			using enum LoadNote::Kind;
			const bool allowed =
			    (mode.unknown_keys && (note.kind == UNKNOWN_KEY_KEPT || note.kind == UNKNOWN_KEY_DROPPED))
			    || (mode.plugin_kept && note.kind == PLUGIN_STATE_KEPT)
			    || (first && mode.out_of_range && (note.kind == CLAMPED || note.kind == UNKNOWN_WORD));
			if (!allowed) {
				return std::unexpected("noted something at " + note.path + " in " + write_json(document));
			}
		}
		return {};
	};
	auto first = decode(schema, document, user, onto);
	if (!first) {
		return std::unexpected("does not decode at " + first.error().path + ": " + write_json(document));
	}
	if (auto clean = noted(first->report, true); !clean) {
		return clean;
	}
	const std::string written = write_json(encode(schema, first->value, user));
	auto second = decode(schema, *parseJson(written), user, onto);
	if (!second) {
		return std::unexpected("its encode does not decode at " + second.error().path + ": " + written);
	}
	if (auto clean = noted(second->report, false); !clean) {
		return clean;
	}
	if (!(second->value == first->value)) {
		return std::unexpected("its encode decodes to another record: " + written);
	}
	if (write_json(encode(schema, second->value, user)) != written) {
		return std::unexpected("a second encode writes other text: " + written);
	}
	return {};
}

} // namespace deluge::serialization::schema::test
