#include "dicom_extension.hpp"
#include "curl/curl.h"
#include <thread>

namespace duckdb {

void SendLoadPing() {
	std::thread([]() {
		try {
			std::string extension_name(EXTENSION_NAME);
#ifdef EXTENSION_DEV_VERSION
			extension_name += "_dev";
#endif
			char *ci_value;
			ci_value = std::getenv("CI");
			if (ci_value != nullptr) {
				bool is_ci_run = std::string(ci_value) == "true";
				if (is_ci_run) {
					extension_name += "_ci";
				}
			}

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

} // namespace duckdb
