#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace deluge::serialization {

/// What a scalar was in the document, so a writer emits it the same way: JSON's `96` and `"96"` read alike but are
/// written differently. Every scalar parsed from XML is TEXT.
enum class ScalarKind : uint8_t {
	TEXT,    ///< a string
	NUMBER,  ///< a number, written bare
	LITERAL, ///< JSON's `true`, `false` or `null`, written bare
};

/// In-memory, format-agnostic parse tree shared by the XML and JSON deserializers.
///
/// A Value is one of:
///   - Scalar: a raw (untyped) string, e.g. attribute/element text.
///   - Map:    an insertion-ordered sequence of (key, Value) pairs. NEVER std::map --
///             later components replay entries in file order, so ordering is load-bearing.
///   - Array:  an ordered sequence of Value.
///
/// A scalar keeps its text as written plus its ScalarKind; converting the text (to an int, a hex value…) is the
/// consumer's job (serialization/tree.h).
class Value {
public:
	enum class Type : uint8_t { Scalar, Map, Array };

	[[nodiscard]] static Value makeScalar(std::string s, ScalarKind kind = ScalarKind::TEXT) {
		Value v{std::move(s)};
		v.kind_ = kind;
		return v;
	}
	/// A NUMBER scalar holding @p n in decimal.
	[[nodiscard]] static Value makeNumber(int64_t n) { return makeScalar(std::to_string(n), ScalarKind::NUMBER); }
	[[nodiscard]] static Value makeMap() { return Value{std::vector<std::pair<std::string, Value>>{}}; }
	[[nodiscard]] static Value makeArray() { return Value{std::vector<Value>{}}; }

	// Relies on Type's enumerator order matching data_'s alternative order (Scalar=string,
	// Map=MapType, Array=vector<Value>) -- keep the two declarations in lockstep.
	[[nodiscard]] Type type() const { return static_cast<Type>(data_.index()); }
	[[nodiscard]] bool isScalar() const { return type() == Type::Scalar; }
	[[nodiscard]] bool isMap() const { return type() == Type::Map; }
	[[nodiscard]] bool isArray() const { return type() == Type::Array; }

	// Scalar accessors.
	[[nodiscard]] const std::string& str() const { return std::get<std::string>(data_); }
	[[nodiscard]] ScalarKind scalarKind() const { return kind_; }

	/// Equal when the structure, the order of entries, every key and scalar text, and every scalar's kind match.
	friend bool operator==(const Value& a, const Value& b) {
		if (a.data_.index() != b.data_.index()) {
			return false;
		}
		if (a.isScalar() && a.kind_ != b.kind_) {
			return false;
		}
		return a.data_ == b.data_;
	}

	// Map accessor. Appends (key, value) at the end, preserving insertion order.
	// Duplicate keys are permitted (later components decide how to handle them);
	// find() returns the first match.
	void set(std::string key, Value value) { std::get<MapType>(data_).emplace_back(std::move(key), std::move(value)); }
	[[nodiscard]] const Value* find(std::string_view key) const {
		const auto& entries = std::get<MapType>(data_);
		for (const auto& entry : entries) {
			if (entry.first == key) {
				return &entry.second;
			}
		}
		return nullptr;
	}
	[[nodiscard]] const std::vector<std::pair<std::string, Value>>& entries() const { return std::get<MapType>(data_); }

	// Array accessors.
	void append(Value value) { std::get<std::vector<Value>>(data_).push_back(std::move(value)); }
	[[nodiscard]] const std::vector<Value>& items() const { return std::get<std::vector<Value>>(data_); }

private:
	using MapType = std::vector<std::pair<std::string, Value>>;

	explicit Value(std::string s) : data_(std::move(s)) {}
	explicit Value(MapType m) : data_(std::move(m)) {}
	explicit Value(std::vector<Value> a) : data_(std::move(a)) {}

	std::variant<std::string, MapType, std::vector<Value>> data_;
	ScalarKind kind_{ScalarKind::TEXT}; ///< meaningful only for a Scalar

	// type() casts data_.index() to Type, so the enum's order MUST match the variant's alternatives.
	// Enforce it at compile time — a reorder of either would otherwise silently misclassify every node.
	static_assert(std::variant_size_v<decltype(data_)> == 3);
	static_assert(static_cast<std::size_t>(Type::Scalar) == 0
	              && std::is_same_v<std::variant_alternative_t<0, decltype(data_)>, std::string>);
	static_assert(static_cast<std::size_t>(Type::Map) == 1
	              && std::is_same_v<std::variant_alternative_t<1, decltype(data_)>, MapType>);
	static_assert(static_cast<std::size_t>(Type::Array) == 2
	              && std::is_same_v<std::variant_alternative_t<2, decltype(data_)>, std::vector<Value>>);
};

} // namespace deluge::serialization
