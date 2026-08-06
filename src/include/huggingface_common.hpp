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

void RegisterHFFiles(ExtensionLoader &loader);
void RegisterHFScan(ExtensionLoader &loader);
void RegisterHFProfile(ExtensionLoader &loader);
void RegisterHFBlobSize(ExtensionLoader &loader);

} // namespace duckdb
