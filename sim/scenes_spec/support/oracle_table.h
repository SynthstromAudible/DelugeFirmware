#pragma once

/// @file
/// @brief The menu reference tables under `tests/golden/ui/menu_oracle/`: upstream `main`'s menu answers, one row
///        per answer, which the menu's specs are checked against.
///
/// A table is a text file of rows. A row is one line: a path, a column and zero or more values, separated by tabs. The
/// path names what answered (a node by its child positions under its root, a setting case, a shortcut cell), the
/// column names the question (`name`, `read`, `rel@kit row`, `write@synth/25`), and the values are the answer. Blank
/// lines and lines that start with `#` are skipped; a tab, a newline or a backslash inside a field is written `\t`,
/// `\n` or `\\`.
///
/// The rows are reference data, not a recording to refresh: nothing in the tree produces them. A row changes only to
/// follow upstream `main`'s source (`git show origin/main:<path>`), and the commit names the source it follows.
///
/// Every row is a claim, so a spec that reads a table also checks it asked every row (`unasked`): a row the port no
/// longer reaches fails as loudly as a wrong answer.

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef GOLDEN_ROOT
#error "GOLDEN_ROOT must be defined by CMake (deluge_golden_root, tests/support/cmake/golden.cmake)"
#endif

namespace deluge::scenes::spec {

/// @brief One row of a reference table.
struct OracleRow {
	std::string path;
	std::string column;
	std::vector<std::string> values;
};

/// @brief @p field as a table line holds it: tab, newline and backslash escaped.
inline std::string oracle_escape(std::string_view field) {
	std::string out;
	for (const char c : field) {
		switch (c) {
		case '\t':
			out += "\\t";
			break;
		case '\n':
			out += "\\n";
			break;
		case '\\':
			out += "\\\\";
			break;
		default:
			out += c;
		}
	}
	return out;
}

/// @brief The field @p escaped stands for; an unknown escape keeps its character.
inline std::string oracle_unescape(std::string_view escaped) {
	std::string out;
	for (size_t i = 0; i < escaped.size(); i++) {
		if (escaped[i] != '\\' || i + 1 == escaped.size()) {
			out += escaped[i];
			continue;
		}
		const char next = escaped[++i];
		out += next == 't' ? '\t' : next == 'n' ? '\n' : next;
	}
	return out;
}

/// @brief The directory the tables live in.
inline std::string oracle_dir() {
	return std::string{GOLDEN_ROOT} + "/ui/menu_oracle";
}

/// @brief A reference table: its rows, and which of them a spec has asked.
class OracleTable {
public:
	/// @brief The rows of @p text. A line with no tab, or a second row with the same path and column, is recorded in
	///        `problems()` and kept out of the table.
	static OracleTable parse(std::string_view text) {
		OracleTable t;
		size_t start = 0;
		size_t number = 0;
		while (start <= text.size()) {
			const size_t end = std::min(text.find('\n', start), text.size());
			const std::string_view line = text.substr(start, end - start);
			number++;
			start = end + 1;
			if (line.empty() || line.front() == '#') {
				continue;
			}
			std::vector<std::string> fields;
			size_t from = 0;
			while (true) {
				const size_t tab = line.find('\t', from);
				fields.push_back(oracle_unescape(line.substr(from, tab == std::string_view::npos ? tab : tab - from)));
				if (tab == std::string_view::npos) {
					break;
				}
				from = tab + 1;
			}
			if (fields.size() < 2) {
				t.problems_.push_back("line " + std::to_string(number) + ": no column");
				continue;
			}
			OracleRow row{std::move(fields[0]), std::move(fields[1]), {}};
			row.values.assign(std::make_move_iterator(fields.begin() + 2), std::make_move_iterator(fields.end()));
			const std::string key = row.path + '\t' + row.column;
			if (!t.index_.emplace(key, t.rows_.size()).second) {
				t.problems_.push_back("line " + std::to_string(number) + ": a second row for " + row.path + " "
				                      + row.column);
				continue;
			}
			t.rows_.push_back(std::move(row));
		}
		t.asked_.assign(t.rows_.size(), false);
		return t;
	}

