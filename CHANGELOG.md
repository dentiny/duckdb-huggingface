# Changelog

## v0.1.2

### Changed

- Update DuckDB and extension-ci-tools to `v1.5.6`.
- Update cache_httpfs to use DuckDB `v1.5.6` and its matching httpfs dependency.

### Added

- Added descriptions, examples, categories, and parameter names to `duckdb_functions()` for all Hugging Face
  functions.

## v0.1.1

### Fixed

- `cache_httpfs` needs to be loaded first ([#13])

[#13]: https://github.com/dentiny/duckdb-huggingface/pull/13

## v0.1.0

### Added

- `hf_files` for discovering Hugging Face Parquet files and reporting per-file Parquet, logical blob, and physical
  blob sizes.
- `hf_scan` for querying Hugging Face datasets through DuckDB's Parquet scanner.
- `hf_profile` for summarizing file, row, row-group, and storage metadata.
- `hf_dataset_estimate` for estimating total Parquet and external-blob storage with HyperLogLog and blob sampling.
- Configurable parallel external-blob size resolution.

### Documentation

- Added contribution guidelines and issue templates.
