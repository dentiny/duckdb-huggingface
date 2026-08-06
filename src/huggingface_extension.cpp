#define DUCKDB_EXTENSION_MAIN

#include "huggingface_common.hpp"
#include "huggingface_extension.hpp"

namespace duckdb {

namespace {

void LoadInternal(ExtensionLoader &loader) {
	loader.SetDescription("Discover and query Hugging Face datasets through cache_httpfs");
	RegisterHFFiles(loader);
	RegisterHFScan(loader);
	RegisterHFSchema(loader);
	RegisterHFProfile(loader);
	RegisterHFDatasetSize(loader);
}

} // namespace

void HuggingfaceExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

std::string HuggingfaceExtension::Name() {
	return "huggingface";
}

std::string HuggingfaceExtension::Version() const {
#ifdef EXT_VERSION_HUGGINGFACE
	return EXT_VERSION_HUGGINGFACE;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(huggingface, loader) {
	duckdb::LoadInternal(loader);
}
}
