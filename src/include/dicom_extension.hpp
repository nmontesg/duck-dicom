#pragma once

#include "duckdb.hpp" // IWYU pragma: keep

#define EXTENSION_NAME "dicom"

namespace duckdb {

class DicomExtension : public Extension {
public:
	void Load(ExtensionLoader &) override;
	std::string Name() override;
	std::string Version() const override;
};

} // namespace duckdb
