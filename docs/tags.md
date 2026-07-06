# DICOM tags

## `DICOM_TAG` type

DICOM tags can be created in several ways:

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

## Scalar functions for DICOM tags

### `tag_group`

Extract the 16-bit group number from the DICOM tag and returns it as a 4-character, zero-padded hexadecimal `VARCHAR`.

**Examples:**

```sql
SELECT tag_group('0010,0010') AS group_id;

-- extract unique tag groups from a DICOM file
SELECT DISTINCT tag_group(unnest(json_keys(dicom_content))::DICOM_TAG) 
FROM read_dicom('/path/to/dicom_file.dcm');
```

### `tag_element`

Extract the 16-bit element number from the DICOM tag and returns it as a 4-character, zero-padded hexadecimal
`VARCHAR`.

**Examples:**

```sql
SELECT tag_element('0010,0020') AS element_id;
```

### `tag_name`

Look up the standard human-readable keyword/name for the given DICOM tag based on the standard DICOM dictionary. If the
tag is private or unknown, it returns the tag formatted as `GGGG,EEEE`.

**Examples:**

```sql
SELECT tag_name('0010,0010') AS tag_keyword;
```

## Comprehensive example

Below is an example of how these functions can be combined to parse and analyze all the tags present inside a DICOM
file:

```sql
-- extract the raw tags, group, element and name from a DICOM file
SELECT 
    tag AS raw_tag,
    tag_group(tag) AS group_hex,
    tag_element(tag) AS element_hex,
    tag_name(tag) AS keyword
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
