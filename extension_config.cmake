# Extension from this repo
duckdb_extension_load(dicom
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
)

# Any extra extensions that should be built
duckdb_extension_load(json)
duckdb_extension_load(httpfs
    GIT_URL https://github.com/duckdb/duckdb-httpfs
    GIT_TAG f71cfdb4bff85b9e15b1f3b842d9aa395aff0167
)
