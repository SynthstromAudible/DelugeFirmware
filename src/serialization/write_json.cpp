#include "serialization/write_json.h"

#include <cstdio>

namespace deluge::serialization {
namespace {

void write_text(std::string& out, const std::string& text) {
	out += '"';
	for (unsigned char c : text) {
		switch (c) {
		case '"':
			out += "\\\"";
			break;
		case '\\':
			out += "\\\\";
			break;
		case '\n':
			out += "\\n";
			break;
		case '\t':
			out += "\\t";
			break;
		case '\r':
			out += "\\r";
			break;
		default:
			if (c < 0x20) {
				char escaped[7];
				std::snprintf(escaped, sizeof escaped, "\\u%04x", c);
				out += escaped;
			}
			else {
				out += static_cast<char>(c);
			}
		}
	}
	out += '"';
}

void write_value(std::string& out, const Value& value, int depth) {
	const auto indent = [&](int d) { out.append(static_cast<size_t>(d), '\t'); };
	switch (value.type()) {
	case Value::Type::Scalar:
		if (value.scalarKind() == ScalarKind::TEXT) {
			write_text(out, value.str());
		}
		else {
			out += value.str();
		}
		return;
	case Value::Type::Map: {
		if (value.entries().empty()) {
			out += "{}";
			return;
		}
		out += "{\n";
		bool first = true;
		for (const auto& [key, child] : value.entries()) {
			out += first ? "" : ",\n";
			first = false;
			indent(depth + 1);
			write_text(out, key);
			out += ": ";
			write_value(out, child, depth + 1);
		}
		out += '\n';
		indent(depth);
		out += '}';
		return;
	}
	case Value::Type::Array: {
		if (value.items().empty()) {
			out += "[]";
			return;
		}
		out += "[\n";
		bool first = true;
		for (const Value& item : value.items()) {
			out += first ? "" : ",\n";
			first = false;
			indent(depth + 1);
			write_value(out, item, depth + 1);
		}
		out += '\n';
		indent(depth);
		out += ']';
		return;
	}
	}
}

} // namespace

std::string write_json(const Value& document) {
	std::string out;
	write_value(out, document, 0);
	out += '\n';
	return out;
}

} // namespace deluge::serialization
