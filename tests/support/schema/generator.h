#pragma once

/// @file
/// @brief Random documents a schema accepts: every scalar inside its rule, every list within its capacity with unique
/// ids, every record of several kinds with its kind first. Plugin nodes are `null`.

#include "serialization/schema/descriptor.h"
#include "serialization/schema/plugin_node.h"
#include "serialization/value.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <variant>

namespace deluge::serialization::schema::test {

struct GeneratorOptions {
	bool unknown_keys{false}; ///< also write a key no schema has, `zzUnknown`, into some records
	bool out_of_range{false}; ///< also write some numbers past their rule and some words outside their list
	bool plugin_nodes{false}; ///< write plugin nodes with no entries, which no plugin's state is (else `null`)
	std::size_t max_items{3}; ///< the most items a list gets
};

/// @brief Builds random documents a schema accepts. The same seed builds the same document.
class Generator {
public:
	explicit Generator(uint32_t seed, GeneratorOptions options = {}) : rng_{seed}, options_{options} {}

	/// @brief A random document of the record @p desc describes.
	/// @param desc The record's descriptor.
	/// @return The document.
	[[nodiscard]] Value record(const RecordDesc& desc) { return fields(desc, Value::makeMap()); }

private:
	template <typename... Fs>
	struct Overloaded : Fs... {
		using Fs::operator()...;
	};

	[[nodiscard]] int64_t pick(int64_t low, int64_t high) {
		return std::uniform_int_distribution<int64_t>{low, high}(rng_);
	}
	[[nodiscard]] bool one_in(int64_t n) { return pick(1, n) == 1; }
	[[nodiscard]] static Value null() { return Value::makeScalar("null", ScalarKind::LITERAL); }

	Value fields(const RecordDesc& desc, Value map) {
		for (const FieldDesc& field : desc.fields) {
			map.set(std::string{field.key}, value(field));
		}
		if (options_.unknown_keys && one_in(3)) {
			map.set("zzUnknown", Value::makeNumber(pick(-9, 9)));
		}
		return map;
	}

	Value value(const FieldDesc& field) {
		return std::visit(Overloaded{
		                      [&](const ScalarShape& shape) { return scalar(shape); },
		                      [&](const RecordShape& shape) { return record(*shape.record); },
		                      [&](const OptionalShape& shape) { return one_in(2) ? null() : value(*shape.value); },
		                      [&](const ListShape& shape) { return list(shape); },
		                      [&](const VariantShape& shape) { return variant(*shape.variant); },
		                      [&](const PluginShape& shape) { return plugin_node(shape); },
		                  },
		                  field.shape);
	}

	Value variant(const VariantDesc& desc) {
		const Alternative& alternative =
		    desc.alternatives[static_cast<std::size_t>(pick(0, std::ssize(desc.alternatives) - 1))];
		Value map = Value::makeMap();
		map.set(std::string{kKindKey}, Value::makeScalar(std::string{alternative.word}));
		return fields(*alternative.record, std::move(map));
	}

	Value list(const ListShape& shape) {
		const auto count =
		    std::min(shape.ops->capacity, static_cast<std::size_t>(pick(0, static_cast<int64_t>(options_.max_items))));
		Value items = Value::makeArray();
		for (std::size_t i = 0; i < count; ++i) {
			Value item = value(*shape.element);
			if (!shape.id_key.empty()) {
				item.replace_or_append(shape.id_key, Value::makeNumber(fresh_id(shape)));
			}
			items.append(std::move(item));
		}
		return items;
	}

	/// An id no other item of any list has, inside the id field's range.
	int64_t fresh_id(const ListShape& shape) {
		const ScalarRule rule = id_rule(shape);
		const int64_t span = rule.max - std::max<int64_t>(rule.min, 0);
		return std::max<int64_t>(rule.min, 0) + (next_id_++ % (span + 1));
	}

