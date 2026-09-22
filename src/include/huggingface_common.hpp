#pragma once

#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"

namespace duckdb {

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
