#pragma once

#include "duckdb.hpp"
#include "duckdb/common/identifier.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/parser/tableref.hpp"

namespace duckdb {

//! Read a named string parameter, using default_value when it is absent or NULL.
string GetNamedString(const TableFunctionBindInput &input, const Identifier &name, const string &default_value = "");
//! Parse generated SELECT SQL into a subquery table reference for bind replacement.
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
