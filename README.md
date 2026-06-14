# DICOM extension for DuckDB

The `dicom` extension for DuckDB provides functionality to import medical imaging data (DICOM, Digital Imaging and
Communication in Medicine) directly into DuckDB. It uses the powerful [DCMTK C++
library](https://dicom.offis.de/dcmtk.php.en) to read DICOM files and convert them into JSON format.

**NOTE**: This extension is not supported in WebAssembly.

## Features

* **Import medical image data directly into DuckDB**: The `read_dicom(FILEPATH)` function imports DICOM files in JSON
  format directly into DuckDB.

## Quick start

```sql
INSTALL dicom FROM community;
LOAD dicom;

-- read one file
FROM read_dicom('path/to/dicom_file.dcm');
```

## Reader function

**`read_dicom(filepath[, load_pixel_data=false])`**

Read DICOM files directly into DuckDB, returning one row per file.

 <!-- and columns `path (VARCHAR)` (path to the file) and -->
<!-- `dicom_content (JSON)` (JSON-rendered contents of the DICOM file). -->

**Parameters:**

| Parameter | Type | Description | Required |
|-----------|------|-------------|----------|
| `filepath` | `VARCHAR` | Path to the DICOM file read or glob pattern. | Yes |
| `load_pixel_data`| `BOOL` | Whether to import the pixel data. Default is false. | No |

**Return schema:**

| Column Name | Data Type | Description |
|-------------|-----------|-------------|
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

Extracts the 16-bit element number from the DICOM tag and returns it as a 4-character, zero-padded hexadecimal
`VARCHAR`.

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

Below is an example of how these functions can be combined to parse and analyze all the tags present inside a DICOM
file:

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

-- parse DICOM file as table
WITH cte AS
(
  SELECT
    unnest(json_keys(dicom_content)) AS dicom_key,
    json_extract(dicom_content, '$.' || dicom_key || '.Value') AS dicom_value
  FROM read_dicom('/path/to/dicom_file.dcm')
)
SELECT
  dicom_key::DICOM_TAG AS tag,
  tag_name(dicom_key) AS tag_name,
  CASE 
    WHEN json_array_length(dicom_value) = 1 THEN (dicom_value->>'$[0]')::VARIANT
    ELSE dicom_value::VARIANT
  END AS dicom_value
FROM cte;
```

## DICOM networking features

### `DICOM` secret

The extension provides a custom `dicom` secret type to store connection details for other DICOM modalities:

```sql
-- define a DICOM modality, AE title is optional
CREATE OR REPLACE SECRET my_dicom_secret (
  TYPE dicom,
  HOST 'localhost',
  PORT 4242,
  AETITLE 'ORTHANC'
);

-- store TLS key and certificates
CREATE OR REPLACE SECRET my_dicom_secret (
  TYPE dicom,
  HOST 'localhost',
  PORT 4242,
  TLS_CA_FILE 'path/to/certificate.crt',
  TLS_KEY_FILE 'path/to/private.key',
  PEER_CA_FILE 'path/to/peer/certificate.crt'
);
```

When specifying a `dicom` secret with TLS credentials, the following three paths must be provided:

* `TLS_CA_FILE`: Path to the local client public identity certificate (.crt or .pem)

* `TLS_KEY_FILE`: Path to the local client private key (.key) used to sign connections to the secure transport layer.

* `PEER_CA_FILE`: Path to the trusted Root Certificate Authority (CA) bundle used to verify the identity certificate of
  the remote DICOM host.

### `query_dicom` table function

The `query_dicom` function execute DICOM C-FIND query requests directly from DuckDB to a remote DICOM peer
(such as a PACS), and parses the responses directly into a DuckDB table. The result table has a single column named
`dicom_response` of type `JSON`.

```sql
SELECT * FROM query_dicom(
    host = 'localhost',
    port = 4242,
    aetitle = 'ORTHANC',
    tls_ca_file = 'my/certificate.crt',
    tls_key_file = 'my/keyfile.key',
    peer_ca_file = 'trusted.crt',
    query = {'Modality': 'MR'},
    acse_timeout = 30,
    dimse_timeout = 0,
    max_receive_pdu_length = 16384
);
```

**Parameters:**

| Parameter | Type | Description | Required |
|-----------|------|-------------|----------|
| `host` | `VARCHAR` | The IP address or hostname of the remote DICOM Application Entity (AE). | Yes |
| `port` | `UINTEGER` | "The network port of the remote DICOM peer (e.g., 104, 11112, or 2762 for TLS)." | Yes |
| `aetitle` | `VARCHAR` | The Called Application Entity Title (AE Title) of the remote node. | No |
| `query` | `MAP` | "A key-value struct representing the matching tags for the C-FIND request (e.g., {'PatientName': 'John*', 'Modality': 'CT'})." | Yes |
| `tls_ca_file` | `VARCHAR` | Path to your local client identity public certificate (.crt/.pem) for mTLS. | No |
| `tls_key_file` | `VARCHAR` | Path to your local client private key file (.key) for mTLS. | No |
| `peer_ca_file` | `VARCHAR` | Path to the trusted Root Certificate Authority bundle to verify the peer. | No |
| `acse_timeout` | `UINTEGER` | Timeout limit (in seconds) for the Association Control Service Element (ACSE) connection handshakes. Default is 30 | No |
| `dimse_timeout` | `UINTEGER` | Timeout limit (in seconds) for waiting on active DICOM Message Service Element (DIMSE) network operations and message streaming. Default is 0 (no timeout) | No |
| `max_receive_pdu_length` | `UINTEGER` | The maximum Protocol Data Unit (PDU) buffer size (in bytes) that the client is willing to accept per network transaction frame. Default is 16384 (16kB) | No |

The `host`, `port`, `aetitle` and TLS-related fields can also be retrieved from a named DICOM secret:

```sql
SELECT * FROM query_dicom(secret='my_dicom_secret', query = {'Modality': 'MR'});
```

For more information concerning the available options and query syntax, visit the [DCMTK FindSCU
documentation](https://support.dcmtk.org/docs/findscu.html).

**Return schema:**

| Column Name | Data Type | Description |
|-------------|-----------|-------------|
| `dicom_response` | `JSON` | A compact JSON object containing the DICOM dataset returned by the C-FIND match. |

## Roadmap

[ ] DICOM networking functions to import DICOM datasets through C-MOVE commands
