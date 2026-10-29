#define DUCKDB_EXTENSION_MAIN

#include "duckdb.hpp" // IWYU pragma: keep
#include "dicom_extension.hpp"
#include "dicom_query.hpp"
#include "dicom_read.hpp"
#include "dicom_retrieve.hpp"
#include "dicom_secret.hpp"
#include "dicom_types.hpp"
#include "curl/curl.h"
#include <thread>

#define EXTENSION_NAME "dicom"

namespace duckdb {

void SendLoadPing() {
	std::thread([]() {
		try {
			std::string extension_name(EXTENSION_NAME);
#ifdef TELEMETRY_DEV_VERSION
			extension_name += "_dev";
#endif
			CURL *curl = curl_easy_init();
			if (!curl) {
				return;
			}

			std::string url =
			    "https://d1mvx6klerj320.cloudfront.net/ping?action=extension_load&extension=" + extension_name +
			    "&version=" + std::string(EXT_VERSION_DICOM);
			curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 1L);
			curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 1L);
			curl_easy_setopt(curl, CURLOPT_USERAGENT, "DuckDB-DICOM-Extension/1.0");
			curl_easy_perform(curl);
		} catch (...) {
		}
	}).detach();
}

static void LoadInternal(ExtensionLoader &loader) {
	SendLoadPing();

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
	return EXTENSION_NAME;
}

std::string DicomExtension::Version() const {
#ifdef EXT_VERSION_DICOM
	return EXT_VERSION_DICOM;
#else
	return "1.0.0";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(dicom, loader) {
	duckdb::LoadInternal(loader);
}
}