	static ScalarRule id_rule(const ListShape& shape) {
		const RecordDesc* record = nullptr;
		if (const auto* single = std::get_if<RecordShape>(&shape.element->shape)) {
			record = single->record;
		}
		else if (const auto* several = std::get_if<VariantShape>(&shape.element->shape)) {
			record = several->variant->alternatives.front().record;
		}
		if (record != nullptr) {
			for (const FieldDesc& field : record->fields) {
				if (field.key == shape.id_key) {
					if (const auto* id = std::get_if<ScalarShape>(&field.shape)) {
						return id->rule;
					}
				}
			}
		}
		return {.min = 0, .max = 1'000'000};
	}

	/// A node whose entries are empty: what no plugin's state reads as, so the decode keeps it.
	Value plugin_node(const PluginShape& shape) {
		if (!options_.plugin_nodes) {
			return null();
		}
		Value node = Value::makeMap();
		node.set(std::string{kVersionKey}, Value::makeNumber(shape.ops->version));
		node.set(std::string{kTickMagnitudeKey}, Value::makeNumber(0));
		node.set(std::string{kEntriesKey}, Value::makeMap());
		return node;
	}

	Value scalar(const ScalarShape& shape) {
		if (options_.out_of_range && one_in(4)) {
			if (const std::optional<Value> outside = out_of_range(shape)) {
				return *outside;
			}
		}
		if (!shape.rule.words.empty()) {
			return Value::makeScalar(
			    std::string{shape.rule.words[static_cast<std::size_t>(pick(0, std::ssize(shape.rule.words) - 1))]});
		}
		const std::string_view form = shape.codec->form;
		if (form == "bool") {
			return Value::makeScalar(one_in(2) ? "true" : "false", ScalarKind::LITERAL);
		}
		if (form == "text") {
			return Value::makeScalar(text());
		}
		if (form == "real") {
			return real();
		}
		return Value::makeNumber(integer(shape.rule));
	}

	/// A value just outside @p shape's rule: a word no list has, or a number one past an end the rule narrows.
	std::optional<Value> out_of_range(const ScalarShape& shape) {
		if (!shape.rule.words.empty()) {
			return Value::makeScalar("zzNoSuchWord");
		}
		if (shape.codec->form != "integer") {
			return std::nullopt;
		}
		constexpr int64_t kNarrowest = std::numeric_limits<int32_t>::max();
		if (shape.rule.max < kNarrowest) {
			return Value::makeNumber(shape.rule.max + 1);
		}
		if (shape.rule.min > -kNarrowest) {
			return Value::makeNumber(shape.rule.min - 1);
		}
		return std::nullopt;
	}

	int64_t integer(const ScalarRule& rule) {
		switch (pick(0, 3)) {
		case 0:
			return rule.min;
		case 1:
			return rule.max;
		default: {
			const int64_t low = std::max<int64_t>(rule.min, -100'000);
			const int64_t high = std::min<int64_t>(rule.max, 100'000);
			return low <= high ? pick(low, high) : rule.min;
		}
		}
	}

	/// A real a float holds exactly, so that a float member reads back what was written.
	Value real() {
		const auto value = static_cast<double>(std::uniform_real_distribution<float>{-1000.F, 1000.F}(rng_));
		std::array<char, 32> digits{};
		const auto [end, error] = std::to_chars(digits.data(), digits.data() + digits.size(), value);
		return Value::makeScalar(std::string{digits.data(), end}, ScalarKind::NUMBER);
	}

	std::string text() {
		static constexpr std::string_view kCharacters = "abcXYZ019 _-\"\\/";
		std::string made;
		for (int64_t n = pick(0, 8); n > 0; --n) {
			made += kCharacters[static_cast<std::size_t>(pick(0, std::ssize(kCharacters) - 1))];
		}
		return made;
	}

	std::mt19937 rng_;
	GeneratorOptions options_;
	int64_t next_id_{1};
};

} // namespace deluge::serialization::schema::test
