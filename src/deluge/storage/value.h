#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace deluge::storage {

/// In-memory, format-agnostic parse tree shared by the XML and JSON deserializers.
///
/// A Value is one of:
///   - Scalar: a raw (untyped) string, e.g. attribute/element text.
///   - Map:    an insertion-ordered sequence of (key, Value) pairs. NEVER std::map --
///             later components replay entries in file order, so ordering is load-bearing.
///   - Array:  an ordered sequence of Value.
///
/// This type does no parsing of scalar contents (no int/float/bool conversion); that is
/// the job of later, format-specific consumers.
class Value {
public:
	enum class Type : uint8_t { Scalar, Map, Array };

	[[nodiscard]] static Value makeScalar(std::string s) { return Value{std::move(s)}; }
	[[nodiscard]] static Value makeMap() { return Value{std::vector<std::pair<std::string, Value>>{}}; }
	[[nodiscard]] static Value makeArray() { return Value{std::vector<Value>{}}; }

	// Relies on Type's enumerator order matching data_'s alternative order (Scalar=string,
	// Map=MapType, Array=vector<Value>) -- keep the two declarations in lockstep.
	[[nodiscard]] Type type() const { return static_cast<Type>(data_.index()); }
	[[nodiscard]] bool isScalar() const { return type() == Type::Scalar; }
	[[nodiscard]] bool isMap() const { return type() == Type::Map; }
	[[nodiscard]] bool isArray() const { return type() == Type::Array; }

	// Scalar accessor.
	[[nodiscard]] const std::string& str() const { return std::get<std::string>(data_); }

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
};

} // namespace deluge::storage
