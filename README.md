# DICOM extension for DuckDB

The `dicom` extension for DuckDB provides functionality to import medical imaging data (DICOM,
Digital Imaging and Communication in Medicine) directly into DuckDB. It uses the powerful [DCMTK C++
library](https://dicom.offis.de/dcmtk.php.en) to read DICOM files and convert them into JSON format.

**NOTE**: This extension is not supported in WebAssembly.

## Features

* **Import medical image data directly into DuckDB**: The `read_dicom(FILEPATH)` function imports
  DICOM files in JSON format directly into DuckDB.

## Quickstart

```sql
INSTALL dicom FROM community;
LOAD dicom;

-- read one file
FROM read_dicom('path/to/dicom_file.dcm');
```

## Functions

### `read_dicom(filepath[, load_pixel_data=false])`

Read DICOM files directly into DuckDB, return one row per file and columns `path (VARCHAR)` (path to
the file) and `dicom_content (JSON)` (JSON-rendered contents of the DICOM file).

Parameters:

* `filepath`: path to the DICOM file to be imported. Also accepts glob patterns.

* `load_pixel_data` (optional): whether to strip the pixel data (DICOM tag 7FE0,0010) from the
  contents. Default is false.

## Examples

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
FROM read_dicom('/Users/nmontes/Downloads/slicer_export/**/*.dcm')
GROUP BY 1;

-- extract study description per study instance
SELECT
    dicom_content->'0020000D'->'Value' AS study_instance_uid,
    any_value(dicom_content->'00081030'->'Value') AS study_description
FROM read_dicom('/Users/nmontes/Downloads/slicer_export/**/*.dcm')
GROUP BY 1;
```

## Roadmap

* Predicate pushdown in `read_dicom`

* DICOM networking functions to import DICOM datasets through C-MOVE commands
