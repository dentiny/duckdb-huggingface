#define DUCKDB_EXTENSION_MAIN

#include "huggingface_common.hpp"
#include "huggingface_extension.hpp"
#include "huggingface_functions.hpp"

#include "cache_httpfs_config.hpp"
#include "cache_httpfs_extension.hpp"
#include "duckdb/main/extension_install_info.hpp"
#include "duckdb/main/extension_manager.hpp"

namespace duckdb {

namespace {

constexpr const char *CACHE_HTTPFS_EXTENSION = "cache_httpfs";

// huggingface extension set cache_httpfs extension attributes, so need to ensure it is loaded first.
void EnsureCacheHttpfsExtensionLoaded(ExtensionLoader &loader) {
	auto &instance = loader.GetDatabaseInstance();
	auto &extension_manager = ExtensionManager::Get(instance);
	if (extension_manager.ExtensionIsLoaded(CACHE_HTTPFS_EXTENSION)) {
		return;
	}

	CacheHttpfsExtension().Load(loader);

	auto extension_active_load = extension_manager.BeginLoad(CACHE_HTTPFS_EXTENSION);
	if (!extension_active_load) {
		return;
	}
	ExtensionInstallInfo extension_install_info;
	extension_install_info.mode = ExtensionInstallMode::UNKNOWN;
	extension_active_load->FinishLoad(extension_install_info);
}

// Use in-memory cache for Huggingface access.
void DisablePersistentCache(ExtensionLoader &loader) {
	auto &instance = loader.GetDatabaseInstance();
	SetCacheHttpfsExtensionOption(instance, "cache_httpfs_type", Value("in_mem"));
}

void LoadInternal(ExtensionLoader &loader) {
	EnsureCacheHttpfsExtensionLoaded(loader);
	DisablePersistentCache(loader);
	RegisterHuggingFaceFunctions(loader);
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
