#pragma once

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"

namespace duckdb {

ScalarFunction GetHFBlobSizeFunction();
TableFunction GetHFDatasetEstimateFunction();
TableFunction GetHFFilesFunction();
TableFunction GetHFProfileFunction();
TableFunction GetHFScanFunction();

void RegisterHuggingFaceFunctions(ExtensionLoader &loader);

} // namespace duckdb
