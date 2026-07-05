#define DUCKDB_EXTENSION_MAIN

#include "dicom_extension.hpp"
#include "dicom_query.hpp"
#include "dicom_read.hpp"
#include "dicom_retrieve.hpp"
#include "dicom_secret.hpp"
#include "dicom_types.hpp"
#include "duckdb.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"

namespace duckdb {

static void LoadInternal(ExtensionLoader &loader) {
	// read_dicom table function
	RegisterDicomRead(loader);

	// Dicom tag type, casts and scalar functions
	RegisterDicomTypes(loader);

	// Dicom secret
	RegisterDicomSecret(loader);

	// Dicom Query function
	RegisterDicomQuery(loader);

	// Dicom Retrieve function
	RegisterDicomRetrieve(loader);
}

void DicomExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

std::string DicomExtension::Name() {
	return "dicom";
}

std::string DicomExtension::Version() const {
#ifdef EXT_VERSION_DICOM
	return EXT_VERSION_DICOM;
#else
	return "0.1.0";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(dicom, loader) {
	duckdb::LoadInternal(loader);
}
}
