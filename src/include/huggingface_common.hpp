#pragma once

#include "duckdb.hpp"
#include "duckdb/common/identifier.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/parser/tableref.hpp"

namespace duckdb {

string GetNamedString(const TableFunctionBindInput &input, const Identifier &name, const string &default_value = "");
unique_ptr<TableRef> ParseHuggingFaceQuery(ClientContext &context, const string &query);

struct HuggingFaceOptions {
	string repository;
	string revision;
	string config;
	string split;
	string path;

	static HuggingFaceOptions Parse(const TableFunctionBindInput &input);
	string Pattern() const;
};

} // namespace duckdb
