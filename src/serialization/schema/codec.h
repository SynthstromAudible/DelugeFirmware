#pragma once

/// @file
/// @brief The typed entry points: a record written to and read from a Value tree through its schema.

#include "serialization/schema/traversal.h"
#include "serialization/schema/value_reader.h"
#include "serialization/schema/value_writer.h"

#include <expected>
#include <utility>

namespace deluge::serialization::schema {

/// @brief @p record as a Value tree.
/// @tparam R The record type.
/// @param schema Its schema.
/// @param record The record.
/// @param user   The caller's data for plugin nodes; null for none.
/// @return The record's tree.
template <typename R>
[[nodiscard]] Value encode(Schema<R> schema, const R& record, const void* user = nullptr) {
	ValueWriter out;
	const WalkContext context{user};
	encode_record(*schema.desc, &record, out, context);
	return std::move(out).take();
}

/// @brief The record @p document holds.
/// @tparam R The record type.
/// @param schema   Its schema.
/// @param document The record's tree.
/// @param user     The caller's data for plugin nodes; null for none.
/// @param onto     The values a key the document lacks keeps (a kind's own defaults, for one).
/// @return The record and what its decode did that the document did not say; or why it could not be read.
template <typename R>
[[nodiscard]] std::expected<Loaded<R>, LoadError> decode(Schema<R> schema, const Value& document,
                                                         const void* user = nullptr, R onto = R{}) {
	ValueReader in{document};
	WalkContext context{user};
	if (auto done = decode_record(*schema.desc, &onto, in, context); !done) {
		return std::unexpected(std::move(done.error()));
	}
	return Loaded<R>{std::move(onto), std::move(context).take_report()};
}

} // namespace deluge::serialization::schema
