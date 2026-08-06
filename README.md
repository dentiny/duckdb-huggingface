# DuckDB Hugging Face

A utility extension for understanding datasets on the Hugging Face Hub directly from DuckDB. It helps users discover
Parquet files, inspect dataset structure, profile metadata, query records, and estimate external-blob storage before
building a larger ingestion or processing pipeline.

It uses cache_httpfs, which wraps DuckDB HTTPFS and its `hf://` filesystem with remote-read caching, while retaining
Parquet projection and filter pushdown.

## Usage

Load the required extensions:

```sql
LOAD cache_httpfs;
LOAD parquet;
LOAD huggingface;
```

The default revision is Hugging Face's auto-converted `~parquet` branch. Repositories use the `owner/name` form.

### Selecting files with `path`

`path` is relative to the root of the selected dataset repository and `revision`. When supplied, it takes precedence
over the default `config/split/**/*.parquet` discovery pattern.

For example:

```sql
-- One Parquet file
path = 'data-v1.1/partial-train/0000.parquet'

-- Every Parquet file in a directory
path = 'data-v1.1/partial-train/*.parquet'
```

With repository `mlfoundations/MINT-1T-HTML` and the default revision, the first example resolves to:

```text
hf://datasets/mlfoundations/MINT-1T-HTML@~parquet/data-v1.1/partial-train/0000.parquet
```

Omit `path` to discover all matching Parquet files, optionally restricted with `config` and `split`.

### Discover Parquet files

List every matching Parquet file:

```sql
FROM hf_files(
    'ibm/duorc',
    config = 'ParaphraseRC',
    split = 'train'
);
```

Without `blob_column`, `hf_files` performs inexpensive file discovery and returns only file metadata plus
`parquet_size_bytes`. Blob-related columns are added only when blob sizing is explicitly enabled.

Count files and measure their physical storage:

```sql
SELECT
    count(*) AS parquet_file_count,
    sum(parquet_size_bytes) AS parquet_size_bytes
FROM hf_files('mlfoundations/MINT-1T-HTML');
```

### Inspect the schema

Use DuckDB's native Parquet function with a path returned by `hf_files`:

```sql
FROM parquet_schema(
    'hf://datasets/mlfoundations/MINT-1T-HTML@~parquet/data-v1.1/partial-train/0000.parquet'
);
```

### Query a dataset

```sql
SELECT *
FROM hf_scan(
    'ibm/duorc',
    config = 'ParaphraseRC',
    split = 'train'
)
LIMIT 10;
```

`hf_scan` delegates to DuckDB's `parquet_scan`, preserving glob expansion, projection pushdown, filter pushdown,
revision handling, and Hugging Face authentication.

### Profile Parquet metadata

```sql
FROM hf_profile(
    'ibm/duorc',
    config = 'ParaphraseRC',
    split = 'train'
);
```

The profile reports file count, rows, row groups, and physical Parquet storage.

### Estimate total dataset storage

For URL-backed image datasets, estimate total Parquet and external-blob storage without issuing one request per blob:

```sql
FROM hf_dataset_estimate(
    'mlfoundations/MINT-1T-HTML',
    blob_column = 'images',
    blob_hash_column = 'image_hashes',
    blob_sample_size = 1000,
    blob_sample_pool_size = 4000,
    blob_concurrency = 8
);
```

The function reports exact Parquet file and logical blob counts, an approximate distinct physical blob count,
sample resolution statistics, average sampled blob size, and estimated blob and overall dataset sizes.
`blob_hash_column` is optional; without it, distinct URLs are counted instead. Distinct counts use HyperLogLog, while
blob sizes are extrapolated from a reservoir sample, so blob and overall byte values are estimates.

Estimation methodology:

1. Unnest `blob_column`, discard `NULL` and empty values, and count every remaining reference to obtain
   `logical_blob_count`.
2. Estimate `estimated_physical_blob_count` with DuckDB's HyperLogLog-based `approx_count_distinct`. The identity is
   `blob_hash_column` when supplied; otherwise it is the blob URL from `blob_column`.
3. Reservoir-sample `blob_sample_pool_size` URL references, deduplicate that sample, and retain at most
   `blob_sample_size` URLs. The pool defaults to four times `blob_sample_size`; both values are configurable.
4. Resolve sampled URLs in parallel. `average_blob_size_bytes` includes only successfully resolved URLs.
5. Calculate `estimated_blob_size_bytes` as `estimated_physical_blob_count * average_blob_size_bytes`, then add exact
   Parquet bytes to obtain `estimated_dataset_size_bytes`.

### Measure per-file Parquet and blob storage

The physical Parquet size includes binary values stored inline, but it does not include blobs referenced by external
URLs. Pass a `VARCHAR[]` URL column to `hf_files` to resolve external sizes in parallel:

```sql
FROM hf_files(
    'mlfoundations/MINT-1T-HTML',
    blob_column = 'images',
    blob_concurrency = 64
);
```

Each Parquet row reports:

- `parquet_size_bytes`: physical Parquet object size.
- `logical_blob_count` and `logical_blob_size_bytes`: all references, including repeated URLs.
- `physical_blob_count` and `physical_blob_size_bytes`: distinct URLs within that Parquet file.
- `size_bytes`: Parquet plus physical blob storage; `NULL` if any distinct blob size is unresolved.
- `unresolved_blob_count`: distinct URLs whose size could not be determined (i.e., file not exist, or IO failure).

Blob URLs are fetched only once across the query, even when referenced repeatedly. However, the same URL can appear
in multiple output rows, so summing per-file physical sizes may double-count blobs shared across Parquet files.

Blob concurrency defaults to 32 and accepts values from 1 to 256.

External sizing performs one remote metadata request per distinct URL. It can be expensive for datasets containing
millions or billions of unique blobs.

## Supported functions

`hf_files(repository, ...)` discovers Parquet files and optionally measures per-file logical and physical blob sizes:

```sql
FROM hf_files(
    'mlfoundations/MINT-1T-HTML',
    path = 'data-v1.1/partial-train/*.parquet'
);
```

`hf_scan(repository, ...)` queries a Hugging Face dataset with DuckDB's Parquet scanner:

```sql
SELECT *
FROM hf_scan('ibm/duorc', config = 'ParaphraseRC', split = 'train')
LIMIT 10;
```

`hf_profile(repository, ...)` summarizes file, row, row-group, and storage metadata:

```sql
FROM hf_profile('ibm/duorc', config = 'ParaphraseRC', split = 'train');
```

`hf_dataset_estimate(repository, ...)` efficiently estimates total Parquet and external-blob storage:

```sql
FROM hf_dataset_estimate(
    'mlfoundations/MINT-1T-HTML',
    path = 'data-v1.1/partial-train/0000.parquet',
    blob_column = 'images',
    blob_sample_size = 100,
    blob_sample_pool_size = 400,
    blob_concurrency = 14
);
```

The Hugging Face functions accept `revision`, `config`, `split`, and `path` options where applicable.

## Authentication

Public datasets require no credentials. For private or gated datasets, configure a DuckDB Hugging Face secret:

```sql
CREATE SECRET hf_token (
    TYPE HUGGINGFACE,
    PROVIDER credential_chain
);
```

cache_httpfs delegates Hugging Face authentication to its bundled HTTPFS implementation when resolving `hf://` paths.
The credential chain checks `HF_TOKEN`, then the file named by `HF_TOKEN_PATH`, then `$HF_HOME/token`, and finally
`~/.cache/huggingface/token`.

## Contributing

Bug reports, feature requests, and pull requests are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for development,
testing, formatting, and review guidelines.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
