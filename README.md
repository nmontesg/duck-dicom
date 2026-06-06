# DICOM extension for DuckDB

The `dicom` extension for DuckDB provides functionality to import medical imaging data (DICOM,
Digital Imaging and Communication in Medicine) directly into DuckDB. It uses the powerful [DCMTK C++
library](https://dicom.offis.de/dcmtk.php.en) to read DICOM files and convert them into JSON format.

**NOTE**: This extension is not supported in WebAssembly.

## Features

* **Import medical image data directly into DuckDB**: The `read_dicom(FILEPATH)` function imports
  DICOM files in JSON format directly into DuckDB.

## Quick start

```sql
INSTALL dicom FROM community;
LOAD dicom;

-- read one file
FROM read_dicom('path/to/dicom_file.dcm');
```

## Reader function

**`read_dicom(filepath[, load_pixel_data=false])`**

Read DICOM files directly into DuckDB, return one row per file and columns `path (VARCHAR)` (path to
the file) and `dicom_content (JSON)` (JSON-rendered contents of the DICOM file).

Parameters:

* `filepath`: path to the DICOM file to be imported. Also accepts glob patterns.

* `load_pixel_data` (optional): whether to strip the pixel data (DICOM tag 7FE0,0010) from the
  contents. Default is false.

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

The `dicom` extension also supports reading from cloud object storage through the
[`httpfs` extension](https://duckdb.org/docs/current/core_extensions/httpfs/overview):

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

## `DICOM_TAG` type

The extension provides a custom `DICOM_TAG` type and scalar functions for working with DICOM tags. Tags can be created
in several ways:

```sql
CREATE TABLE dicom_tags_test (id INTEGER, tag DICOM_TAG);

-- explicitly specifying the group and element
INSERT INTO dicom_tags_test VALUES (1, {'group': '0x0008', 'elem': '0x012D'});

-- using a comma to format the tag as GROUP,ELEMENT
INSERT INTO dicom_tags_test
VALUES
  (1, '0008,0008'),
  (2, '8,8'),
  (3, '0008,012D'),
  (4, '8,12D'),
  (5, '0008,012d'),
  (6, '8,12d');

-- when not using a comma to separate group and element, the group is parsed
-- from the first 4 characters and the element from the last 4
INSERT INTO dicom_tags_test
VALUES
  (1, '00080008'),
  (2, '0008012D'),
  (3, '0008012d');

-- extract the DICOM tags from a file
SELECT unnest(json_keys(dicom_content))::dicom_tag
FROM read_dicom('path/to/dicom/file.dcm');
```

### Scalar functions for DICOM tags

**`tag_group(DICOM_TAG)`**

Extracts the 16-bit group number from the DICOM tag and returns it as a 4-character, zero-padded hexadecimal `VARCHAR`.

**Examples:**

```sql
SELECT tag_group('0010,0010') AS group_id;

-- extract unique tag groups from a DICOM file
SELECT DISTINCT tag_group(unnest(json_keys(dicom_content))::DICOM_TAG) 
FROM read_dicom('/path/to/dicom_file.dcm');
```

**``tag_element(DICOM_TAG)``**

Extracts the 16-bit element number from the DICOM tag and returns it as a 4-character, zero-padded hexadecimal `VARCHAR`.

**Examples:**

```sql
SELECT tag_element('0010,0020') AS element_id;
```

**``tag_name(DICOM_TAG)``**

Look up the standard human-readable keyword/name for the given DICOM tag based on the standard DICOM dictionary. If the
tag is private or unknown, it returns the tag formatted as `GGGG,EEEE`.

**Examples:**

```sql
SELECT tag_name('0010,0010') AS tag_keyword;
```

### Comprehensive example

Below is an example of how these functions can be combined to parse and analyze all the tags present inside a DICOM file:

```sql
SELECT 
    tag AS raw_tag,
    tag_name(tag) AS keyword,
    tag_group(tag) AS group_hex,
    tag_element(tag) AS element_hex
FROM (
    SELECT unnest(json_keys(dicom_content))::DICOM_TAG AS tag 
    FROM read_dicom('/path/to/dicom_file.dcm')
);
```

## Roadmap

[ ] DICOM networking functions to import DICOM datasets through C_FIND, C-MOVE commands