	/// @brief `tests/golden/ui/menu_oracle/<name>.tsv`, read once per process; a missing file is a problem, and an
	///        empty table.
	static OracleTable& named(std::string_view name) {
		static std::map<std::string, std::unique_ptr<OracleTable>, std::less<>> loaded;
		if (const auto it = loaded.find(name); it != loaded.end()) {
			return *it->second;
		}
		std::ifstream in(oracle_dir() + "/" + std::string{name} + ".tsv");
		std::stringstream text;
		text << in.rdbuf();
		auto table = std::make_unique<OracleTable>(parse(text.str()));
		if (!in.good() && table->rows_.empty()) {
			table->problems_.push_back(std::string{name} + ".tsv: missing");
		}
		return *loaded.emplace(std::string{name}, std::move(table)).first->second;
	}

	[[nodiscard]] size_t size() const { return rows_.size(); }
	[[nodiscard]] const std::vector<std::string>& problems() const { return problems_; }

	/// @brief The values of the row (@p path, @p column), and that row marked asked; null when there is no such row.
	const std::vector<std::string>* find(std::string_view path, std::string_view column) {
		std::string key{path};
		key += '\t';
		key += column;
		const auto it = index_.find(key);
		if (it == index_.end()) {
			return nullptr;
		}
		asked_[it->second] = true;
		return &rows_[it->second].values;
	}

	/// @brief The first value of (@p path, @p column), or `<no row>`/`<no value>`, so a miss prints as one.
	std::string value(std::string_view path, std::string_view column) {
		const std::vector<std::string>* v = find(path, column);
		return v == nullptr ? std::string{"<no row>"} : v->empty() ? std::string{"<no value>"} : v->front();
	}

	/// @brief Every row of @p path whose column starts with @p prefix, in file order, each marked asked.
	std::vector<const OracleRow*> columns(std::string_view path, std::string_view prefix) {
		std::vector<const OracleRow*> out;
		for (size_t i = 0; i < rows_.size(); i++) {
			if (rows_[i].path == path && rows_[i].column.starts_with(prefix)) {
				asked_[i] = true;
				out.push_back(&rows_[i]);
			}
		}
		return out;
	}

	/// @brief Every row whose path starts with @p prefix, in file order, each marked asked.
	std::vector<const OracleRow*> under(std::string_view prefix) {
		std::vector<const OracleRow*> out;
		for (size_t i = 0; i < rows_.size(); i++) {
			if (rows_[i].path.starts_with(prefix)) {
				asked_[i] = true;
				out.push_back(&rows_[i]);
			}
		}
		return out;
	}

	/// @brief "path column" of every row nothing asked, in file order, one per line.
	[[nodiscard]] std::string unasked() const {
		std::string out;
		for (size_t i = 0; i < rows_.size(); i++) {
			if (!asked_[i]) {
				out += rows_[i].path + " " + rows_[i].column + "\n";
			}
		}
		return out;
	}

	/// @brief Like `unasked`, over the rows whose column starts with one of @p columns.
	[[nodiscard]] std::string unasked(std::initializer_list<std::string_view> columns) const {
		std::string out;
		for (size_t i = 0; i < rows_.size(); i++) {
			const bool mine =
			    std::ranges::any_of(columns, [&](std::string_view c) { return rows_[i].column.starts_with(c); });
			if (mine && !asked_[i]) {
				out += rows_[i].path + " " + rows_[i].column + "\n";
			}
		}
		return out;
	}

private:
	std::vector<OracleRow> rows_;
	std::vector<bool> asked_;
	std::map<std::string, size_t, std::less<>> index_;
	std::vector<std::string> problems_;
};

} // namespace deluge::scenes::spec
