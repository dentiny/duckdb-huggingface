# Changelog

## v0.1.0 - 2026-08-06

### Added

- `hf_files` for discovering Hugging Face Parquet files and reporting per-file Parquet, logical blob, and physical
  blob sizes.
- `hf_scan` for querying Hugging Face datasets through DuckDB's Parquet scanner.
- `hf_profile` for summarizing file, row, row-group, and storage metadata.
- `hf_dataset_estimate` for estimating total Parquet and external-blob storage with HyperLogLog and blob sampling.
- Configurable parallel external-blob size resolution.

### Documentation

- Added contribution guidelines and issue templates.
