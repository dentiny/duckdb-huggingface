#include "huggingface_common.hpp"

#include "duckdb/common/exception.hpp"

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
	return result;
}

string HuggingFaceOptions::Pattern() const {
	auto normalized_path = TrimSlashes(path);
	auto normalized_config = TrimSlashes(config.empty() ? "**" : config);
	auto normalized_split = TrimSlashes(split.empty() ? "**" : split);
	auto pattern_path = normalized_path;
	if (pattern_path.empty()) {
		pattern_path = normalized_config + "/" + normalized_split + "/**/*.parquet";
	}
	return "hf://datasets/" + repository + "@" + revision + "/" + pattern_path;
}

} // namespace duckdb
