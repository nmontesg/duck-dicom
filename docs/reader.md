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
| `path` | `VARCHAR` | Path to the DICOM file. |
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
