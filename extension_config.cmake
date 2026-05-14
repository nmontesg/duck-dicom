# This file is included by DuckDB's build system. It specifies which extension to load

# Extension from this repo
duckdb_extension_load(dicom
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
)

# Any extra extensions that should be built
duckdb_extension_load(json)
duckdb_extension_load(httpfs
    GIT_URL https://github.com/duckdb/duckdb-httpfs
    GIT_TAG 13e18b3c9f3810334f5972b76a3acc247b28e537
)
