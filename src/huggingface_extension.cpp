#define DUCKDB_EXTENSION_MAIN

#include "huggingface_common.hpp"
#include "huggingface_extension.hpp"

#include "cache_httpfs_config.hpp"

namespace duckdb {

namespace {

// Use in-memory cache for Huggingface access.
void DisablePersistentCache(ExtensionLoader &loader) {
	auto &instance = loader.GetDatabaseInstance();
	SetCacheHttpfsExtensionOption(instance, "cache_httpfs_type", Value("in_mem"));
}

void LoadInternal(ExtensionLoader &loader) {
	DisablePersistentCache(loader);
	RegisterHFFiles(loader);
	RegisterHFScan(loader);
	RegisterHFProfile(loader);
	RegisterHFBlobSize(loader);
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
