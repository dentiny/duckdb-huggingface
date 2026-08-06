#include "huggingface_common.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"

namespace duckdb {

namespace {

string GetNamedString(const TableFunctionBindInput &input, const string &name, const string &default_value = "") {
	auto entry = input.named_parameters.find(name);
	if (entry == input.named_parameters.end() || entry->second.IsNull()) {
		return default_value;
	}
	return entry->second.GetValue<string>();
}

void ValidateRepository(const string &repository) {
	if (repository.empty() || repository.find('/') == string::npos) {
		throw InvalidInputException("Hugging Face repository must use the 'owner/name' form");
	}
}

string TrimSlashes(string value) {
	while (!value.empty() && value.front() == '/') {
		value.erase(value.begin());
	}
	while (!value.empty() && value.back() == '/') {
		value.pop_back();
	}
	return value;
}

void InferConfigAndSplitFromPath(HuggingFaceOptions &options) {
	auto path = TrimSlashes(options.path);
	if (path.empty() || path.find_first_of("*?[") != string::npos) {
		return;
	}
	auto first_separator = path.find('/');
	if (first_separator == string::npos) {
		return;
	}
	auto second_separator = path.find('/', first_separator + 1);
	if (second_separator == string::npos) {
		return;
	}
	if (options.config.empty()) {
		options.config = path.substr(0, first_separator);
	}
	if (options.split.empty()) {
		options.split = path.substr(first_separator + 1, second_separator - first_separator - 1);
	}
}

} // namespace

HuggingFaceOptions HuggingFaceOptions::Parse(const TableFunctionBindInput &input) {
	if (input.inputs[0].IsNull()) {
		throw InvalidInputException("Hugging Face repository cannot be NULL");
	}
	HuggingFaceOptions result;
	result.repository = input.inputs[0].GetValue<string>();
	ValidateRepository(result.repository);
	result.revision = GetNamedString(input, "revision", "~parquet");
	result.config = GetNamedString(input, "config");
	result.split = GetNamedString(input, "split");
	result.path = GetNamedString(input, "path");
	InferConfigAndSplitFromPath(result);
	return result;
}

string HuggingFaceOptions::Pattern() const {
	auto normalized_path = TrimSlashes(path);
	auto normalized_config = TrimSlashes(config.empty() ? "**" : config);
	auto normalized_split = TrimSlashes(split.empty() ? "**" : split);
	auto pattern_path = normalized_path;
	if (pattern_path.empty()) {
		pattern_path = StringUtil::Format("%s/%s/**/*.parquet", normalized_config, normalized_split);
	}
	return StringUtil::Format("hf://datasets/%s@%s/%s", repository, revision, pattern_path);
}

} // namespace duckdb
