# DICOM networking

## `DICOM` secret

The custom `dicom` secret type stores connection details for other DICOM modalities:

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

## `query_dicom`

Execute DICOM C-FIND query requests directly from DuckDB to a remote DICOM peer (such as a PACS), and parse the
responses directly into a DuckDB table. The resulting table has a single column named `dicom_response` of type `JSON`.

```sql
SELECT * FROM query_dicom(
    host = 'localhost',
    port = 4242,
    aetitle = 'ORTHANC',
    tls_ca_file = 'my/certificate.crt',
    tls_key_file = 'my/keyfile.key',
    peer_ca_file = 'trusted.crt',
    qr_level = 'study',
    acse_timeout = 30,
    dimse_timeout = 0,
    max_receive_pdu_length = 16384,
    match_keys = {},
    retrieve_keys = []
);
```

**Parameters:**

| Parameter | Type | Description | Required |
|-----------|------|-------------|----------|
| `host` | `VARCHAR` | The IP address or hostname of the remote DICOM Application Entity (AE). | Yes |
| `port` | `UINTEGER` | The DICOM network port of the remote DICOM peer. | Yes |
| `aetitle` | `VARCHAR` | The called Application Entity Title (AE Title) of the remote node. Default is `'ANY-SCP'`. | No |
| `tls_ca_file` | `VARCHAR` | Path to your local client identity public certificate (.crt/.pem) for TLS. Default is empty. | No |
| `tls_key_file` | `VARCHAR` | Path to your local client private key file (.key) for TLS. Default is empty. | No |
| `peer_ca_file` | `VARCHAR` | Path to the trusted Root Certificate Authority bundle to verify the peer. Default is empty. | No |
| `qr_level` | `VARCHAR` | Query/Retrieve level. Options are: `'patient'`, `'study'`, `'series'`, `'image'`. Default is `'study'`. | No |
| `acse_timeout` | `UINTEGER` | Timeout limit (in seconds) for the Association Control Service Element (ACSE) connection handshakes. Default is 30. | No |
| `dimse_timeout` | `UINTEGER` | Timeout limit (in seconds) for waiting on active DICOM Message Service Element (DIMSE) network operations and message streaming. Default is 0 (no timeout). | No |
| `max_receive_pdu_length` | `UINTEGER` | The maximum Protocol Data Unit (PDU) buffer size (in bytes) that the client is willing to accept per network transaction frame. Default is 16384 (16kB). | No |
| `match_keys` | `MAP(VARCHAR, VARCHAR)` | A key-value struct representing the matching tags for the C-FIND request (e.g., `{'PatientName': 'John*', 'Modality': 'CT'}`). Default is empty. | No |
| `retrieve_keys` | `LIST(VARCHAR)` | A list of extra tags to retrieve in the response (e.g. `['ProtocolName', 'StudyInstanceUID']`). Default is empty. | No |

The `host`, `port`, `aetitle` and TLS-related fields can also be retrieved from a named DICOM secret:

```sql
SELECT * FROM query_dicom(secret='my_dicom_secret', query = {'Modality': 'MR'});
```

**Return schema:**

| Column Name | Data Type | Description |
|-------------|-----------|-------------|
| `dicom_response` | `JSON` | A compact JSON object containing the DICOM dataset returned by the C-FIND match. |

**Examples:**

```sql
-- retrieve all patient IDs and birth dates
FROM query_dicom(
  secret='my_dicom_secret',
  qr_level='patient',
  retrieve_keys=['PatientID', 'PatientBirthDate']);

-- retrieve all study UIDs of MR studies
FROM query_dicom(
  secret='my_dicom_secret',
  qr_level='study',
  match_keys={'Modality': 'MR'},
  retrieve_keys=['StudyInstanceUID']);

-- retrieve series UIDs and descriptions for all series
FROM query_dicom(
  secret='my_dicom_secret',
  qr_level='series',
  match_keys={'SeriesDate': '20070101'},
  retrieve_keys=['SeriesInstanceUID', 'SeriesDescription']);

-- retrieve position and image UIDs for all MR images 
FROM query_dicom(
  secret='my_dicom_secret',
  qr_level='image',
  match_keys={'Modality': 'MR'},
  retrieve_keys=['ImagePositionPatient', 'SOPInstanceUID']);
```

## `retrieve_dicom`

Similar to the `query_dicom` function, but execute C-MOVE requests to a remote DICOM modality to retrieve the full DICOM
dataset(s) corresponding to a patient, study, series or instance. Takes as input a sequence of unique IDs and returns a
table with two columns: one for the unique ID from which the dataset was retrieved, and the other with the complete
dataset.

