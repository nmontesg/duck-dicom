# Extension from this repo
duckdb_extension_load(dicom
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
)

# Any extra extensions that should be built
duckdb_extension_load(json)
duckdb_extension_load(httpfs
    GIT_URL https://github.com/duckdb/duckdb-httpfs
    GIT_TAG 96a2f2e88e5dd075facbc5a65dc3afd67aa2bb44
)
