#include "huggingface_functions.hpp"

#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"

namespace duckdb {

namespace {

FunctionDescription CreateDescription(vector<string> parameter_names, string description, vector<string> examples,
                                      vector<string> categories) {
	FunctionDescription result;
	result.parameter_names = std::move(parameter_names);
	result.description = std::move(description);
	result.examples = std::move(examples);
	result.categories = std::move(categories);
	return result;
}

void RegisterScalarFunction(ExtensionLoader &loader, ScalarFunction function, vector<string> parameter_names,
                            string description, vector<string> examples, vector<string> categories) {
	CreateScalarFunctionInfo info(std::move(function));
	info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
	info.descriptions.push_back(CreateDescription(std::move(parameter_names), std::move(description),
	                                              std::move(examples), std::move(categories)));
	loader.RegisterFunction(std::move(info));
}

void RegisterTableFunction(ExtensionLoader &loader, TableFunction function, vector<string> positional_parameter_names,
                           string description, vector<string> examples, vector<string> categories) {
	for (const auto &parameter : function.named_parameters) {
		positional_parameter_names.push_back(parameter.first);
	}

	CreateTableFunctionInfo info(std::move(function));
	info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
	info.descriptions.push_back(CreateDescription(std::move(positional_parameter_names), std::move(description),
	                                              std::move(examples), std::move(categories)));
	loader.RegisterFunction(std::move(info));
}

} // namespace

void RegisterHuggingFaceFunctions(ExtensionLoader &loader) {
	RegisterTableFunction(
	    loader, GetHFFilesFunction(), {"repository"},
	    "Discovers matching Parquet files in a Hugging Face dataset and reports file and optional external-blob sizes.",
	    {"SELECT * FROM hf_files('ibm/duorc', config = 'ParaphraseRC', split = 'train');"}, {"hugging_face", "files"});
	RegisterTableFunction(
	    loader, GetHFScanFunction(), {"repository"},
	    "Scans matching Parquet files in a Hugging Face dataset, with projection and filter pushdown.",
	    {"SELECT * FROM hf_scan('ibm/duorc', config = 'ParaphraseRC', split = 'train') LIMIT 10;"},
	    {"hugging_face", "scanning"});
	RegisterTableFunction(
	    loader, GetHFProfileFunction(), {"repository"},
	    "Summarizes file counts, row counts, row-group counts, and Parquet storage for a Hugging Face dataset.",
	    {"SELECT * FROM hf_profile('ibm/duorc', config = 'ParaphraseRC', split = 'train');"},
	    {"hugging_face", "profiling"});
	RegisterTableFunction(
	    loader, GetHFDatasetEstimateFunction(), {"repository"},
	    "Estimates Parquet and external-blob storage for a Hugging Face dataset from sampled rows and blob references.",
	    {"SELECT * FROM hf_dataset_estimate('owner/dataset', blob_column = 'images');"}, {"hugging_face", "profiling"});
	RegisterScalarFunction(
	    loader, GetHFBlobSizeFunction(), {"url", "concurrency"},
	    "Returns the byte size of a Hugging Face or HTTP object, or NULL when its size cannot be resolved.",
	    {"SELECT huggingface_internal_blob_size('hf://datasets/owner/dataset@~parquet/file.parquet', 1);"},
	    {"hugging_face", "internal"});
}

} // namespace duckdb
