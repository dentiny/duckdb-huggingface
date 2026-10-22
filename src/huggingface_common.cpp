#include "huggingface_common.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"
#include <string_view>

namespace duckdb {

namespace {

void ValidateRepository(const string &repository) {
	if (repository.empty() || repository.find('/') == string::npos) {
		throw InvalidInputException("Hugging Face repository must use the 'owner/name' form");
	}
}

std::string_view TrimSlashes(std::string_view value) {
	const auto first = value.find_first_not_of('/');
	if (first == std::string_view::npos) {
		return {};
	}
	return value.substr(first, value.find_last_not_of('/') - first + 1);
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
		options.config = string(path.substr(0, first_separator));
	}
	if (options.split.empty()) {
		options.split = string(path.substr(first_separator + 1, second_separator - first_separator - 1));
	}
}

} // namespace

string GetNamedString(const TableFunctionBindInput &input, const Identifier &name, const string &default_value) {
	auto entry = input.named_parameters.find(name);
	if (entry == input.named_parameters.end() || entry->second.IsNull()) {
		return default_value;
	}
	return entry->second.GetValue<string>();
}

unique_ptr<TableRef> ParseHuggingFaceQuery(ClientContext &context, const string &query) {
	Parser parser(context.GetParserOptions());
	parser.ParseQuery(query);
	auto statement = unique_ptr_cast<SQLStatement, SelectStatement>(std::move(parser.statements[0]));
	return make_uniq<SubqueryRef>(std::move(statement));
}

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
	auto normalized_config = TrimSlashes(config.empty() ? std::string_view("**") : std::string_view(config));
	auto normalized_split = TrimSlashes(split.empty() ? std::string_view("**") : std::string_view(split));
	string pattern_path(normalized_path);
	if (pattern_path.empty()) {
		pattern_path = StringUtil::Format("%s/%s/**/*.parquet", string(normalized_config), string(normalized_split));
	}
	return StringUtil::Format("hf://datasets/%s@%s/%s", repository, revision, pattern_path);
}

} // namespace duckdb
