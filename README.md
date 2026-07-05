# DICOM extension for DuckDB

The `dicom` extension for DuckDB provides functionality to import and query medical imaging data (DICOM, Digital Imaging
and Communication in Medicine) directly into DuckDB. It uses the powerful [DCMTK C++
library](https://dicom.offis.de/dcmtk.php.en) to import DICOM files and datasets and parse them into DuckDB-friendly
JSON format.

**NOTE**: This extension is not supported in WebAssembly.

## Features

* **Import medical image data directly into DuckDB**: The `read_dicom` function imports DICOM files in JSON format
  directly into DuckDB.

* **Work with DICOM tags**: The `DICOM_TAG` type and several scalar functions parse DICOM tags and transform them for
  presentation-ready reports.

* **DICOM networking**: Store credentials to connect to remote modalities with the custom `dicom` secret type. Extract
  targeted information or entire datasets from remote modalities with `query_dicom` and `retrieve_dicom`.

For more information and examples, see the [API reference](docs/api.md).

## Quick start

```sql
INSTALL dicom FROM community;
LOAD dicom;

-- read one file
FROM read_dicom('path/to/dicom_file.dcm');
```

## Get in touch

Feel free to share feedback, issues and feature requests in [GitHub](https://github.com/nmontesg/duck-dicom/issues).
