# Reader function

## `read_dicom`

Read DICOM files directly into DuckDB, returning one row per file. Files that can't be read as DICOM get a NULL entry in
the `dicom_content` column.

**Parameters:**

| Parameter | Type | Description | Required |
|-----------|------|-------------|----------|
| `filepath` | `VARCHAR` | Path to the DICOM file read or glob pattern. | Yes |
| `load_pixel_data`| `BOOL` | Whether to import the pixel data. Default is false. | No |

**Return schema:**

| Column Name | Data Type | Description |
|-------------|-----------|-------------|
| `filename` | `VARCHAR` | Path to the DICOM file. |
| `dicom_content` | `JSON` | JSON-rendered contents of the DICOM files. |

**Examples:**

```sql
-- read one file
FROM read_dicom('path/to/dicom_file.dcm');

-- use glob pattern
FROM read_dicom('path/to/dicoms/**/*.dcm');

-- import pixel data
FROM read_dicom('path/to/dicom_file.dcm', load_pixel_data=true);

-- extract series description per series instance
SELECT
    dicom_content->'0020000E'->'Value' AS series_instance_uid,
    any_value(dicom_content->'0008103E'->'Value') AS series_description
FROM read_dicom('~/Downloads/slicer_export/**/*.dcm')
GROUP BY 1;

-- extract study description per study instance
SELECT
    dicom_content->'0020000D'->'Value' AS study_instance_uid,
    any_value(dicom_content->'00081030'->'Value') AS study_description
FROM read_dicom('~/Downloads/slicer_export/**/*.dcm')
GROUP BY 1;
```

The `dicom` extension also supports reading from cloud object storage through the [`httpfs`
extension](https://duckdb.org/docs/current/core_extensions/httpfs/overview):

```sql
-- configure httpfs and credentials
LOAD httpfs;

CREATE OR REPLACE SECRET my_secret (
    TYPE s3,
    PROVIDER config,
    KEY_ID 'my_key',
    SECRET 'my_secret_key',
    URL_STYLE 'path'
);

-- read one file from an AWS S3 bucket
FROM read_dicom('s3://my-bucket/path/to/dicom_file.dcm');

-- use glob pattern
FROM read_dicom('s3://my-bucket/path/to/dicoms/**/*.dcm');
```

## Advanced configuration

The DICOM format covers a wide variety of imaging modalities. The settings that `read_dicom` uses to operate are
intended to provide "common-sense" defaults. However, since DICOM files can have wildly different sizes, having
"one-size-fits-all" settings is extremely challenging. The `dicom` extension provides advanced
configuration options to tune the behavior of `read_dicom` to users' specific use cases.

**How `read_dicom` works under the hood**

By default, the DCMTK reader parses bytes directly from a stream on demand. This results in a high volume of small read
operations. This has very detrimental performance implication when parsing files from remote storage, as each small
read incurs significant network round-trip latency.

By default, DCMTK's reader reads bytes from a data stream as it needs to parse them. This mean that, by default, the
DCMTK reader performs large numbers of read operations of small byte sizes on the data stream. This behavior has very
adverse performance effects when reading from remote storage, where each read operation carries networking overhead.

To alleviate this, the `read_dicom` uses an **internal read-ahead buffer**. Instead of reading small byte ranges
directly from the remote storage handle, larger data chunks are loaded into the internal buffer. Then, parsing requests
are always served from this internal buffer first. Only when DCMTK requests data beyond the buffer range, a refill
operation is issued to load the next chunk of data from the remote into the buffer. This strategy reduces the total
number of network operations, in line with DuckDB's approach to I/O.

**Configuration settings**

* `read_dicom_internal_buffer_size` (Default: 128): Controls the size (in KB) of the thread-local prefetch buffer.

* `read_dicom_batch_size` (Default: 64, Max: 2048): Controls the number of files assigned to a single thread during a
single execution task. Once a thread finishes its assigned batch, DuckDB assigns it another batch until all files are
processed.

**Tuning recommendations**

* Setting `read_dicom_batch_size`: If the total file count is known ahead of time, distribute work evenly across
available CPU cores: `read_dicom_batch_size = <number of files to read> / <number of machine cores>`.

_Note_ Setting batch sizes too low increases scheduling overhead, while setting them too high can cause thread underuse.

* Setting `read_dicom_internal_buffer_size`: Start with the default 128 KB setting. Increase the size incrementally
(e.g., to 256 KB or 512 KB) for files with large metadata headers until performance gains plateau. Optimal values depend
on your specific dataset and other use case characteristics.