```sql
SELECT * FROM retrieve_dicom(
    '1.2.826.0.1.3680043.8.1055.1.20111103111148288.98361414.79379639',
    '1.2.826.0.1.3680043.8.1055.1.20111103111204584.92619625.78204558',
    ...,
    host = 'localhost',
    port = 4242,
    incoming_port=11112,
    aetitle = 'ORTHANC',
    calling_aetitle='DUCKDB';
    tls_ca_file = 'my/certificate.crt',
    tls_key_file = 'my/keyfile.key',
    peer_ca_file = 'trusted.crt',
    qr_level = 'study',
    acse_timeout = 30,
    dimse_timeout = 0,
    max_receive_pdu_length = 16384,
    load_pixel_data = false
);
```

Since `retrieve_dicom` uses C-MOVE commands, the peer DICOM modality (e.g. a PACS) opens a new connection to send the
full DICOM datasets over `incoming_port`. Hence, DuckDB must be configured as a trusted remote modality in the PACS. The
configured AE title and port must match the `calling_aetitle` and `incoming_port` parameters, respectively.

The `retrieve_dicom` function can retrieve DICOM datasets corresponding to a patient, study, series or instance. The
level at which the data is retrieved is controlled through the `qr_level` parameter.

**Parameters:**

| Parameter | Type | Description | Required |
|-----------|------|-------------|----------|
| `host` | `VARCHAR` | The IP address or hostname of the remote DICOM Application Entity (AE). | Yes |
| `port` | `UINTEGER` | The DICOM network port of the remote DICOM peer. | Yes |
| `incoming port` | `UINTEGER` | The DICOM network port for the incoming connection initiated by the peer. | Yes |
| `aetitle` | `VARCHAR` | The called Application Entity Title (AE Title) of the remote node. Default is `'ANY-SCP'`. | No |
| `calling_aetitle` | `VARCHAR` | The Application Entity Title (AE Title) for DuckDB. Default is `'DUCKDB'`. | No |
| `qr_level` | `VARCHAR` | Query/Retrieve level. Options are: `'patient'`, `'study'`, `'series'`, `'image'`. Default is `'study'`. | No |
| `acse_timeout` | `UINTEGER` | Timeout limit (in seconds) for the Association Control Service Element (ACSE) connection handshakes. Default is 30 | No |
| `dimse_timeout` | `UINTEGER` | Timeout limit (in seconds) for waiting on active DICOM Message Service Element (DIMSE) network operations and message streaming. Default is 0 (no timeout) | No |
| `max_receive_pdu_length` | `UINTEGER` | The maximum Protocol Data Unit (PDU) buffer size (in bytes) that the client is willing to accept per network transaction frame. Default is 16384 (16kB) | No |
| `tls_ca_file` | `VARCHAR` | Path to your local client identity public certificate (.crt/.pem) for TLS. | No |
| `tls_key_file` | `VARCHAR` | Path to your local client private key file (.key) for tLS. | No |
| `peer_ca_file` | `VARCHAR` | Path to the trusted Root Certificate Authority bundle to verify the peer. | No |
| `load_pixel_data`| `BOOL` | Whether to import the pixel data. Default is false. | No |

Similarly to `query_dicom`, The `host`, `port`, `aetitle` and TLS-related fields can also be retrieved from a named
DICOM secret:

```sql
SELECT * FROM query_dicom('1.2.3...', ..., secret='my_dicom_secret', incoming_port=11112);
```

**Return schema:**

| Column Name | Data Type | Description |
|-------------|-----------|-------------|
| `patient_id` or `[study,series,image]_instance_uid` | `VARCHAR` | The patient, study, series or SOP instance unique ID. |
| `dicom_response` | `JSON` | A compact JSON object containing the DICOM dataset returned by the C-FIND match. |

**Examples:**

```sql
-- extract all datasets for PatientID PAT123
FROM retrieve_dicom('PAT123', secret='dicom_test_secret', incoming_port=11112, qr_level='patient');

-- retrieve all datasets corresponding to a study, including pixel data
FROM retrieve_dicom(
    '1.2.826.0.1.3680043.8.1055.1.20111103111148288.98361414.79379639',
    secret='dicom_test_secret', incoming_port=11112, qr_level='study', load_pixel_data=true);

-- retrieve a couple of series
FROM retrieve_dicom(
    '1.2.826.0.1.3680043.8.1055.1.20111103111201370.72665630.67534267',
    '1.2.826.0.1.3680043.8.1055.1.20111103111204584.92619625.78204558',
    secret='dicom_test_secret', incoming_port=11112, qr_level='series');
```
